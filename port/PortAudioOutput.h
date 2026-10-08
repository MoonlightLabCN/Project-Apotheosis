// ============================================================================
//  PortAudioOutput.h — WinUWP audio render queue (Apotheosis 2026-10-08)
//
//  The engine has no audio device access of its own. This class is the
//  engine-side half of the media audio path:
//
//      MediaPlayerPrivateWinUWP  ->  pushPCM()  ->  bounded PCM queue
//                                                     |
//                                     harness WASAPI IAudioClient::GetBuffer
//                                                     v
//                                              takePCM() (pull side)
//
//  The queue is lock-protected but every operation is O(bytes) and nonblocking
//  with respect to the device: pushPCM() never waits for the audio callback and
//  takePCM() never waits for the engine thread. That keeps the single-engine-
//  thread rule intact — the engine only ever touches its own bounded buffer,
//  and the code that talks to IAudioClient lives on the harness side.
//
//  Format policy: the queue is fixed at the format the engine was told to open
//  (stereo float32 by default). Rate conversion, if the device runs at another
//  rate, happens on the pull side where the device format is known.
// ============================================================================

#pragma once

#include <cstdint>
#include <vector>
#include <mutex>

namespace WebCorePort {

class PortAudioOutput {
public:
    PortAudioOutput();
    ~PortAudioOutput();

    // Configure the queue for a stream. sampleRate may be 0 ("unknown yet"),
    // which only changes the bookkeeping, not the byte layout: samples are
    // always interleaved float32. capacityFrames is clamped to a sane range so
    // a caller cannot ask for an unbounded buffer. Returns false on bad args.
    bool configure(int sampleRate, int channels, int capacityFrames);

    // Reset the queue and forget the stream (stop/seek/media teardown).
    void reset();

    // --- engine thread: producer side ---
    // Append interleaved float32 frames. Returns frames actually queued; frames
    // that do not fit are dropped (counted in droppedFrames()) rather than
    // blocking the engine thread. channels must match the configured value.
    int pushPCM(const float* interleaved, int frames);

    // --- device thread: consumer side ---
    // Copy up to `frames` frames into `out`. Returns frames copied; the caller
    // zero-fills the remainder when the queue underran.
    int takePCM(float* out, int frames, bool* outUnderran);

    // --- diagnostics (any thread) ---
    int bufferedFrames() const;
    int capacityFrames() const;
    int channels() const;
    int sampleRate() const;
    uint64_t pushedFrames() const;
    uint64_t consumedFrames() const;
    uint64_t droppedFrames() const;
    uint64_t underrunEvents() const;

private:
    mutable std::mutex m_mutex;
    std::vector<float> m_ring;
    int m_capacityFrames { 0 };
    int m_channels { 2 };
    int m_sampleRate { 0 };
    int m_readFrame { 0 };
    int m_writeFrame { 0 };
    int m_bufferedFrames { 0 };
    uint64_t m_pushedFrames { 0 };
    uint64_t m_consumedFrames { 0 };
    uint64_t m_droppedFrames { 0 };
    uint64_t m_underrunEvents { 0 };
};

} // namespace WebCorePort
