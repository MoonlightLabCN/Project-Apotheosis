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
 * the single engine thread.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace WebCore {
class FileChooser;
}

namespace WebCorePort {

enum UIRequestKind : int {
    UIRequestNone        = 0,
    UIRequestFileChooser = 1,
    UIRequestAlert       = 2,
    // Reserved; keep the numbering stable, the shell switches on it:
    //   3 = confirm, 4 = prompt, 5 = select popup, 6 = color, 7 = date.
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
bool takeNextUIRequest(int& outKind, uint64_t& outId, std::string& outPayload);

// Deliver a file-chooser result. An empty list means the user cancelled. Silently
// ignores unknown/stale ids — a cancelled navigation legitimately produces them.
void completeFileChooser(uint64_t id, const std::vector<std::string>& utf8Paths);

// Drop everything still pending (session teardown). Cancels each outstanding
// FileChooser so the page's <input type=file> does not sit "waiting" forever.
void clearPendingUIRequests();

} // namespace WebCorePort
