# Win10 Mobile media implementation notes

Branch: `media-mf`

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

The invariant needed is "**the object cannot be destroyed while the decode thread is alive**", which
means the keep-alive must be taken on the main thread *before* the thread starts and released *after*
the join — never by the thread itself:

1. Add a member `RefPtr<MediaPlayerPrivateWinUWP> m_threadKeepAlive;` and set it to `*this` in
   `startDecodeThreadIfNeeded()` **before** `Thread::create`.
2. `stopDecodeThread()`: signal stop, `waitForCompletion()`, clear `m_decodeThread`, and only then
   clear `m_threadKeepAlive`. Every caller that can drop the last reference
   (`~MediaPlayerPrivateWinUWP`, `cancelLoad`) must hold a local
   `Ref<MediaPlayerPrivateWinUWP> protectedThis { *this }` across the call, so the final release does
   not destroy the object while a member function is still touching `this`.
3. With that in place the decode thread may keep its temporary self-refs: the count can no longer
   reach 0 while it runs.

Do **not** "fix" it by letting the decode thread hold a long-lived self-ref: that merely moves the
destruction onto the decode thread, where `stopDecodeThread()` would then join itself.

### Also learned: console.txt is buffered, not broken

The probe pages' self-reports (`PROBE ...`, `MEDIADIAG ...`) that were *missing* during the earlier
round showed up in `console.txt` hours later, at 23:3x. Console mirroring works; it **flushes late**.
The earlier claim in this document that it "does not flush for these pages" was wrong — pulling too
early is what made it look empty. `stage.txt` is buffered the same way, which is why its `media ...`
line had not landed when the round was read.

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

## Workflow: full rebuilds are the norm, so probe single files

Every `ninja WebCore` run on this tree is ~1007 steps / 60-80 minutes, because the CMake PCH
is regenerated and invalidates every TU. Do not iterate through it.

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
