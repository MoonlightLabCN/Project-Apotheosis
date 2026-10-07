// PortUIBridge.cpp — see PortUIBridge.h for the design and the threading rules.

#include "config.h"

#include "PortUIBridge.h"

#include <WebCore/FileChooser.h>
#include <WebCore/PopupMenu.h>
#include <WebCore/PopupMenuClient.h>
#include <WebCore/LocalFrameView.h>
#include <wtf/MainThread.h>
#include <wtf/RefPtr.h>
#include <wtf/Ref.h>
#include <wtf/RefPtr.h>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>

namespace WebCorePort {

using namespace WebCore;

namespace {

struct PendingRequest {
    uint64_t id { 0 };
    int kind { UIRequestNone };
    uint64_t generation { 0 };
    std::string payload;
    RefPtr<FileChooser> chooser;   // FileChooser only
    // Apotheosis (0.2.5.15): the <select> counterpart. WeakPtr-capable by the
    // interface itself (AbstractRefCountedAndCanMakeWeakPtr), so this stays valid
    // to test: RefPtr keeps the object alive, and the element dying does not
    // dangle - it just means a later valueChanged() is a no-op.
    RefPtr<PopupMenuClient> client;
};

// The one parked dialog. Only one can exist at a time: the engine thread is
// inside enqueueConfirm/enqueuePrompt for as long as it lives, and nothing else
// on that thread can enqueue a second one. Guarded by queueMutex; the answer
// side sets the fields under the same mutex and notifies the CV.
struct DialogWaiter {
    uint64_t id { 0 };
    bool active { false };
    bool answered { false };
    bool ok { false };
    std::string text;
};

uint64_t g_nextRequestId = 1;
uint64_t g_sessionGeneration = 1;

// Every queue operation and the waiter live under this one mutex. The engine
// drain path used to be lock-free (single consumer); the confirm/prompt answer
// path added a second, UI-thread consumer, and a std::deque is not safe for
// concurrent pop/push.
std::mutex g_queueMutex;

// Requests waiting to be handed to the shell.
std::deque<PendingRequest>& queued()
{
    static std::deque<PendingRequest> q;
    return q;
}

// Requests the shell has taken and still owes an answer for. Bounded: a page that
// spams file inputs must not be able to grow this without limit on a phone.
std::deque<PendingRequest>& inFlight()
{
    static std::deque<PendingRequest> q;
    return q;
}

constexpr size_t maxQueued = 8;
constexpr size_t maxInFlight = 8;

// A dialog the shell never answers would park the engine thread (and with it
// every navigation and every frame) for as long as the process lives. Suspend,
// a torn-down window, a dialog the shell fails to show - all end with Cancel,
// which is what every site expects from a dialog the user walked away from.
constexpr int kDialogTimeoutMs = 60 * 1000;

DialogWaiter g_dialog;
std::condition_variable g_dialogCV;

UIRequestWakeCallback g_wakeCallback = nullptr;
void* g_wakeContext = nullptr;

void appendTabSeparated(std::string& out, const String& value)
{
    out += '\t';
    auto utf8 = value.utf8();
    if (utf8.data())
        out.append(utf8.data(), utf8.length());
}

// Pop the oldest live request under the lock. Shared by the engine drain and the
// UI-thread wake path.
bool popRequestLocked(int& outKind, uint64_t& outId, std::string& outPayload, bool& outTracked)
{
    while (!queued().empty()) {
        PendingRequest request = WTF::move(queued().front());
        queued().pop_front();

        // Queued before a navigation that has since happened: nobody is waiting
        // for this any more.
        if (request.generation != g_sessionGeneration) {
            if (RefPtr chooser = request.chooser)
                chooser->cancelFileChoosing();
            continue;
        }

        outKind = request.kind;
        outId = request.id;
        outPayload = request.payload;

        // Alerts and new-window requests get no reply, so they never enter the
        // in-flight set (nothing in the engine is waiting on an answer).
        if (request.kind != UIRequestAlert && request.kind != UIRequestNewWindow) {
            while (inFlight().size() >= maxInFlight) {
                if (RefPtr chooser = inFlight().front().chooser)
                    chooser->cancelFileChoosing();
                inFlight().pop_front();
            }
            inFlight().push_back(WTF::move(request));
            outTracked = true;
        } else
            outTracked = false;
        return true;
    }
    return false;
}

} // namespace

uint64_t currentSessionGeneration()
{
    return g_sessionGeneration;
}

void bumpSessionGeneration()
{
    std::lock_guard<std::mutex> lock(g_queueMutex);
    ++g_sessionGeneration;
}

uint64_t enqueueFileChooser(FileChooser& chooser)
{
    ASSERT(isMainThread());

    std::lock_guard<std::mutex> lock(g_queueMutex);

    // A page can call click() on a file input in a loop. Drop the oldest rather
    // than grow: the user can only answer one picker at a time anyway.
    while (queued().size() >= maxQueued) {
        if (RefPtr stale = queued().front().chooser)
            stale->cancelFileChoosing();
        queued().pop_front();
    }

    PendingRequest request;
    request.id = g_nextRequestId++;
    request.kind = UIRequestFileChooser;
    request.generation = g_sessionGeneration;
    request.chooser = &chooser;

    const auto& settings = chooser.settings();
    request.payload = settings.allowsMultipleFiles ? "1" : "0";
    for (const auto& mimeType : settings.acceptMIMETypes)
        appendTabSeparated(request.payload, mimeType);
    for (const auto& extension : settings.acceptFileExtensions)
        appendTabSeparated(request.payload, extension);

    uint64_t id = request.id;
    queued().push_back(WTF::move(request));
    return id;
}

uint64_t enqueueAlert(const std::string& utf8Message)
{
    ASSERT(isMainThread());
    std::lock_guard<std::mutex> lock(g_queueMutex);
    while (queued().size() >= maxQueued)
        queued().pop_front();

    PendingRequest request;
    request.id = g_nextRequestId++;
    request.kind = UIRequestAlert;
    request.generation = g_sessionGeneration;
    request.payload = utf8Message;

    uint64_t id = request.id;
    queued().push_back(WTF::move(request));
    return id;
}

// Apotheosis (0.2.5.15): <select> popup. The shell shows a native list and answers
// with an index, which comes back through completeSelectPopup() ->
// PopupMenuClient::valueChanged(). Every rule the file chooser already follows
// applies: bounded queue, generation check, and the client is a weak pointer
// (AbstractRefCountedAndCanMakeWeakPtr), so a dead <select> cannot be touched.
//
// Payload: "<selectedIndex>\t<text>\t<text>\t..." (UTF-8, NUL-free). A phone
// dropdown is tens of entries at most; the cap is a layout guard on the shell
// card, not a semantic limit, and it is applied here so a pathological
// <select> with ten thousand options cannot blow up the payload buffer.
uint64_t enqueueSelectPopup(PopupMenuClient& client)
{
    ASSERT(isMainThread());

    std::lock_guard<std::mutex> lock(g_queueMutex);

    while (queued().size() >= maxQueued) {
        queued().pop_front();   // no chooser-style cancel: the shell never saw it
    }

    PendingRequest request;
    request.id = g_nextRequestId++;
    request.kind = UIRequestSelect;
    request.generation = g_sessionGeneration;
    request.client = &client;

    const int count = client.listSize();
    int selected = -1;
    // The shell needs to know which row starts highlighted. listSize() is the
    // number of ROWS (options + optgroup labels + separators), not options.
    for (int i = 0; i < count; ++i) {
        if (client.itemIsSelected(i)) { selected = i; break; }
    }

    constexpr int kMaxRows = 200;   // enough for any real dropdown on a phone
    request.payload = std::to_string(selected < 0 ? -1 : selected);
    for (int i = 0; i < count && i < kMaxRows; ++i) {
        // appendTabSeparated takes a WTF String; the row tags are ASCII literals and
        // the text arrives as one already, so this is the only conversion point.
        const String text = client.itemText(i);
        if (client.itemIsSeparator(i))
            appendTabSeparated(request.payload, String::fromUTF8("\x01separator"));
        else if (client.itemIsLabel(i))
            appendTabSeparated(request.payload, makeString("\x01label\t"_s, text));
        else if (client.itemIsEnabled(i))
            appendTabSeparated(request.payload, text);
        else
            appendTabSeparated(request.payload, makeString("\x01disabled\t"_s, text));
    }

    uint64_t id = request.id;
    queued().push_back(WTF::move(request));
    return id;
}

// -1 = the user cancelled; >= 0 = the row they picked.
void completeSelectPopup(uint64_t id, int listIndex)
{
    ASSERT(isMainThread());

    std::lock_guard<std::mutex> lock(g_queueMutex);

    for (auto it = inFlight().begin(); it != inFlight().end(); ++it) {
        if (it->id != id)
            continue;

        PendingRequest request = WTF::move(*it);
        inFlight().erase(it);

        RefPtr client = request.client;
        if (!client)
            return;
        // The user answered, but for a page that is no longer here.
        if (request.generation != g_sessionGeneration) {
            client->popupDidHide();
            return;
        }
        if (listIndex < 0) {
            client->popupDidHide();
            return;
        }

        client->valueChanged(static_cast<unsigned>(listIndex));
        return;
    }
    // Unknown id: a reply for a request already dropped as stale. Not an error.
}

// Apotheosis (0.2.5.15): the PopupMenu WebCore gets back from createPopupMenu().
// It exists because HTMLSelectElement::showPopup() calls show() on whatever the
// ChromeClient returned, so the old "return nullptr" meant a tap on a dropdown
// could do nothing at all. The real request is enqueued by queueSelectPopup()
// below (called from createPopupMenu) and the shell shows the card; the user's
// answer arrives later via completeSelectPopup().
//
// m_client is a WeakPtr on purpose: queueSelectPopup also parks a Ref in the
// pending request, so the client is alive for as long as the request is queued.
class PortSelectPopupMenu final : public PopupMenu {
public:
    explicit PortSelectPopupMenu(uint64_t requestId)
        : m_requestId(requestId)
    {
    }

private:
    void show(const IntRect&, LocalFrameView&, int) final
    {
        // Deliberately empty: the request was already queued by queueSelectPopup()
        // and the shell is showing the card. Enqueueing here instead would
        // double-queue every popup.
    }

    void hide() final
    {
        // The page closed its own popup. Ask the bridge to cancel it so the card
        // does not outlive the element that opened it.
        WebCorePort::cancelSelectPopup(m_requestId);
    }

    void updateFromElement() final
    {
    }

    void disconnectClient() final
    {
        m_requestId = 0;
    }

    uint64_t m_requestId;
};

// The single entry PortChromeClient::createPopupMenu calls: queue the request and
// hand WebCore the object it will call show() on. Returns nullptr only if the
// queue is closed.
RefPtr<PopupMenu> queueSelectPopup(PopupMenuClient& client, uint64_t& outId)
{
    outId = enqueueSelectPopup(client);
    return adoptRef(*new PortSelectPopupMenu(outId));
}

// The page closed its own popup (or the element went away). Drop the request so
// the shell's card does not outlive it, and tell the client it is hidden.
void cancelSelectPopup(uint64_t id)
{
    if (!id)
        return;
    ASSERT(isMainThread());

    std::lock_guard<std::mutex> lock(g_queueMutex);

    auto hideAndDrop = [](PendingRequest& request) {
        if (RefPtr client = request.client)
            client->popupDidHide();
        return true;
    };

    for (auto it = queued().begin(); it != queued().end(); ++it) {
        if (it->id != id)
            continue;
        hideAndDrop(*it);
        queued().erase(it);
        return;
    }
    for (auto it = inFlight().begin(); it != inFlight().end(); ++it) {
        if (it->id != id)
            continue;
        hideAndDrop(*it);
        inFlight().erase(it);
        return;
    }
    // Unknown id: already answered. Not an error.
}


uint64_t enqueueNewWindow(const std::string& utf8Url)
{
    ASSERT(isMainThread());
    std::lock_guard<std::mutex> lock(g_queueMutex);
    // 一个页面可以在循环里 window.open()。和 alert 一样丢最旧的:壳一次也只能切到一个标签。
    while (queued().size() >= maxQueued)
        queued().pop_front();

    PendingRequest request;
    request.id = g_nextRequestId++;
    request.kind = UIRequestNewWindow;
    request.generation = g_sessionGeneration;
    request.payload = utf8Url;

    uint64_t id = request.id;
    queued().push_back(WTF::move(request));
    return id;
}

bool takeNextUIRequest(int& outKind, uint64_t& outId, std::string& outPayload)
{
    ASSERT(isMainThread());
    outKind = UIRequestNone;
    outId = 0;
    outPayload.clear();

    std::lock_guard<std::mutex> lock(g_queueMutex);
    bool tracked = false;
    return popRequestLocked(outKind, outId, outPayload, tracked);
}

bool takeNextUIRequestFromUIThread(int& outKind, uint64_t& outId, std::string& outPayload)
{
    outKind = UIRequestNone;
    outId = 0;
    outPayload.clear();

    std::lock_guard<std::mutex> lock(g_queueMutex);
    bool tracked = false;
    return popRequestLocked(outKind, outId, outPayload, tracked);
}

int enqueueConfirm(const std::string& utf8Message)
{
    ASSERT(isMainThread());
    std::unique_lock<std::mutex> lock(g_queueMutex);

    while (queued().size() >= maxQueued)
        queued().pop_front();

    PendingRequest request;
    request.id = g_nextRequestId++;
    request.kind = UIRequestConfirm;
    request.generation = g_sessionGeneration;
    request.payload = utf8Message;
    const uint64_t id = request.id;
    queued().push_back(WTF::move(request));

    g_dialog = DialogWaiter();
    g_dialog.id = id;
    g_dialog.active = true;

    auto wake = g_wakeCallback;
    auto context = g_wakeContext;
    lock.unlock();
    // Wake the shell from here rather than making it poll: the engine thread is
    // about to park, so nothing on this side will run the queue's drain.
    if (wake)
        wake(context);

    lock.lock();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kDialogTimeoutMs);
    while (!g_dialog.answered && g_dialog.active) {
        if (g_dialogCV.wait_until(lock, deadline) == std::cv_status::timeout)
            break;
    }
    const bool answered = g_dialog.answered;
    const bool ok = g_dialog.ok;
    g_dialog.active = false;
    return (answered && ok) ? 1 : 0;
}

int enqueuePrompt(const std::string& utf8Message, const std::string& utf8Default, std::string& outText)
{
    ASSERT(isMainThread());
    outText.clear();
    std::unique_lock<std::mutex> lock(g_queueMutex);

    while (queued().size() >= maxQueued)
        queued().pop_front();

    PendingRequest request;
    request.id = g_nextRequestId++;
    request.kind = UIRequestPrompt;
    request.generation = g_sessionGeneration;
    request.payload = utf8Message;
    if (!utf8Default.empty())
        appendTabSeparated(request.payload, String::fromUTF8(utf8Default.c_str()));
    const uint64_t id = request.id;
    queued().push_back(WTF::move(request));

    g_dialog = DialogWaiter();
    g_dialog.id = id;
    g_dialog.active = true;

    auto wake = g_wakeCallback;
    auto context = g_wakeContext;
    lock.unlock();
    if (wake)
        wake(context);

    lock.lock();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(kDialogTimeoutMs);
    while (!g_dialog.answered && g_dialog.active) {
        if (g_dialogCV.wait_until(lock, deadline) == std::cv_status::timeout)
            break;
    }
    const bool answered = g_dialog.answered;
    const bool ok = g_dialog.ok;
    const std::string text = g_dialog.text;
    g_dialog.active = false;
    if (answered && ok)
        outText = text;
    return (answered && ok) ? 1 : 0;
}

int answerConfirm(uint64_t id, bool ok)
{
    std::lock_guard<std::mutex> lock(g_queueMutex);
    if (!g_dialog.active || g_dialog.id != id || g_dialog.answered)
        return 0;
    g_dialog.answered = true;
    g_dialog.ok = ok;
    g_dialogCV.notify_all();
    return 1;
}

int answerPrompt(uint64_t id, const char* utf8Text)
{
    std::lock_guard<std::mutex> lock(g_queueMutex);
    if (!g_dialog.active || g_dialog.id != id || g_dialog.answered)
        return 0;
    g_dialog.answered = true;
    g_dialog.ok = utf8Text != nullptr;
    g_dialog.text = utf8Text ? utf8Text : "";
    g_dialogCV.notify_all();
    return 1;
}

void setUIRequestWakeCallback(UIRequestWakeCallback callback, void* context)
{
    std::lock_guard<std::mutex> lock(g_queueMutex);
    g_wakeCallback = callback;
    g_wakeContext = context;
}

void completeFileChooser(uint64_t id, const std::vector<std::string>& utf8Paths)
{
    ASSERT(isMainThread());

    std::lock_guard<std::mutex> lock(g_queueMutex);

    for (auto it = inFlight().begin(); it != inFlight().end(); ++it) {
        if (it->id != id)
            continue;

        PendingRequest request = WTF::move(*it);
        inFlight().erase(it);

        RefPtr chooser = request.chooser;
        if (!chooser)
            return;
        // The user answered, but for a page that is no longer here.
        if (request.generation != g_sessionGeneration) {
            chooser->cancelFileChoosing();
            return;
        }
        if (utf8Paths.empty()) {
            chooser->cancelFileChoosing();
            return;
        }

        Vector<String> paths;
        paths.reserveInitialCapacity(utf8Paths.size());
        for (const auto& path : utf8Paths)
            paths.append(String::fromUTF8(path.c_str()));

        // Safe even if the <input> died while the picker was up: ~FileInputType
        // calls FileChooser::invalidate(), and chooseFiles() null-checks.
        chooser->chooseFiles(paths);
        return;
    }
    // Unknown id: a reply for a request already dropped as stale. Not an error.
}

void clearPendingUIRequests()
{
    ASSERT(isMainThread());
    std::lock_guard<std::mutex> lock(g_queueMutex);
    for (auto& request : queued()) {
        if (RefPtr chooser = request.chooser)
            chooser->cancelFileChoosing();
        // Apotheosis (0.2.5.15): a <select> whose popup the shell never showed
        // must be told it is over, or the element stays in its open state.
        if (RefPtr client = request.client)
            client->popupDidHide();
    }
    queued().clear();
    for (auto& request : inFlight()) {
        if (RefPtr chooser = request.chooser)
            chooser->cancelFileChoosing();
        if (RefPtr client = request.client)
            client->popupDidHide();
    }
    inFlight().clear();
    // A dialog that timed out while the page went away: nobody is coming to
    // answer it, and the parked engine (if the timeout has not fired yet) must
    // be released rather than left waiting on a request nobody will serve.
    if (g_dialog.active) {
        g_dialog.answered = true;
        g_dialog.ok = false;
        g_dialog.text.clear();
        g_dialogCV.notify_all();
    }
}

} // namespace WebCorePort
