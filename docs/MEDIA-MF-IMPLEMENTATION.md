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
| appx | `harness/AppPackages/Harness/Harness_0.2.5.18_ARM_Test/` | built |

`port/link-driver-gpu.ps1` reports `EXIT=0 undefined=0 duplicate=0` and `build-harness.ps1`
reports `MSBuild exit code: 0`.

Nothing has been on a device yet. Every claim above is a build claim.

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

## What still has to happen

1. **Device round for picture.** Local mp4, `<video controls>`. Unknown: whether
   `MFStartup`/`MFCreateSourceReaderFromURL`/the Video Processor MFT are all permitted
   inside the App Container. If the transform fails, set `MF_SOURCE_READER_DISABLE_DXVA = TRUE`
   in `openSourceReader()` (one line, already commented in place) to force software decode.
2. **Device round for sound.** `ensureMediaFoundationStarted` + AudioGraph creation are both
   unproven on 15254.
3. **A/V sync is wall-clock, not audio-slaved.** `PortAudioOutput::consumedFrames()` is plumbed
   through the sink and unused; the media clock is `play()`/`seek()`-anchored wall time. Once
   audio is audible, re-anchor `clockNowLocked()` to the device's consumed frames.
4. **No controls.** `modern-media-controls` is still dropped; the plan is a harness XAML overlay
   (the long-press menu card is the same pattern), not the Shadow DOM controls.
5. **MSE / Web Audio / WebRTC untouched** — `parameters.platformType != FileOrHLS` is rejected
   outright in `supportsType()` so the element fails fast instead of hanging.
6. `docs/FEATURE-GAPS-2026-10-08.md` §5 should be updated with whatever the device says.

## Diagnostic surface (for the device round)

One line per navigation lands in `LocalState\stage.txt`:

```
media readers=<N> frames=<decoded>/<presented> pcm=<pushed>/<consumed>/<dropped> q=<depth>@<rate>Hz/<ch>ch native=<guid> out=<guid> dxva=<on|off> err=<hresult> at=<site>
```

`readers=0` means the media engine was never asked to load anything (element absent, or
`supportsType()` rejected the MIME type). `out=0x16` means the Video Processor did hand back
RGB32; anything else means the negotiated output type was not what was asked for.
