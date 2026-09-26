/*
 * PortChromeClient.cpp — out-of-line method bodies for WebCorePort::PortChromeClient.
 *
 * Bodies mirror WebCore's EmptyChromeClient (EmptyClients.cpp). The
 * popup-menu factories return nullptr (EmptyChromeClient returns a file-local
 * EmptyPopupMenu/EmptySearchPopupMenu; nullptr is a valid no-op here since the
 * headless port never shows native popups).
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

RefPtr<PopupMenu> PortChromeClient::createPopupMenu(PopupMenuClient&) const
{
    return nullptr;
}

RefPtr<SearchPopupMenu> PortChromeClient::createSearchPopupMenu(PopupMenuClient&) const
{
    return nullptr;
}

RefPtr<ColorChooser> PortChromeClient::createColorChooser(ColorChooserClient&, const Color&)
{
    return nullptr;
}

RefPtr<DataListSuggestionPicker> PortChromeClient::createDataListSuggestionPicker(DataListSuggestionsClient&)
{
    return nullptr;
}

RefPtr<DateTimeChooser> PortChromeClient::createDateTimeChooser(DateTimeChooserClient&)
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

// window.alert(). Queued as a shell notification; JS continues immediately rather
// than blocking, which is a deliberate deviation from the spec's modal semantics:
// making it truly modal needs a nested run loop on the engine thread, and shipping
// that untested on device is a worse trade than an alert that does not pause JS.
// See the 0.1.9 report ("JS dialogs") for the design that would make it modal.
void PortChromeClient::runJavaScriptAlert(LocalFrame&, const String& message)
{
    auto utf8 = message.utf8();
    WebCorePort::enqueueAlert(utf8.data() ? std::string(utf8.data(), utf8.length()) : std::string());
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
