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
    std::snprintf(out, static_cast<size_t>(outBytes),
        "media readers=%u frames=%llu/%llu pcm=%llu/%llu/%llu q=%d@%dHz/%dch"
        " native=0x%X out=0x%X dxva=%s err=0x%08lX at=%s",
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
        WebCore::WinUWPMediaDiagnostics::dxvaDisabled() ? "off" : "on",
        static_cast<unsigned long>(WebCore::WinUWPMediaDiagnostics::lastError()),
        WebCore::WinUWPMediaDiagnostics::lastErrorSite() ? WebCore::WinUWPMediaDiagnostics::lastErrorSite() : "-");
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
