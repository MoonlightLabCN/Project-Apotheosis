// PortMediaAudio.cpp — see PortMediaAudio.h.
//
// The engine side of the media audio path. WebCore's WinUWP media backend pushes
// interleaved float32 PCM into the sink installed here; the harness pulls it out through
// WebCoreAudioTakePCM(). Keeping the device in the harness is deliberate (see the header).

#include "PortMediaAudio.h"

#include "PortAudioOutput.h"
#include "WebCoreDriver.h"   // the C ABI declarations, so these definitions are checked

#include <WebCore/WinUWPMediaBridge.h>

#include <atomic>
#include <cstdio>

namespace WebCorePort {

// Declared in PortChromeClient.h. Needed here because the harness only ever starts its AudioGraph
// from a present callback (OnPresentWake -> MediaAudio::EnsureStarted), and a media page that is
// stalled produces no presents of its own - so the format becomes known and the graph is never
// built for it. Asking for a present the moment a format is configured closes that gap.
// Declared at namespace scope, NOT inside the anonymous namespace below, or it would be a
// different, never-defined function with internal linkage.
void presentRequested();

namespace {

// One second of audio. Deep enough to absorb a decode hiccup or a slow first frame,
// shallow enough that a seek only throws away about a second of work.
int ringCapacityFor(int sampleRate)
{
    if (sampleRate <= 0)
        return 48000;
    if (sampleRate > 48000)
        return 96000;   // clamped by PortAudioOutput to its own maximum
    return sampleRate;
}

PortAudioOutput g_audioQueue;
std::atomic<bool> g_installed { false };

// Apotheosis: the WebCore-facing half. Every method is called from the engine or the
// decode thread (never the harness); the queue itself does the locking, so this class
// holds no state.
class MediaAudioSink final : public WebCore::WinUWPMediaAudioSink {
public:
    void configure(int sampleRate, int channels) final
    {
        g_audioQueue.configure(sampleRate, channels, ringCapacityFor(sampleRate));
        WebCorePort::presentRequested();
    }

    void reset() final
    {
        g_audioQueue.reset();
    }

    int pushPCM(const float* interleaved, int frames) final
    {
        return g_audioQueue.pushPCM(interleaved, frames);
    }

    uint64_t consumedFrames() const final
    {
        return g_audioQueue.consumedFrames();
    }
};

MediaAudioSink g_sink;

} // anonymous namespace

void installMediaAudioSink()
{
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true))
        return;

    // WebCore declares the interface, the port implements it: the engine never has to
    // know that a port/ symbol exists.
    WebCore::setWinUWPMediaAudioSink(&g_sink);
}

int takeMediaPCM(float* out, int frames, bool* outUnderran)
{
    return g_audioQueue.takePCM(out, frames, outUnderran);
}

int mediaAudioSampleRate()
{
    return g_audioQueue.sampleRate();
}

int mediaAudioChannels()
{
    return g_audioQueue.channels();
}

void appendMediaDiagnostics(char* out, int outBytes)
{
    if (!out || outBytes <= 0)
        return;
    out[0] = '\0';

    // subtype values are GUID::Data1 of the negotiated video media types; RGB32 is 0x16,
    // NV12 is 0x3231564E, H264 is 0x34363248. Enough to tell "we got RGB32 out of the
    // Video Processor" from "the transform never engaged" on device.
    // Apotheosis (media, 2026-10-18/19): dxva reports the MAIN decode path, which is
    // software: the DXVA experiment is closed (the D3D manager cannot be handed to the MF
    // decoder on 15254 - see MediaPlayerPrivateWinUWP.cpp). What the device DOES expose
    // (decoder profile count, feature level, H.264 profile) is in dxvaprof/dxvafl/h264.
    const char* dxvaText = "sw";
    std::snprintf(out, static_cast<size_t>(outBytes),
        "media readers=%u frames=%llu/%llu pcm=%llu/%llu/%llu q=%d@%dHz/%dch"
        " native=0x%X out=0x%X dxva=%s err=0x%08lX at=%s"
        " reads=%llu lasthr=0x%08lX flags=0x%X sidx=%d vidx=%d aidx=%d drop=%llu"
        " cap=%d qpush=%llu qcons=%llu qdrop=%llu und=%llu cpush=%llu phase=%d"
        " dxvaprof=%u dxvafl=0x%04X h264=%d",
        WebCore::WinUWPMediaDiagnostics::startedSourceReaders(),
        WebCore::WinUWPMediaDiagnostics::framesDecoded(),
        WebCore::WinUWPMediaDiagnostics::framesPresented(),
        WebCore::WinUWPMediaDiagnostics::pcmFramesPushed(),
        g_audioQueue.consumedFrames(),
        g_audioQueue.droppedFrames(),
        g_audioQueue.bufferedFrames(),
        g_audioQueue.sampleRate(),
        g_audioQueue.channels(),
        WebCore::WinUWPMediaDiagnostics::nativeVideoSubtype(),
        WebCore::WinUWPMediaDiagnostics::outputVideoSubtype(),
        dxvaText,
        static_cast<unsigned long>(WebCore::WinUWPMediaDiagnostics::lastError()),
        WebCore::WinUWPMediaDiagnostics::lastErrorSite() ? WebCore::WinUWPMediaDiagnostics::lastErrorSite() : "-",
        // ReadSample instrumentation. `reads` rising means the synchronous read does return;
        // lasthr/flags then say what it returned, and sidx against vidx/aidx says whether a real
        // sample matched either configured stream (drop counts the ones that matched neither).
        WebCore::WinUWPMediaDiagnostics::readSampleCalls(),
        static_cast<unsigned long>(WebCore::WinUWPMediaDiagnostics::lastReadSampleHr()),
        WebCore::WinUWPMediaDiagnostics::lastReadSampleFlags(),
        static_cast<int>(WebCore::WinUWPMediaDiagnostics::lastReadSampleStreamIndex()),
        static_cast<int>(WebCore::WinUWPMediaDiagnostics::videoStreamIndex()),
        static_cast<int>(WebCore::WinUWPMediaDiagnostics::audioStreamIndex()),
        WebCore::WinUWPMediaDiagnostics::samplesDroppedStreamIndex(),
        // The queue's own view of the same traffic. `pcm` above is the *media side's* counter,
        // which only increments once a push returns; these say whether a push was attempted at
        // all (qpush 0 with cap > 0 = nothing ever reached the ring) and whether anything is
        // draining it (und growing = the harness IS pulling and finding it empty).
        g_audioQueue.capacityFrames(),
        static_cast<unsigned long long>(g_audioQueue.pushedFrames()),
        static_cast<unsigned long long>(g_audioQueue.consumedFrames()),
        static_cast<unsigned long long>(g_audioQueue.droppedFrames()),
        static_cast<unsigned long long>(g_audioQueue.underrunEvents()),
        static_cast<unsigned long long>(g_audioQueue.contendedPushes()),
        WebCore::WinUWPMediaDiagnostics::audioPhase(),
        WebCore::WinUWPMediaDiagnostics::dxvaProfiles(),
        WebCore::WinUWPMediaDiagnostics::dxvaFeatureLevel(),
        WebCore::WinUWPMediaDiagnostics::dxvaH264Profile() ? 1 : 0);
}

} // namespace WebCorePort

// ============================================================================
//  C ABI (declared in WebCoreDriver.h; both copies must stay in sync)
// ============================================================================

extern "C" int WebCoreMediaAudioTake(float* out, int frames, int* underran)
{
    bool underranFlag = false;
    const int taken = WebCorePort::takeMediaPCM(out, frames, underran ? &underranFlag : nullptr);
    if (underran)
        *underran = underranFlag ? 1 : 0;
    return taken;
}

extern "C" int WebCoreMediaAudioSampleRate(void)
{
    return WebCorePort::mediaAudioSampleRate();
}

extern "C" int WebCoreMediaAudioChannels(void)
{
    return WebCorePort::mediaAudioChannels();
}

extern "C" void WebCoreMediaDiagnostics(char* out, int outBytes)
{
    WebCorePort::appendMediaDiagnostics(out, outBytes);
}
