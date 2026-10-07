// ============================================================================
// stubs-pasteboard.cpp  —  link-time stubs for the WinUWP WebCore driver
// (EdgeHTML Reborn — ARM32 thumbv7 Win10-Mobile App Container)
//
// CLASS: pasteboard
//
// WHY THESE ARE UNDEFINED
//   platform/win/PasteboardWin.cpp, platform/win/WCDataObject.cpp,
//   platform/StaticPasteboard.cpp and editing/win/EditorWin.cpp are all DROPPED
//   from PlatformWinUWP.cmake (clipboard / OLE IDataObject are unavailable under
//   WINAPI_FAMILY_APP). The platform-agnostic editing/DataTransfer code still
//   references the base-class Pasteboard virtuals + Editor::paste*/platform*Font
//   + WCDataObject::Release, so the linker reports them undefined.
//
// POLICY
//   The render (layout + paint) path never touches the system clipboard. Every
//   write/read/copy/paste here is a no-op returning empty Vector / empty String /
//   false / the inert FileContentState. Anything that *would* mutate the clipboard
//   asserts loudly so a real call surfaces instead of silently corrupting state;
//   the read/query side returns benign empties because DataTransfer can legitimately
//   probe an empty pasteboard during DOM teardown.
//
//   Signatures are transcribed verbatim from the headers so the mangled names match
//   WebCore.lib exactly:
//     Source/WebCore/platform/Pasteboard.h
//     Source/WebCore/platform/win/PasteboardWin.h (via Pasteboard.h PLATFORM(WIN))
//     Source/WebCore/platform/win/WCDataObject.h
//     Source/WebCore/editing/Editor.h
// ============================================================================

#include "config.h"

#include <wtf/Assertions.h>
#include <wtf/StdLibExtras.h>
#include <wtf/text/WTFString.h>

#include <WebCore/Pasteboard.h>        // brings WCDataObject.h, PasteboardContext.h, COMPtr.h (PLATFORM(WIN))
#include <WebCore/Editor.h>
#include <WebCore/Color.h>
#include <WebCore/SimpleRange.h>
#include <WebCore/DocumentFragment.h>
#include <WebCore/TextIterator.h>      // plainText(range)
#include <WebCore/markup.h>            // createFragmentFromText
#include <WebCore/LocalFrame.h>
#include <WebCore/LocalFrameInlines.h> // inline LocalFrame::document()/protectedDocument()
#include <WebCore/Document.h>
#include <WebCore/FrameDestructionObserverInlines.h> // inline FrameDestructionObserver::frame()/document()
#include <WebCore/DocumentPage.h>       // inline Document::page()

#include <wtf/text/WTFString.h>
#include <wtf/StdLibExtras.h>           // std::span
#include <span>

namespace WebCore {

// ----------------------------------------------------------------------------
// Apotheosis (2026-10-08): engine-side clipboard buffer.
//
// platform/win/PasteboardWin.cpp and WCDataObject.cpp are dropped from
// PlatformWinUWP.cmake (OLE IDataObject is unavailable under WINAPI_FAMILY_APP), so
// the ORIGINAL stub made every write/read a no-op and Editor::pasteWithPasteboard
// an empty function. That left the whole WebCore editing chain dead: copy, cut,
// paste, execCommand('copy') and navigator.clipboard all reached real code and then
// fell into a hole.
//
// The chain itself is fine - it only ever needs somewhere to PUT the bytes and
// somewhere to GET them back. This is that somewhere: a plain process-local U8
// buffer, engine-thread only (every entry point that reaches it is already
// serialized onto the single engine thread by the C ABI).
//
// The SYSTEM clipboard is a separate hop: UWP's DataPackage may only be touched on
// the UI thread, so the shell drains/fills this buffer through the
// WebCoreClipboardGetText/SetText C ABI (declared in WebCoreDriver.h) around its own
// DataPackage calls. The engine therefore never blocks and never waits on the UI
// thread - the port's one hard rule.
//
// Only text is carried. Images/colors/custom-data writes are still no-ops: those
// need a MIME-carrying clipboard and a picker, and text is what a phone browser's
// long-press menu actually offers. Everything that reads is honest about being
// empty rather than inventing data.
// ----------------------------------------------------------------------------
static String& apoClipboardText()
{
    static String text;
    return text;
}

// Out-of-namespace forwarders for ApotheosisClipboard::text()/setText() at the
// bottom of this file (the driver declares them in the global namespace).
String& apoClipboardTextForStub() { return apoClipboardText(); }
void apoSetClipboardTextForStub(const String& value) { apoClipboardText() = value; }

// Defined OUTSIDE namespace WebCore (at the bottom of this file) so the driver's
// global-namespace forward declaration matches: WebCoreDriver.cpp declares
// `namespace ApotheosisClipboard { const String& text(); void setText(const String&); }`
// and calls it from there. Deliberately not extern "C": it returns a WTF::String&.

// ----------------------------------------------------------------------------
// Pasteboard — base-class construction / factory
//   Mirror the PLATFORM(WIN) member-init list from PasteboardWin.cpp but skip
//   finishCreatingPasteboard() (it pulls clipboard-format registration symbols
//   that are themselves dropped). The buffer above replaces the OS clipboard.
// ----------------------------------------------------------------------------
Pasteboard::Pasteboard(std::unique_ptr<PasteboardContext>&& context)
    : m_context(WTF::move(context))
    , m_dataObject(0)
    , m_writableDataObject(0)
{
}

std::unique_ptr<Pasteboard> Pasteboard::createForCopyAndPaste(std::unique_ptr<PasteboardContext>&& context)
{
    return makeUnique<Pasteboard>(WTF::move(context));
}

// ----------------------------------------------------------------------------
// Pasteboard — read / query side (benign empties; may be probed on empty board)
// ----------------------------------------------------------------------------
bool Pasteboard::hasData()
{
    return false;
}

Vector<String> Pasteboard::typesSafeForBindings(const String&)
{
    return { };
}

Vector<String> Pasteboard::typesForLegacyUnsafeBindings()
{
    return { };
}

String Pasteboard::readOrigin()
{
    return { };
}

String Pasteboard::readString(const String&)
{
    return { };
}

String Pasteboard::readStringInCustomData(const String&)
{
    return { };
}

Pasteboard::FileContentState Pasteboard::fileContentState()
{
    return FileContentState::NoFileOrImageData;
}

bool Pasteboard::canSmartReplace()
{
    return false;
}

// ----------------------------------------------------------------------------
// Pasteboard — read side: answer from the engine-side buffer.
//   The buffer only ever carries text, so the plain-text reader is the one that
//   sees data; the web-content and file readers honestly report "nothing" (a page
//   asking for richer pasteboard content than a phone long-press provides).
// ----------------------------------------------------------------------------
void Pasteboard::read(PasteboardPlainText& reader, PlainTextURLReadingPolicy, std::optional<size_t>)
{
    // PasteboardPlainText is a plain data struct (String text), not a reader with a
    // virtual readString() - assign the buffer straight in.
    reader.text = apoClipboardText();
}

void Pasteboard::read(PasteboardWebContentReader&, WebContentReadingPolicy, std::optional<size_t>)
{
}

void Pasteboard::read(PasteboardFileReader&, std::optional<size_t>)
{
}

// ----------------------------------------------------------------------------
// Pasteboard — write side. text lands in the buffer (the shell shuttles it to UWP's
//   DataPackage); the rest keep the no-op because there is no MIME-carrying
//   clipboard behind them yet.
// ----------------------------------------------------------------------------
void Pasteboard::clear()
{
    // Apotheosis: drop the engine-side buffer so a read after a programmatic clear
    // (a page calling clipboardData.clearData()) is honest.
    apoClipboardText() = String();
}

void Pasteboard::clear(const String&)
{
    apoClipboardText() = String();
}

void Pasteboard::writeString(const String&, const String&)
{
    // No type tag in the buffer; the plain-text slot is the only one there is.
}

void Pasteboard::write(const Color&)
{
    // Apotheosis: graceful no-op until the UWP Clipboard backend is added.
}

void Pasteboard::write(const PasteboardURL&)
{
    // Apotheosis: graceful no-op until the UWP Clipboard backend is added.
}

void Pasteboard::writeTrustworthyWebURLsPboardType(const PasteboardURL&)
{
    // Apotheosis: graceful no-op until the UWP Clipboard backend is added.
}

void Pasteboard::write(const PasteboardImage&)
{
    // Apotheosis: graceful no-op until the UWP Clipboard backend is added.
}

void Pasteboard::write(const PasteboardBuffer&)
{
    // Apotheosis: graceful no-op until the UWP Clipboard backend is added.
}

void Pasteboard::write(const PasteboardWebContent&)
{
    // Apotheosis: graceful no-op until the UWP Clipboard backend is added.
}

void Pasteboard::writeCustomData(const Vector<PasteboardCustomData>&)
{
    // Apotheosis: graceful no-op until the UWP Clipboard backend is added.
}

void Pasteboard::writeMarkup(const String&)
{
    // Apotheosis: graceful no-op until the UWP Clipboard backend is added.
}

void Pasteboard::writePlainText(const String& text, SmartReplaceOption)
{
    // Apotheosis: the one write that actually carries data. Editor::performCutOrCopy
    // lands here for both copy and cut, and DOM clipboardData.setText() routes
    // through it too.
    apoClipboardText() = text;
}

// PLATFORM(WIN)-only layering-violation writers (FIXME in header).
void Pasteboard::writeImage(Element&, const URL&, const String&)
{
}

void Pasteboard::writeSelection(const std::optional<SimpleRange>& range, bool, LocalFrame&, ShouldSerializeSelectedTextForDataTransfer)
{
    // Apotheosis: serialise the selected text ourselves (PasteboardWin.cpp, which
    // would do this, is dropped) so "copy" on a real selection carries the text the
    // user actually selected instead of nothing.
    if (!range)
        return;
    apoClipboardText() = plainText(*range);
}
// ----------------------------------------------------------------------------
// Pasteboard::documentFragment — PLATFORM(WIN) layering violation. Its only
//   definition lives in the dropped platform/win/PasteboardWin.cpp (Win32
//   GetClipboardData/CF_UNICODETEXT), which is why the linker never asked for it:
//   the whole editing chain was stubbed out below, so nothing called it.
//   Now that Editor::pasteWithPasteboard drives a real paste, this has to exist.
//
//   The engine-side buffer carries text only, so this is the plain-text branch of
//   the original (fragmentFromCFHTML for HTML is unreachable - there is no
//   CF_HTML in the buffer). createFragmentFromText() is the shared markup helper,
//   already compiled into WebCore.lib.
// ----------------------------------------------------------------------------
RefPtr<DocumentFragment> Pasteboard::documentFragment(LocalFrame& frame, const SimpleRange& context, bool allowPlainText, bool& chosePlainText)
{
    chosePlainText = false;
    const String& text = apoClipboardText();
    if (!allowPlainText || text.isEmpty())
        return nullptr;
    chosePlainText = true;
    UNUSED_PARAM(frame);
    return createFragmentFromText(context, text);
}

// ----------------------------------------------------------------------------
// WCDataObject::Release — OLE IDataObject refcount; WCDataObject.cpp dropped.
//   Header declares STDMETHODCALLTYPE (collapses to the lone ARM32 calling
//   convention, so the mangled name matches). Never instantiated under WinUWP.
// ----------------------------------------------------------------------------
ULONG STDMETHODCALLTYPE WCDataObject::Release()
{
    // Apotheosis: this Win32 OLE object is never exposed by the UWP host. Do
    // not turn an unexpected teardown into a process-killing assertion.
    return 0;
}

// ----------------------------------------------------------------------------
// Editor — paste / platform font.
//
// pasteWithPasteboard: verbatim port of editing/win/EditorWin.cpp:41 (that file is
//   dropped from PlatformWinUWP.cmake, so it had to be stubbed - and the stub left
//   the whole paste path dead). The shared machinery it calls
//   (Pasteboard::documentFragment above, Editor::shouldInsertFragment,
//   Editor::pasteAsFragment, Editor::quoteFragmentForPasting,
//   Editor::canSmartReplaceWithPasteboard) is all compiled into WebCore.lib; only
//   the two platform files were missing.
//   The Win32 E_NOTIMPL / canSmartReplaceWithPasteboard specifics are gone because
//   there is no IDataObject here: our Pasteboard IS the backend, so
//   canSmartReplaceWithPasteboard reports false (Pasteboard::canSmartReplace()).
//
// platform*Font stay empty, matching EditorWin.
//
// selectedRange() is private to Editor (EditorWin.cpp calls it from inside the
// class). The public equivalent is the selection's first range, which is exactly
// what selectedRange() reduces to - so use that here from outside the class.
// ----------------------------------------------------------------------------
void Editor::pasteWithPasteboard(Pasteboard* pasteboard, OptionSet<PasteOption> options)
{
    auto range = document().selection().selection().firstRange();
    if (!range)
        return;

    bool chosePlainText;
    auto fragment = pasteboard->documentFragment(*document().frame(), *range, options.contains(PasteOption::AllowPlainText), chosePlainText);

    if (fragment && options.contains(PasteOption::AsQuotation))
        quoteFragmentForPasting(*fragment);

    if (fragment && shouldInsertFragment(*fragment, *range, EditorInsertAction::Pasted))
        pasteAsFragment(fragment.releaseNonNull(), canSmartReplaceWithPasteboard(*pasteboard), chosePlainText, options.contains(PasteOption::IgnoreMailBlockquote) ? MailBlockquoteHandling::IgnoreBlockquote : MailBlockquoteHandling::RespectBlockquote);
}

void Editor::platformCopyFont()
{
}

void Editor::platformPasteFont()
{
}

} // namespace WebCore

// ----------------------------------------------------------------------------
// C-linkage accessors for the driver's clipboard C ABI (WebCoreClipboardGetText /
// SetText). The driver's clipboard functions live inside its extern "C" block, so a
// C++ namespace declaration there does not resolve the way it looks; these are the
// safe seam - plain extern "C" with C-compatible signatures, defined here where C++
// is normal.
//
// apoClipboardRead copies up to cap bytes of UTF-8 into out and returns the byte
// length, or -1 when the buffer is empty. apoClipboardWrite replaces the buffer from
// utf8/len (len < 0 = NUL-terminated).
// ----------------------------------------------------------------------------
extern "C" int apoClipboardRead(char* out, int cap)
{
    const String& text = WebCore::apoClipboardTextForStub();
    if (text.isEmpty())
        return -1;
    auto utf8 = text.utf8();
    if (!out || cap <= 0 || static_cast<int>(utf8.length()) >= cap)
        return -2;
    std::memcpy(out, utf8.data(), utf8.length());
    out[utf8.length()] = '\0';
    return static_cast<int>(utf8.length());
}

extern "C" void apoClipboardWrite(const char* utf8, int len)
{
    if (!utf8)
        return;
    if (len < 0)
        WebCore::apoSetClipboardTextForStub(String::fromUTF8(utf8));
    else
        WebCore::apoSetClipboardTextForStub(String::fromUTF8(std::span<const char>(utf8, static_cast<size_t>(len))));
}


