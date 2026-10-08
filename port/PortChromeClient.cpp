/*
 * PortChromeClient.cpp — out-of-line method bodies for WebCorePort::PortChromeClient.
 *
 * Bodies mirror WebCore's EmptyChromeClient (EmptyClients.cpp). The picker factories
 * (<select>, colour, date/time) do NOT mirror it: EmptyChromeClient hands back a
 * file-local no-op object, which on a phone means the user gets nothing at all and
 * no visible feedback. They go through PortUIBridge instead, so the shell can show
 * a real control - see PortUIBridge.h for the async rules.
 */

#include "config.h"
#include "PortChromeClient.h"

#include "PortUIBridge.h"

#include <WebCore/ColorChooser.h>
#include <WebCore/DataListSuggestionPicker.h>
#include <WebCore/DateTimeChooser.h>
#include <WebCore/FileChooser.h>
#include <WebCore/Icon.h>
#include <WebCore/NavigationAction.h>
#include <WebCore/Page.h>
#include <WebCore/PopupMenu.h>
#include <WebCore/SearchPopupMenu.h>
#include <wtf/CompletionHandler.h>
#include <wtf/text/CString.h>   // String::utf8() (addMessageToConsole -> consoleLogAppend)
#include <array>

namespace WebCorePort {

using namespace WebCore;

RefPtr<SearchPopupMenu> PortChromeClient::createSearchPopupMenu(PopupMenuClient&) const
{
    // Apotheosis (0.2.5.15): still null - the <datalist> suggestion list is the
    // remaining gap (see docs/FEATURE-GAPS-2026-10-08.md §1). Unlike <select>
    // there is no page-visible "nothing happened" breakage here: the input still
    // takes typed text, it just never offers the list. Leave it refused rather
    // than half-wired.
    return nullptr;
}

// Apotheosis (0.2.5.16): <input type=color>. Same shape as the <select> popup:
// WebCore hands us a ColorChooserClient and expects a ColorChooser back, then
// calls setSelectedColor() on it. reattachColorChooser() carries a new value
// when the element's value changes underneath us, so that is where the request
// goes out; endChooser() tears it down.
//
// Was `return nullptr`: before the 0.2.5.15 showPopup-style guards, a null return
// here was simply "no picker", and the page's own fallback (if any) took over.
// With the picker wired it is a real feature - a phone has no OS colour wheel to
// fall back to.
RefPtr<ColorChooser> PortChromeClient::createColorChooser(ColorChooserClient& client, const Color& current)
{
    return WebCorePort::queueColorChooser(client, current);
}

// Apotheosis (0.2.5.16): <input type=date/time/...>. The WebCore contract is
// DateTimeChooser::showChooser(params), so the object we return has to carry the
// parameters through to the shell; unlike the colour picker the request is
// raised by showChooser(), not at factory time (the parameters are not known then).
RefPtr<DateTimeChooser> PortChromeClient::createDateTimeChooser(DateTimeChooserClient& client)
{
    return WebCorePort::queueDateTimeChooser(client);
}

RefPtr<DataListSuggestionPicker> PortChromeClient::createDataListSuggestionPicker(DataListSuggestionsClient&)
{
    return nullptr;
}

void PortChromeClient::setTextIndicator(RefPtr<TextIndicator>&&) const
{
}

void PortChromeClient::updateTextIndicator(RefPtr<TextIndicator>&&) const
{
}

// <input type=file>. Hands the chooser to the async UI bridge and returns at once
// — the shell shows a FileOpenPicker on the UI thread and posts the result back
// onto the engine queue. Blocking here would deadlock: the engine thread must
// never wait on the UI thread (see PortUIBridge.h).
void PortChromeClient::runOpenPanel(LocalFrame&, FileChooser& chooser)
{
    WebCorePort::enqueueFileChooser(chooser);
}

// Apotheosis (0.2.5.15): <select>. Was `return nullptr`, which meant a tap on a
// dropdown opened nothing at all - and after the showPopup() null-guard landed
// (HTMLSelectElement.cpp) it stopped crashing but still did nothing. WebCore's
// contract is that this returns a PopupMenu it can then call show() on, so the
// bridge both queues the request and hands back the object (queueSelectPopup).
// The shell answers over the same async request/response path runOpenPanel uses
// (UIRequestSelect), with the same staleness rules: a reply that arrives after a
// navigation is dropped, and PopupMenuClient is weak-observing, so a dead element
// cannot be touched either way.
RefPtr<PopupMenu> PortChromeClient::createPopupMenu(PopupMenuClient& client) const
{
    uint64_t id = 0;
    return WebCorePort::queueSelectPopup(client, id);
}

// window.alert(). Queued as a shell notification; JS continues immediately rather
// than blocking, which is a deliberate deviation from the spec's modal semantics:
// making it truly modal needs a nested run loop on the engine thread, and shipping
// that untested on device is a worse trade than an alert that does not pause JS.
// See the 0.1.9 report ("JS dialogs") for the design that would make it modal.
//
// Apotheosis (2026-09-27): confirm()/prompt() DID get that treatment - but without
// the nested run loop. They park the engine thread on a condition variable while
// the shell answers on the UI thread (PortUIBridge.h), so nothing runs on the
// engine underneath the dialog and the re-entrancy risk the alert comment cites
// does not exist. alert() stays non-modal: pausing JS for a notification the user
// did not ask for is the rarer, less useful direction.
void PortChromeClient::runJavaScriptAlert(LocalFrame&, const String& message)
{
    auto utf8 = message.utf8();
    WebCorePort::enqueueAlert(utf8.data() ? std::string(utf8.data(), utf8.length()) : std::string());
}

// window.confirm(). Parks the engine thread until the shell answers (OK -> true,
// Cancel/timeout -> false). The 60 s timeout means a shell that never shows the
// dialog - suspend, torn-down window - cannot take the page (and every later
// navigation) down with it; the site sees a Cancel, which is what a walked-away
// user produces anyway.
bool PortChromeClient::runJavaScriptConfirm(LocalFrame&, const String& message)
{
    auto utf8 = message.utf8();
    return WebCorePort::enqueueConfirm(utf8.data() ? std::string(utf8.data(), utf8.length()) : std::string()) != 0;
}

// window.prompt(). Same dialog round trip; returns false (and leaves result
// untouched) when the user cancels or the timeout fires.
bool PortChromeClient::runJavaScriptPrompt(LocalFrame&, const String& message, const String& defaultValue, String& result)
{
    auto messageUtf8 = message.utf8();
    auto defaultUtf8 = defaultValue.utf8();
    std::string answer;
    const int rc = WebCorePort::enqueuePrompt(
        messageUtf8.data() ? std::string(messageUtf8.data(), messageUtf8.length()) : std::string(),
        defaultUtf8.data() ? std::string(defaultUtf8.data(), defaultUtf8.length()) : std::string(),
        answer);
    if (rc == 0)
        return false;
    result = String::fromUTF8(answer.c_str());
    return true;
}

// window.open()。设计与限制见 PortChromeClient.h 上方的长注释。
// 这里只做一件事:把请求的 URL 送进 UI bridge,让壳去开标签。
RefPtr<Page> PortChromeClient::createWindow(LocalFrame&, const String&, const WindowFeatures&, const NavigationAction& action)
{
    const URL& url = action.url();
    // about:blank 之类没有可导航目标的 open() —— 壳开一个空标签毫无意义,直接忽略。
    // (这类调用通常紧接着 w.document.write(),而那条路本来就走不通,见头文件说明。)
    if (!url.isValid() || url.isAboutBlank() || url.isEmpty())
        return nullptr;

    auto utf8 = url.string().utf8();
    if (utf8.data())
        WebCorePort::enqueueNewWindow(std::string(utf8.data(), utf8.length()));
    return nullptr;
}

void PortChromeClient::showShareSheet(ShareDataWithParsedURL&&, CompletionHandler<void(bool)>&&)
{
}


RefPtr<Icon> PortChromeClient::createIconForFiles(const Vector<String>& /* filenames */)
{
    return nullptr;
}

// Apotheosis: mirror JS console output to LocalState\console.txt via WebCoreDriver.cpp's
// consoleLogAppend() (declared above, in this namespace). sourceID (script/URL) + lineNumber is
// the "where" a console line actually needs — MessageSource (JS/Network/CSS/...) is a coarser
// category the caller can't use as well, so it is left unnamed here (matches the rest of this
// file's convention for unused ChromeClient params).
void PortChromeClient::addMessageToConsole(JSC::MessageSource, JSC::MessageLevel level, const String& message,
    unsigned lineNumber, unsigned /* columnNumber */, const String& sourceID)
{
    static constexpr std::array<const char*, 5> levelStrings { "log", "warning", "error", "debug", "info" };
    const unsigned levelIndex = static_cast<unsigned>(level);
    const char* levelStr = levelIndex < levelStrings.size() ? levelStrings[levelIndex] : "?";
    consoleLogAppend(levelStr, sourceID.utf8().data(), lineNumber, message.utf8().data());
}

} // namespace WebCorePort
