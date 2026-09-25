// PortUIBridge.cpp — see PortUIBridge.h for the design and the threading rules.

#include "config.h"

#include "PortUIBridge.h"

#include <WebCore/FileChooser.h>
#include <wtf/MainThread.h>
#include <wtf/Ref.h>
#include <wtf/RefPtr.h>
#include <wtf/Vector.h>
#include <wtf/text/WTFString.h>

#include <deque>

namespace WebCorePort {

using namespace WebCore;

namespace {

struct PendingRequest {
    uint64_t id { 0 };
    int kind { UIRequestNone };
    uint64_t generation { 0 };
    std::string payload;
    RefPtr<FileChooser> chooser;   // FileChooser only
};

uint64_t g_nextRequestId = 1;
uint64_t g_sessionGeneration = 1;

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

void appendTabSeparated(std::string& out, const String& value)
{
    out += '\t';
    auto utf8 = value.utf8();
    if (utf8.data())
        out.append(utf8.data(), utf8.length());
}

} // namespace

uint64_t currentSessionGeneration()
{
    return g_sessionGeneration;
}

void bumpSessionGeneration()
{
    ++g_sessionGeneration;
}

uint64_t enqueueFileChooser(FileChooser& chooser)
{
    ASSERT(isMainThread());

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

uint64_t enqueueNewWindow(const std::string& utf8Url)
{
    ASSERT(isMainThread());
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
        }
        return true;
    }
    return false;
}

void completeFileChooser(uint64_t id, const std::vector<std::string>& utf8Paths)
{
    ASSERT(isMainThread());

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
    for (auto& request : queued()) {
        if (RefPtr chooser = request.chooser)
            chooser->cancelFileChoosing();
    }
    queued().clear();
    for (auto& request : inFlight()) {
        if (RefPtr chooser = request.chooser)
            chooser->cancelFileChoosing();
    }
    inFlight().clear();
}

} // namespace WebCorePort
