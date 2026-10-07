/*
 * PortUIBridge.h — one asynchronous engine->shell UI request channel.
 *
 * WHY THIS EXISTS
 * The engine runs on a single dedicated thread and the XAML shell on the UI
 * thread, and the hard rule of this port is that neither may block waiting on the
 * other (ANGLE marshals surface work back to the panel dispatcher, so an engine
 * thread that waits on the UI thread deadlocks, and RunOnUIThread's timeout path
 * calls std::terminate). Anything WebCore wants from real UI — a file picker
 * today; <select> popups, color/date choosers, share sheets later — therefore has
 * to be a request/response pair, never a call.
 *
 * Deliberately ONE queue rather than a bespoke dispatch per feature: this is the
 * seam the next round's <select>/dialog work plugs into, and one place to get the
 * staleness rules right is worth more than five places that each get them
 * slightly wrong.
 *
 * FLOW
 *     engine thread                       UI thread
 *     -------------                       ---------
 *     ChromeClient::runOpenPanel
 *       enqueueFileChooser() -> id        (returns immediately, JS continues)
 *     ...engine call returns
 *     takeNextUIRequest() ------------->  FileOpenPicker (async, no .get())
 *                                         user picks / cancels
 *     completeFileChooser(id, paths) <--  posted back onto the engine queue
 *       validate generation + id
 *       FileChooser::chooseFiles()
 *
 * STALENESS
 * Every request carries the session generation it was created in. A reply that
 * arrives after the page was torn down, or after a new navigation, is dropped.
 * Belt and braces: WebCore's FileChooser also null-checks its client, and
 * ~FileInputType calls FileChooser::invalidate(), so even a reply that slips
 * through cannot touch a dead element.
 *
 * THREADING
 * Every function here is engine-thread only. The shell reaches them exclusively
 * through the C ABI in WebCoreDriver.h, which the harness already serializes onto
 * the single engine thread. The one exception is the confirm/prompt answer path
 * (answerConfirm/answerPrompt/takeNextUIRequestFromUIThread), documented at its
 * declaration: the engine parks inside those dialogs, so the shell answers from
 * the UI thread - the UI thread never waits on the engine.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace WebCore {
class FileChooser;
class PopupMenu;
class PopupMenuClient;
}

namespace WebCorePort {

enum UIRequestKind : int {
    UIRequestNone        = 0,
    UIRequestFileChooser = 1,
    UIRequestAlert       = 2,
    UIRequestConfirm     = 3,
    UIRequestPrompt      = 4,
    // Apotheosis (0.2.5.15): <select> was reserved and is now live.
    UIRequestSelect      = 5,
    // Reserved; keep the numbering stable, the shell switches on it:
    //   6 = color, 7 = date.
    UIRequestNewWindow   = 8,
};

// Session generation. Bumped on every session teardown/create so a reply that
// crosses a navigation can be recognised as stale and dropped.
uint64_t currentSessionGeneration();
void bumpSessionGeneration();

// Queue a file-chooser request; returns its id. Keeps a Ref on the chooser so the
// object outlives the round trip (the element behind it may not — that is what
// FileChooser::invalidate() is for).
uint64_t enqueueFileChooser(WebCore::FileChooser&);

// Apotheosis (0.2.5.15): <select>. Queue a dropdown request; returns its id. The
// Ref on PopupMenuClient is what keeps the client object alive across the round
// trip (the interface itself is AbstractRefCountedAndCanMakeWeakPtr). Payload:
// "<selectedIndex>\t<text>\t<text>...", where a row prefixed with \x01 is a
// separator / label / disabled entry and the rest is plain item text.
uint64_t enqueueSelectPopup(WebCore::PopupMenuClient&);

// The one call PortChromeClient::createPopupMenu makes: queue the request AND hand
// back the PopupMenu WebCore will call show()/hide() on (see PortSelectPopupMenu).
RefPtr<WebCore::PopupMenu> queueSelectPopup(WebCore::PopupMenuClient& client, uint64_t& outId);

// Apotheosis (0.2.5.15): the page closed its own popup, or the element went away.
// Drops the request and tells the client it is hidden. No-op for an unknown id.
void cancelSelectPopup(uint64_t id);

// Queue a message the shell should show and then forget about. No reply.
uint64_t enqueueAlert(const std::string& utf8Message);

// Queue "the page wants this URL opened in a new tab" (window.open / target=_blank
// / a form with target). No reply: the shell owns the tab model, and this port's
// tab model is one hot Page + inactive snapshots — it cannot hand back a live
// WindowProxy for a second, simultaneously-running Page. See PortChromeClient.h
// for the opener/noopener consequences of that.
uint64_t enqueueNewWindow(const std::string& utf8Url);

// Pop the oldest pending request. Returns UIRequestNone when there is nothing to
// do. `payload` is request-specific UTF-8:
//   FileChooser: "<multiple:0|1>\t<accept>\t<accept>..." where each accept is a
//                MIME type or a ".ext" extension, exactly as the page wrote it.
//   Alert:       the message text.
//   Confirm:     the message text.
//   Prompt:      "<message>\t<default value>".
// Engine-thread path. The queue is mutex-protected because the confirm/prompt
// path below takes from the UI thread while the engine is parked.
bool takeNextUIRequest(int& outKind, uint64_t& outId, std::string& outPayload);

// ---- confirm() / prompt() (Apotheosis 2026-09-27) ---------------------------
// Both ChromeClient hooks are synchronous by contract: script does not continue
// until the user answers. The engine thread therefore PARKS on a condition
// variable (it does not spin a nested run loop, so WebCore cannot be re-entered
// at an arbitrary JS point - no timers or network callbacks fire underneath the
// dialog), and the UI thread answers through answerConfirm/answerPrompt. A
// dialog the shell never answers (suspend, torn-down window) is released by the
// timeout below, which returns Cancel/empty like a user pressing cancel.
//
// THREADING EXCEPTION: these three are the only functions in this file that may
// be called from the UI thread (they touch only mutex-protected bridge state,
// never the Page). The engine-thread rule from the header comment still holds:
// the UI thread never waits on the engine; it is the engine that waits here.
//
// enqueueConfirm  returns 1 for OK, 0 for Cancel/timeout.
// enqueuePrompt   returns 1 for OK (outText = the input), 0 for Cancel/timeout
//                 (outText cleared).
int enqueueConfirm(const std::string& utf8Message);
int enqueuePrompt(const std::string& utf8Message, const std::string& utf8Default, std::string& outText);

// Deliver a dialog answer from the UI thread. Unknown/stale ids are a no-op - a
// timed-out request legitimately produces them. Returns 1 when a waiter took
// the answer, 0 otherwise.
int answerConfirm(uint64_t id, bool ok);
int answerPrompt(uint64_t id, const char* utf8Text);

// UI-thread twin of takeNextUIRequest, for the wake path: the engine is parked
// inside confirm()/prompt() and cannot run its own drain, so the shell pops the
// request from the UI thread. Same queue, same payload conventions.
bool takeNextUIRequestFromUIThread(int& outKind, uint64_t& outId, std::string& outPayload);

// Engine->shell wake for a parked dialog. The driver sets this from the C ABI
// (WebCoreSetUIRequestCallback); it is invoked on the engine thread, so the
// harness's thunk must marshal to its UI thread before touching XAML.
using UIRequestWakeCallback = void (*)(void*);
void setUIRequestWakeCallback(UIRequestWakeCallback callback, void* context);

// Deliver a file-chooser result. An empty list means the user cancelled. Silently
// ignores unknown/stale ids — a cancelled navigation legitimately produces them.
void completeFileChooser(uint64_t id, const std::vector<std::string>& utf8Paths);

// Apotheosis (0.2.5.15): deliver a <select> answer. listIndex < 0 = the user
// cancelled (or the popup was torn down); >= 0 = the row they picked, which is
// handed to PopupMenuClient::valueChanged on the engine thread. Unknown/stale ids
// are silently ignored for the same reason as the file chooser.
void completeSelectPopup(uint64_t id, int listIndex);

// Drop everything still pending (session teardown). Cancels each outstanding
// FileChooser so the page's <input type=file> does not sit "waiting" forever.
void clearPendingUIRequests();

} // namespace WebCorePort
