# Win10 Mobile media implementation notes

Branch: `media-mf`

> **Where the engine-side code lives.** `WebKit/` is out of tree (gitignored), so none of the
> WebCore-side work below is visible in a checkout of this repo. The whole media delta — the
> `MediaPlayerPrivateWinUWP` player, `WinUWPMediaBridge.h`, the `PAL` clock implementation, the
> `MediaPlayer.cpp` registration and the three CMake edits — is captured as a feature patch:
> **`patches/2026-10-09-winuwp-media-mf.patch`** (7 files; reverse-apply checked). Treat that file,
> not this document, as the source of truth for what to apply to a pristine webkitgtk-2.52.4.
> The repo-root `wk-winuwp.patch` is the much larger whole-port master patch and was deliberately
> left alone: the `WebKit` checkout currently has unmerged (`UU`) entries, so regenerating a master
> patch unattended would not be trustworthy.

## Status (2026-10-09)

**Both halves build.** What exists now:

| Piece | File | State |
|---|---|---|
| WinUWP MediaPlayerPrivate (MF SourceReader decode) | `WebKit/Source/WebCore/platform/graphics/win/MediaPlayerPrivateWinUWP.{h,cpp}` | compiles, in `WebCore.lib` |
| WebCore↔port media contract | `WebKit/.../win/WinUWPMediaBridge.h` | copied to `PrivateHeaders/WebCore/`, included by port/ |
| engine registration | `MediaPlayer.cpp` (WK_WINUWP guarded, 2 spots) | compiles |
| PAL Clock implementation | `WebKit/Source/WebCore/PAL/pal/PlatformWinUWP.cmake` (new) | compiles, in `PAL.lib` |
| audio sink over the PCM ring | `port/PortMediaAudio.{h,cpp}` | compiles, in `WebCoreDriver-gpu.lib` |
| C ABI for the harness | `WebCoreMediaAudioTake/SampleRate/Channels/Diagnostics` in both `WebCoreDriver.h` copies | compiles |
| sink install + stage.txt line | `port/WebCoreDriver.cpp` | compiles |
| harness audio device (AudioGraph) | `harness/MediaAudio.{h,cpp}` + hooks in `MainPage.xaml.cpp` | compiles |
| appx | `harness/AppPackages/Harness/Harness_0.2.5.19_ARM_Test/` | built, **installed on the phone** |

`port/link-driver-gpu.ps1` reports `EXIT=0 undefined=0 duplicate=0` and `build-harness.ps1`
reports `MSBuild exit code: 0`.

0.2.5.19 has been on the device and exercised: the media element is now real and the engine fetches
the media (see "Device round 1" below). Everything *before* that section is a build claim; that
section is measurement.

### C++/CX restrictions that bit the harness half

* A `public ref class` may not have a `= default` (or `= delete`d) member: `error C2684`.
  Write `MediaAudio() { }`.
* Public members of a `public ref class` become part of the WinRT surface, so no raw pointer
  parameters (a `GetStats(unsigned long long*, ...)` helper had to go).
* `AudioFrame`, `AudioBuffer` and `AudioBufferAccessMode` are in **`Windows::Media`**, not
  `Windows::Media::Audio` — only the graph, its nodes and the node event args are in the latter.
* `IMemoryBufferReference` has no `Close()` in this projection (it exposes a `Closed` event
  instead). Releasing the handle is what ends the lock: drop the byte access, then the
  reference, then the `AudioBuffer`, and only then `AddFrame()`.

`MainPage.xaml.cpp` includes `MediaAudio.h`, so any edit to that header recompiles the 514 KB
file. Iterate on the .cpp with MSBuild's file-scoped target instead:

```powershell
MSBuild.exe harness\Harness.vcxproj /p:Configuration=Release /p:Platform=ARM `
  /p:ApotheosisWindowsSdkVersion=10.0.22621.0 /p:VCToolsVersion=14.44.35207 `
  /p:ApotheosisVcpkgRoot=C:\vcpkg /p:ApotheosisIcuRoot=C:\icu-arm-uwp `
  /t:ClCompile /p:SelectedFiles=MediaAudio.cpp
```

(And see the `MSB6001`/`NO_PROXY` row in `docs/HARNESS-BUILD.md`: MSBuild will not start at all
if the environment holds a variable twice under different casings.)


## Device round 1 (2026-10-09): the fix landed, and the crash that replaced the silence

0.2.5.19 (the `setMediaEnabled(true)` build) was installed and the same probe page was run. Two
results, one good and one blocking.

### Good: the element is real and the media path engages

The LAN server log is the ground truth here (the page is served from it, and Media Foundation's
SourceReader does its own HTTP):

```
23:31:59.538  192.168.3.159  GET /probe.html  -> 200
23:32:00.410  192.168.3.159  GET /test.mp4    -> 206 Partial Content (range=bytes=0-             bytes=0+788493/788493)
23:32:00.488  192.168.3.159  GET /test.mp4    -> 206 Partial Content (range=bytes=770048-788492  bytes=770048+18445/788493)
```

The second request is the progressive-MP4 signature: read the tail to get the `moov` atom. Nothing
like this ever happened before the fix — the element was an `HTMLUnknownElement` and never asked for
a single byte. **The element is now a real media element and the engine engaged.**

### Blocking: the player crashes on teardown

`LocalState\crash.txt` grew by 52 lines, at exactly that moment:

```
==== crash 2026-10-09 23:31:59.236 tid=5208 ====
reason: WTF WTF RELEASE_ASSERT at .../wtf/RefCounted.h:53 in WTF::RefCountedBase::~RefCountedBase()
```

Line 53 is `RELEASE_ASSERT(m_refCount == 1)`. Symbolicated with
`llvm-symbolizer --obj=Harness.exe --relative-address` (the PDB beside the exe is the exact crashing
build — this works and is worth remembering):

```
WebCore::MediaPlayerPrivateWinUWP::~MediaPlayerPrivateWinUWP()
  WebCore::MediaPlayerPrivateWinUWP::stopDecodeThread()
    WTF::ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr<WTF::Thread, 0>::deref() const
      WTF::RefCountedBase::~RefCountedBase()   <-- assert
```

and `WebCore::HTMLMediaElement::load()` also appears on that stack — so a (re)load was tearing the old
player down while its decode thread was still finishing.

Mechanism: `Thread::create` returns `Ref<Thread>` *and* moves a ref into thread-local storage, which
the thread releases as it exits (`WTF/wtf/Threading.cpp:269`, and the comment at `Threading.h:360-366`).
My `notify*`/`postFailure` helpers take a temporary
`RefPtr<MediaPlayerPrivateWinUWP> protectedThis { *this }` — on the **decode thread** — and post tasks
carrying a `RefPtr` back to the main thread, while `~MediaPlayerPrivateWinUWP()` only joins the thread
*after* the last deref has already begun destruction. That leaves a race: the main thread drops the
count to 0 and deletes, while the decode thread has just taken a reference to the same object. The
second delete runs `~RefCountedBase()` with `m_refCount == 0` (it is 1 on the healthy path) — and it
runs *on the decode thread*, exactly as the stack shows.

### The fix to make (deliberately not rushed)

The invariant needed is "**the decode thread never manipulates this object's refcount**". Note what
does *not* work, because the obvious ideas each have a hole:

* *Main-thread keep-alive* (`m_threadKeepAlive = *this` before `Thread::create`, released after the
  join): it does stop the race, but if the external owner drops its ref first the object stays alive
  with nobody left to release the keep-alive → **leak**, and the destructor (which is what joins the
  thread) may never run.
* *Decode thread holds a long-lived self-ref*: the count then only reaches 0 on the decode thread, so
  the destruction happens there and `stopDecodeThread()` would **join the thread from inside itself**.

The correct shape is a **weak** reference for everything the decode thread posts:

1. Make the class use `ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr<MediaPlayerPrivateWinUWP>`
   instead of plain `RefCounted<...>` (this is the same base `WTF::Thread` itself uses, and it is what
   provides a control block that survives the object).
2. `notify*`/`postFailure` post lambdas that capture a **`ThreadSafeWeakPtr<MediaPlayerPrivateWinUWP>`**,
   and the lambda takes a strong ref on the main thread (`if (RefPtr player = weakPtr.get()) ...`) and
   no-ops if the object is already gone. Taking a weak→strong ref is safe even after the object died,
   which is exactly the property needed; a plain `RefPtr` capture on the decode thread is not.
3. The destructor keeps joining the decode thread, and the join is now safe: while it runs, the thread
   can no longer ref/deref the object, it only touches state members — which the join already
   guarantees are not yet destroyed (the destructor body runs before member destruction).

The earlier session note in this document said `CanMakeThreadSafeWeakPtr` does not exist; the base to
use in this WTF version is `ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr` (visible in the crash
symbol itself), so the weak design is available.

### Also learned: console.txt is buffered, not broken

The probe pages' self-reports (`PROBE ...`, `MEDIADIAG ...`) that were *missing* during the earlier
round showed up in `console.txt` hours later, at 23:3x. Console mirroring works; it **flushes late**.
The earlier claim in this document that it "does not flush for these pages" was wrong — pulling too
early is what made it look empty. `stage.txt` is buffered the same way, which is why its `media ...`
line had not landed when the round was read.

### Correction: the `media` line is NOT gated on a load event

An earlier revision of this section warned that the `media` row might never be written, because
`HTMLMediaElement::selectMediaResource()` delays the load event and the row is only appended when a
navigation settles. **Measurement says otherwise.** The row is written on every media navigation; what
settle looks like for a page whose only content is a `<video>` is `load=- why=quiet-vp` (the settle
logic has a video-pending path). So a missing `media` line means a missing navigation, not a
suppressed diagnostic — and `load=-` is expected here, not a fault signal.

## Device round 2 (2026-10-10): the play gate, and what is still missing

0.2.5.21 fixed the thing that made `<video>` inert even with a real user tap, and 0.2.5.22 added the
instrumentation for what is left.

### What was wrong: the port gave every page a null PageIdentifier

`port/WebCoreDriver.cpp` created all four of its pages with
`pageConfigurationWithEmptyClients(std::nullopt, ...)`. With no page identifier, WebKit refuses to
start playback, and the refusal is silent from the page's point of view:

```cpp
RefPtr<MediaSessionManagerInterface> Page::mediaSessionManager()
{
    if (!m_identifier)
        return nullptr;                        // <- what we were hitting
    ...
}

void PlatformMediaSession::clientWillBeginPlayback(CompletionHandler<void(bool)>&& completionHandler)
{
    ...
    RefPtr manager = sessionManager();
    if (!manager) {
        completionHandler(false);              // <- "returning because of interruption"
        return;
    }
```

`HTMLMediaElement::playInternal()` then never reaches `completePlayInternal()`: `paused` stays true,
the `play()` promise never settles, `MediaPlayer::play()` is never called, and no backend is ever
asked to decode. On device that read as `readyState` pinned at HAVE_METADATA, `frames=0/0`, `pcm=0`
— identical to "the media engine does not work", which is what made it expensive to find.

Fixed by passing `PageIdentifier::generate()` at all four sites. **Verified on device**: the same
page that reported `paused=1` forever now reports `paused=0` from the moment `play()` is called.

### What is still missing: no samples are produced

With playback permitted, the element plays (paused=0) and still gets **no frames**: `rs` stays 1,
`frames=0/0`, `pcm=0/0/0`, while the engine side looks healthy —
`readers=1 native=0x34363248(H264) out=0x16(RGB32) dxva=on err=0x00000000`.

Ruled out so far:

* **Not the autoplay policy.** The device log contains real `gesture-click` reports (the user tapped
  the page) and the old build still would not play.
* **Not a moov-at-the-end seek problem.** A `+faststart` remux (`ffmpeg -c copy -movflags
  +faststart`) behaves identically, and it only ever issues a single `bytes=0-` request.
* **Not the network.** After the initial fetch Media Foundation issues no further requests at all,
  and while the page sits there "playing" the phone holds **no established connection** to the test
  server (only TIME_WAIT sockets). So the synchronous `ReadSample` is not waiting on I/O.

0.2.5.22 therefore extends the `media` line with ReadSample instrumentation, which is what the next
device round reads:

```
reads=<N> lasthr=0x<hr> flags=0x<f> sidx=<i> vidx=<v> aidx=<a> drop=<N>
```

* `reads` frozen → the synchronous read never returns (stuck inside the decode transform, not I/O).
* `flags` with only STREAMTICK → timestamps arrive, content never does.
* `drop` rising while `frames` stays 0 → real samples arrive whose stream index matches neither
  configured stream. That path exists and used to drop them silently (`decodeOneSample`'s final
  `return true`), which is exactly the kind of failure that looks like "no data".

### Known second issue: playback is interrupted ~7 s in

`fast.html` ended with the play promise rejected as `AbortError — The operation was aborted.` and
`paused` back to 1. That is a media-session interruption, and it is a separate problem from the
missing frames: it only became reachable once playback could start at all (i.e. after the
PageIdentifier fix). Deal with it after frames exist.

### Verified: the teardown crash fix holds

`crash.txt` still ends at `==== crash 2026-10-09 23:38:16` — i.e. **zero crash entries at 01:xx**
across 0.2.5.20 and 0.2.5.21, both of which went through several teardowns (deployment stopping the
old instance, plus this session's terminate-and-relaunch test cycles) and user taps. The file grew
only with `tap at=` interaction records. The pre-fix build crashed on the same actions twice.

### Device round 3: the audio path was freezing the video path

With playback finally permitted the element played (`paused=0`) and still produced no frames. The
ReadSample instrumentation answered it in one line:

```
reads=1 lasthr=0x00000000 flags=0x0 sidx=0 vidx=2 aidx=0 drop=0
cap=48000 qpush=0 qcons=0 qdrop=0 und=0 phase=6
```

`reads=1` means the decode thread read **exactly one** sample and never came back to the loop. That
sample was audio (`sidx=0` matches `aidx=0`), and `phase=6` - recorded immediately before the audio
push loop - together with `qpush=0` means `pushPCM` never returned. So the video stream was **never
read at all**: `frames=0` had nothing to do with the video pipeline.

The amplifier was our own design, and the comment said it out loud:

```cpp
// The sink is bounded; ... a full queue is treated as back-pressure and the decoder waits
// for the device instead.
while (framesPushed < frames) {
    ...
    if (queued <= 0) { sleepForSeconds(0.005); continue; }   // waits forever
```

`PortAudioOutput.h` documents the opposite contract - "pushPCM() never waits for the audio callback"
- so the queue honoured it and the *caller* broke it. One stuck audio consumer therefore froze the
entire media pipeline, video included. Worth remembering as a class of bug, not just as this bug:
**an unbounded wait on a shared resource inside the decode thread converts a local problem into a
total media stall.**

Fixed in three places:

1. `PortAudioOutput::pushPCM` uses `try_lock` and returns 0 when the queue mutex is busy (counting
   `contendedPushes`), so it can never block the decode thread.
2. The media-side push loop is bounded (250 ms), after which it drops the rest of the sample, counts
   it in `g_pcmFramesDropped`, marks `phase=8` and returns - a dropped audio block is a far better
   failure than a frozen picture.
3. Audio that is consumed still needs a consumer: the harness only starts its AudioGraph from a
   present callback (`OnPresentWake` -> `MediaAudio::EnsureStarted`), and a stalled media page
   produces no presents of its own (`und=0`, `qcons=0`). The driver now calls `presentRequested()`
   when a format is configured, so the graph gets built for it.

The media line gained `cap / qpush / qcons / qdrop / und / cpush / phase` for exactly this hunt. Phase
values: 1 entered, 2 sink obtained, 3 ConvertToContiguousBuffer, 4 buffer Lock, 5 formats read,
6 entering push loop, 7 push returned, 8 gave up on the sample.

### Device round 4: the page was never put in the window, and the fix that broke the app

Chasing the `AbortError` play promise upward through the upstream source produced a complete chain:

| Step | Where | Fact |
|---|---|---|
| 1 | `HTMLMediaElement.cpp:9834` | `updateShouldPlay()` consults `playbackStateChangePermitted(Playing)` and calls `pauseInternal()` when denied |
| 2 | `HTMLMediaElement.cpp:4685,4725` | `pauseInternal()` sets `paused=true` and **rejects pending play promises with AbortError** - the exact rejection seen on device |
| 3 | then `updatePlayState()` -> `MediaPlayer::pause()` | the backend's `m_paused` becomes true, the decode thread parks: `reads=1`, no frames, readyState stuck at HAVE_METADATA |
| 4 | `MediaElementSession.cpp:457` | one denial reason is `element->isSuspended()` |
| 5 | `HTMLMediaElement.cpp:9252` | `isSuspended()` is `document().activeDOMObjectsAreSuspended() \|\| ...Stopped()` |
| 6 | `ScriptExecutionContext.cpp:424` via `Document::suspendScheduledTasks` via `LocalFrame::suspendActiveDOMObjectsAndAnimations()` | that is a **counter**; while it is > 0 the document's ActiveDOMObjects stay suspended |
| 7 | `Page.cpp:3410` | the counter is driven by `ActivityState::IsInWindow` (`setIsInWindowInternal(false)`) |
| 8 | the port | only ever called `page->setIsVisible(true)`, which touches the IsVisible bits only - `IsInWindow` was never set |

So the page was never "in the window", the document stayed suspended, and WebKit itself refused
playback. `phase=6` was stale from the preroll; nothing was deadlocked.

**The first fix attempt (0.2.5.26) crashed the app instead**, and the device evidence said so without
needing a symbolized stack: during 0.2.5.26 the test server logged **no request from the device at
all** (only the build machine's own `127.0.0.1` probes), and `crash.txt` gained entries at
`2026-10-13 00:54:10` whose stacks are all XAML/UI-thread frames
(`Windows.UI.Xaml` / `twinapi.appcore` / `minuser`). Absence of a device request was the faster and
more reliable signal: the app was not running at all, not merely failing to play media.

The cause is documented in this port's own source, one screen below the call site:

```cpp
// ⚠ 必须在 setView()+init() 之后调:setActiveInternal 的 selection().pageActivationChanged()
//   不判空,若帧还没 document/view 会解引用 null+0x858 崩溃(0.1.0.9 真机崩因,RVA 0x1E9309)。
page->focusController().setActive(true);
page->focusController().setFocused(true);
```

`setActivityState` hands the whole state to `focusController().setActivityState()`, so adding
`WindowIsActive`/`IsFocused` and calling it **before** `setView()`/`init()` walked straight into the
0.1.0.9 crash that comment exists to prevent.

0.2.5.27 therefore does the minimum, in the safe place:

* add **only** `ActivityState::IsInWindow` (`Page::setIsInWindow(true)`) - it is the bit that gates the
  suspension and it does not touch the focus path;
* call it **after** `setView()` + `init()` (next to the existing focus calls in `buildSession`, and
  after the view is resized in `WebCoreLoadUrl`);
* keep the `media-state vis/visocc/inwin/focused/winactive/idle/fsusp/dsusp` diagnostic line, which
  reports the frame/document suspension directly and will confirm or refute the hypothesis in one
  line on the next device round.

Lesson worth keeping: **a UI-thread crash whose stack symbolizes to nothing useful can still be
localized from the server side** - "the device never even asked for the page" is proof the app died
before navigation, and it costs one grep of the test server log.

### Device round 5: the element and the backend were deadlocked over readyState

0.2.5.27 removed the crash and let the page load, but the picture still never came: the element
reported `paused=0`, `rs=1`, `ct=0.000`, the page kept firing `stalled`, and (from the earlier
instrumented rounds) `reads=1`. Reading the element's own play path explains all of it, and the
explanation is a deadlock between the element and the backend rather than a bug in either one:

```cpp
// HTMLMediaElement::updatePlayState()
bool shouldBePlaying = potentiallyPlaying();
...
if (shouldBePlaying) { ... playPlayer(); }        // the only path that reaches MediaPlayer::play()

// HTMLMediaElement::potentiallyPlaying()
if (!couldPlayIfEnoughData()) return false;
if (m_readyState >= HAVE_FUTURE_DATA) return true;                       // needs readyState >= 3
return m_readyStateMaximum >= HAVE_FUTURE_DATA && m_readyState < HAVE_FUTURE_DATA;
```

`readyState` is *whatever the backend reports*. This backend only decoded while it was asked to
play, and while paused it decoded exactly one poster frame and then parked - so it never reported
more than `HaveCurrentData` (2), `potentiallyPlaying()` stayed false, `playPlayer()` was never
called, `m_paused` stayed true in the backend, the decode thread stayed parked, and therefore no
data was ever produced to raise `readyState`. Each side was waiting for the other.

Every real backend breaks this the same way: **a paused player still buffers**, and reports
`readyState` progression as it does. So 0.2.5.29 makes the backend do that:

* `DecoderState::bufferedEnd` tracks how far ahead the reader has read;
* the paused branch now keeps decoding until it has two seconds of read-ahead (or EOS), then reports
  `HaveFutureData` (`HaveEnoughData` at EOS) on the main thread and only then parks;
* the audio push loop no longer breaks on `m_paused` - a paused player still fills its ring, and
  refusing to do so was a second reason `readyState` could not advance.

This is a separate gate from the `IsInWindow` one in round 4, and both are needed: `IsInWindow` lets
WebKit *permit* playback (otherwise the play() promise is rejected outright), and buffering while
paused is what makes the element actually *ask* the backend to play.

### Also fixed on the way: the app never held the screen awake

Every device round kept dying mid-test because the phone's screen turned off after a few seconds,
and when it does the system freezes the foreground process (and WDP drops). That is not only a test
problem: **a browser that plays video has to hold a display request**, and this one never did, so
playback would stop the moment the screen went off. 0.2.5.28 added `MainPage::HoldDisplayAwake()` -
a `DisplayRequest` re-armed on every navigation with a 10-minute cap, so it does not sit there
draining the battery when nothing is being watched.

## Root cause of "video renders as an empty control and .play() does nothing"

Found on device, 2026-10-09, and it is **not** in WebKit: the port turned media off itself.

`port/WebCoreDriver.cpp` created its pages with

```cpp
page->settings().setMediaEnabled(false);   // three sites, under #if ENABLE(VIDEO)
```

With `mediaEnabled()` false, the generated `HTMLElementFactory` returns an
`HTMLUnknownElement` for `<video>`/`<audio>`/`<track>`:

```cpp
    if (!document.settings().mediaEnabled())
        return HTMLUnknownElement::create(tagName, document);
    return HTMLVideoElement::create(tagName, document, createdByParser);
```

So the element was never an `HTMLMediaElement`: no `MediaPlayer` was ever created, the source was
never fetched (the server log stayed empty for the `.mp4`), `currentTime`/`readyState`/`load` did
not exist, and `play()` was a no-op on a plain element — which is exactly the historical symptom
"the tag renders as an empty control and `.play()` does not even throw". `ENABLE_VIDEO=1` in the
engine was necessary but nowhere near sufficient; the port also had to *ask* for media.

Fixed: `setMediaEnabled(true)` in `buildSession()` (the path real browsing takes:
`WebCoreSessionLoad` → `buildSession`) and in `WebCoreLoadUrl()`. Deliberately left false in
`WebCoreRenderHtml()`, the one-shot no-script snapshot renderer the shell uses for its own
home/error pages.

### How it was found (worth reusing)

The device's `console.txt` mirroring appeared to produce **nothing** for the probe pages while their
timers demonstrably ran (it was in fact only flushing late — see "console.txt is buffered, not
broken"), so the instrument was rebuilt rather than trusted: the page reports by requesting a
uniquely named resource from a LAN HTTP server, and the **server's request log is the ground truth**
(see `tools/Test-MediaServer.ps1`, which logs the full request target). That turned "no output" into a
complete self-report, delivered immediately:

```
video=HTMLUnknownElement  audio=HTMLUnknownElement  track=HTMLUnknownElement
source=HTMLSourceElement  canvas=HTMLCanvasElement  div=HTMLDivElement
```

`source` is the discriminator: in the generated factory it has **no** `mediaEnabled()` check, only
`#if ENABLE(VIDEO)`. So `source` → `HTMLSourceElement` proved the media cases were compiled in,
and `video` → `HTMLUnknownElement` proved the failure was the runtime setting. A stale-object
theory was ruled out first by timestamps (`HTMLElementFactory.cpp` is dated 2026-09-26, but
`UnifiedSource-root-1.cpp.obj` and `Settings.cpp.obj` were both rebuilt on 10-09 after the
`cmakeconfig.h` flip).

### Two device facts that cost time

* **`data:` URLs cannot be navigated top-frame.** WebKit refuses: `Not allowed to navigate top
  frame to data URL`, and the load dies on the watchdog with `firstbyte=-`. Serve test pages over
  HTTP instead.
* **`http://<lan-ip>:port` works fine** from the device (no HTTPS/cert needed for this), which also
  removes internet reachability as a variable when testing `<video>`.

### Why Bilibili still will not play

Its player is MSE/DASH: it needs `MediaSource`, and `window.MediaSource` is `undefined` here
(`ENABLE_MEDIA_SOURCE=0`). Observed: the page loads cleanly (fp≈2.3 s, 30/30 subresources) and the
media engine is never asked for anything (`readers=0`). That is the deferred second round, not a
regression.

## Key finding: there is no compile wall


`ENABLE_VIDEO=1` with `USE_MEDIA_FOUNDATION=0` builds the **entire** WebCore media stack
(MediaPlayer/HTMLMediaElement/MediaControlsHost/MediaElementSession/PlatformMediaSessionManager)
cleanly on ARM32 / App Container: `ninja -j4 WebCore` → **1006/1006, 0 errors**.

The handoff predicted a wall here. The reason there isn't one: WebKit compiles every file
listed in `Sources.txt` unconditionally and each media file's body is `#if ENABLE(VIDEO)`-guarded,
so with the flag off they were being compiled as empty translation units all along. Turning the
flag on only *activates* code that was already being parsed. The 42 unified bundles that carry
media code all built first try.

So the real work is not "make it compile" — it is the player implementation (done) and the
device behaviour (unknown).

## Windows-specific API facts discovered the hard way

These are version facts about *this* WebKit tree, each of which cost a compile iteration:

| Instead of | Use | Why |
|---|---|---|
| `CanMakeThreadSafeWeakPtr<T>` | nothing — capture a `RefPtr` into the posted task | the base does not exist here; the type is `ThreadSafeRefCountedAndCanMakeThreadSafeWeakPtr<T, DestructionThread>` |
| `WTF_GUARDED_BY(x)` | `WTF_GUARDED_BY_LOCK(x)` | only the `_LOCK`/`_CAPABILITY` forms exist |
| `vector.data()` | `vector.mutableSpan().data()` | `Vector::data()` is **private** in this tree |
| `promise->resolve(v)` | `producer.resolve(v)` | `NativePromise::resolve/settle` require a `LogSiteIdentifier`; `NativePromise::Producer`'s versions default it |
| `settle(makeUnexpected(e))` | `producer.reject(e)` | the `unexpected` path instantiates `CrossThreadCopier<unexpected<...>>`, which has no `isolatedCopy()` |
| `MediaTimePromise::create()` | `MediaTimePromise::Producer p; p.promise()` | see `VideoDecoder.cpp:63` for the in-tree idiom |
| `Thread::sleep(s)` | `WTF::sleep(Seconds)` | free function in `wtf/Seconds.cpp` |
| `SetCurrentPosition(guid, &propvariant)` | `SetCurrentPosition(guid, propvariant)` | `REFPROPVARIANT` is a reference in C++ |
| allocating a plain struct with `makeUnique` | `makeUniqueWithoutFastMallocCheck` | `makeUnique` static_asserts the type is TZone-allocated |

Also: `std::optional<NativePromiseProducer<...>>` supports move-**construction** but not
assignment under MSVC (the optional's assignment operators are deleted); use `emplace`.

### Two link-level holes that only appear once ENABLE_VIDEO is on

1. **Media Foundation's GUIDs are data, not functions.** `MF_MT_*`, `MFMediaType_*`,
   `MFVideoFormat_*`, `MFAudioFormat_*` are `EXTERN_GUID`, so their storage lives in
   `mfuuid.lib`. Adding `mfuuid` to `WebCore_LIBRARIES` in `PlatformWinUWP.cmake` does
   nothing: WebCore is a *static* archive and a static library's library list is inert.
   It has to be in the lists of the things that actually link — `port/link-driver-gpu.ps1`
   and `harness/Harness.vcxproj`'s `AdditionalDependencies`. (The MF *entry points* came
   from `WindowsApp.lib`, which the driver link already had.)
2. **PAL had no Clock implementation for this port.** PAL was being built from its
   cross-platform `Sources.txt` alone, which has no `Clock::create()`; nothing noticed
   while `ENABLE_VIDEO` was off. Turning video on pulls in `MediaTime`/`PlatformTimeRanges`
   users that call it, and the driver link failed with
   `undefined symbol: PAL::Clock::create(void)`. Fixed by adding
   `WebKit/Source/WebCore/PAL/pal/PlatformWinUWP.cmake` listing `system/ClockGeneric.cpp`
   (the same portable std::chrono file PlatformWin.cmake and the Cocoa port use).

Note the workflow trap: creating a *new* `.cmake` file does **not** make ninja re-run CMake
(its dependency list was generated when the file did not exist). Run
`pwsh -File port/configure-gpu.ps1` explicitly, and confirm the configure output says
`Using platform-specific CMakeLists: .../PlatformWinUWP.cmake` rather than `not found`.

### Full-rebuild trap

Any CMake re-run invalidates the WebCore PCH and makes the next `ninja WebCore` a full
1007-step / 60-80 minute rebuild even when no WebCore source changed. That churn is
harmless to skip when the change was outside WebCore (PAL, a link list): the previously
built `WebCore.lib` still matches its sources.

**There is a second, unrelated cause, and it is not fixable with `ninja -t restat`.** After an
unrelated full build, `ninja WebCore` asked for 1008 steps *even though* the only edit was one
`.cpp`. `ninja -d explain` named the reason for every object:

```
ninja explain: stored deps info out of date for '...SystemHeap.cpp.obj' (8120639114352540 vs 8131907669142920)
```

That is the dependency log disagreeing with the filesystem, not a content change — every header
involved was still dated 2026-09-26 while the objects were from the later build, so nothing had
actually changed and the objects were perfectly valid. `ninja -t restat` (which reconciles the
*build* log) did **not** clear it; the count went from 1008 to 1049. Hand-archiving a replacement
`lib\WebCore.lib` is not a way out either: the archive step runs `llvm-lib @CMakeFiles\WebCore.rsp`
and **that response file does not exist** on disk, so the archive can only be produced by the
build itself. Budget the hour; do not spend it trying to dodge this.

## Workflow: full rebuilds are the norm, so probe single files

Every `ninja WebCore` run on this tree is ~1007 steps / 60-80 minutes (PCH regeneration, plus the
dependency-log problem above). Do not iterate through it.

`ninja -t commands <object>` prints the fully-expanded compile command for any object, and
`port/probe-media-compile.bat` (scratch, alongside `probe-bundle-compile.bat`) is one such line
with the input/output swapped to a scratch object. Recompile a single file in ~40 seconds:

```powershell
. E:\Apotheosis\port\arm32-uwp-env.ps1       # the PCH is MSVC-version-keyed; without this it mismatches
cd E:\Apotheosis\build-clang-gpu
cmd /c E:\Apotheosis\port\probe-media-compile.bat
```

Only run the full ninja once the probes are clean.

## Device round runbook (the fix is built, not yet installed)

State: **0.2.5.19 is built and waiting** at
`harness\AppPackages\Harness\Harness_0.2.5.19_ARM_Test\Harness_0.2.5.19_ARM.appx`. It contains the
`setMediaEnabled(true)` fix. 0.2.5.18 (the pre-fix build) is what the phone still has.

```powershell
# 1. (re)start the LAN test server; put any small H.264/AAC mp4 at _mediatest\test.mp4
pwsh -File E:\Apotheosis\tools\Test-MediaServer.ps1 -Port 8099 -Root E:\Apotheosis\port\_mediatest
# 2. install; -NoProxy matters, and this machine's env has duplicate *_PROXY spellings
pwsh -File E:\Apotheosis\tools\install-only.ps1 -Ip 192.168.3.159 -Ver 0.2.5.19
# 3. point the app at the self-reporting page and restart it (testurl.txt is read at startup)
#    POST file testurl.txt = http://192.168.3.108:8099/probe.html   (see the WDP calls below)
#    then DELETE + POST /api/taskmanager/app
# 4. read the evidence: the server request log, and LocalState\stage.txt
```

What to expect once the fix is live:

| Evidence | Meaning |
|---|---|
| device self-report `v.ctor=HTMLVideoElement`, `v.instHVE=1`, `v.ty-currentTime=number` | the element is a real media element again (this is the fix landing) |
| server log shows `GET /test.mp4` with a `Range:` header | WebKit's loader and/or MF's own SourceReader fetched the media |
| `stage.txt` `media readers=1 … out=0x16` | `MFCreateSourceReaderFromURL` ran and the Video Processor handed back RGB32 |
| `frames=<n>/<m>` with m growing | frames decoded and presented |
| `pcm=<pushed>/<consumed>/<dropped> q=…` | the harness AudioGraph is draining the PCM ring |

If `readers=1` but no `test.mp4` request appears, MF's own HTTP fetch is the thing that failed
(it does not use WebKit's loader) — check `err=`/`at=` in the media line. If the Video Processor
cannot engage in the App Container, set `MF_SOURCE_READER_DISABLE_DXVA = TRUE` in
`openSourceReader()` (one line, already commented in place) to force software decode.

### Deployment lessons (cost most of the time in this round)

* **A same-version reinstall is a silent no-op.** `install-only.ps1` skips when the PFN is already
  installed, and even a direct `POST /api/app/packagemanager/package` answers **202 "accepted"**
  while the phone keeps running the old binaries. Bump `Package.appxmanifest` (0.2.5.18 → 0.2.5.19
  here) and reinstall; verify by talking to the running app, not by the deploy status.
* **WDP dies when the phone's screen sleeps.** The network stays up (the device answers ARP) while
  TCP 443 stops listening, so it looks like a network drop but is not; a deploy of 54 MB to a locked
  phone will strand exactly like this.
* `tools/Deploy-WhenUp.ps1` cannot be used as-is: it hardcodes `192.168.3.51` and ignores its `-Ip`,
  it calls `Deploy-Robust.ps1`, and it does not disable the proxy.

## Still open after that

1. **Picture + sound on device** (above). Unknown: whether `MFStartup`,
   `MFCreateSourceReaderFromURL` and the Video Processor MFT are all permitted inside the App
   Container, and whether AudioGraph can be created on 15254.
2. **A/V sync is wall-clock, not audio-slaved.** `PortAudioOutput::consumedFrames()` is plumbed
   through the sink and unused; the media clock is `play()`/`seek()`-anchored wall time. Once audio
   is audible, re-anchor `clockNowLocked()` to the device's consumed frames.
3. **No controls.** `modern-media-controls` is still dropped; the plan is a harness XAML overlay
   (the long-press menu card is the same pattern), not the Shadow DOM controls.
4. **MSE / Web Audio / WebRTC untouched** — `parameters.platformType != FileOrHLS` is rejected
   outright in `supportsType()` so the element fails fast instead of hanging. MSE is what Bilibili
   and every DASH/HLS player needs, and it is the reason `readers=0` on those pages is expected
   rather than a bug.
5. `docs/FEATURE-GAPS-2026-10-08.md` §5 should be updated with whatever the device says.

## Diagnostic surface (for the device round)

One line per navigation lands in `LocalState\stage.txt`:

```
media readers=<N> frames=<decoded>/<presented> pcm=<pushed>/<consumed>/<dropped> q=<depth>@<rate>Hz/<ch>ch native=<guid> out=<guid> dxva=<on|off> err=<hresult> at=<site>
```

`readers=0` means the media engine was never asked to load anything (element absent, or
`supportsType()` rejected the MIME type). `out=0x16` means the Video Processor did hand back
RGB32; anything else means the negotiated output type was not what was asked for.
