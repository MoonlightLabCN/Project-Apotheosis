// ============================================================================
//  MediaAudio.h — Apotheosis (media-mf): the harness half of media audio output.
//
//  WebCore's WinUWP media backend decodes audio and pushes interleaved float32 PCM into
//  a bounded queue that lives in the driver (port/PortMediaAudio.cpp, exposed as
//  WebCoreMediaAudioTake). This class owns the actual device.
//
//  Why the device is here and not in the engine: obtaining an audio client inside an App
//  Container means WinRT activation (AudioGraph), which is C++/CX territory, and the port
//  deliberately keeps all device/WinRT work on the harness side. The driver only ever
//  touches its own ring buffer.
//
//  Threading: every method here is UI-thread only. AudioGraph delivers QuantumStarted on
//  the thread that created the graph, and the pull behind it is a lock-free copy out of
//  the driver's ring, so nothing here ever waits on the engine — the port's
//  "UI never blocks on the engine" rule holds.
// ============================================================================

#pragma once

#include <ppltasks.h>
#include <atomic>

namespace Harness {

public ref class MediaAudio sealed {
public:
    // Bring the device up for whatever format the engine currently has, rebuilding it if
    // the format changed (an AudioFrameInputNode's format is fixed at creation, and two
    // videos in a row need not share a sample rate). Cheap enough to call every frame:
    // it returns immediately unless something actually changed. UI thread.
    static void EnsureStarted();

    // Tear the device down (session ended, app backgrounded). UI thread. A later
    // EnsureStarted() starts over, so a transient creation failure is not permanent.
    static void Stop();

    // True once a graph is running and frames are being requested.
    static bool IsRunning();

private:
    // Not `= default`: C++/CX rejects defaulted/deleted members on a WinRT class.
    MediaAudio() { }

    void StartGraphFor(int sampleRate, int channels);
    // Apotheosis (media, 2026-10-19): split out of StartGraphFor so the render-device
    // enumeration can finish first and hand its default device to
    // AudioGraphSettings::PrimaryRenderDevice (the graph's implicit binding produced a
    // null sink on device: everything pushed was consumed, at real-time speed, silently).
    void CreateGraphFor(int sampleRate, int channels,
        Windows::Devices::Enumeration::DeviceInformation^ renderDevice);
    void OnQuantumStarted(Windows::Media::Audio::AudioFrameInputNode^ sender,
        Windows::Media::Audio::FrameInputNodeQuantumStartedEventArgs^ args);
    void Reset();

    Windows::Media::Audio::AudioGraph^ m_graph;
    Windows::Media::Audio::AudioFrameInputNode^ m_inputNode;
    // Apotheosis (media, 2026-10-21): the device output node the frame node feeds. Its
    // absence was the reason everything was silent: an AudioGraph whose input node is not
    // connected to an output node has no consumer, so it runs at maximum speed and throws
    // the frames away (the ~17x-realtime spin this project kept reading). Created and
    // connected before Start().
    Windows::Media::Audio::AudioDeviceOutputNode^ m_outputNode;
    // Needed to unhook QuantumStarted; a stale handler would keep pulling PCM for a
    // device that no longer exists.
    Windows::Foundation::EventRegistrationToken m_quantumToken { };
    bool m_quantumHooked { false };

    int m_sampleRate { 0 };
    int m_channels { 0 };
    bool m_creating { false };
    bool m_failed { false };

    unsigned long long m_quanta { 0 };
    unsigned long long m_underruns { 0 };
    // Apotheosis (media, 2026-10-18): null-sink guard. On device the AudioGraph spun at
    // ~17x realtime on a null endpoint and faulted inside Windows.Media.Audio.dll after
    // ~5 minutes of pulling from an empty ring. The graph is stopped once the ring has
    // been empty for this long (GetTickCount64, ms); the next EnsureStarted rebuilds it.
    unsigned long long m_lastNonEmptyQuantum { 0 };
    // Apotheosis (media, 2026-10-19): the idle stop is DEFERRED. Tearing the graph down
    // (AudioGraph::Stop + unhooking QuantumStarted) from inside the QuantumStarted
    // callback itself crashes inside Windows.Media.Audio.dll - crash.txt 2026-10-19
    // 23:20:26 tid=1788, and the app dies with it (follow-on AV on the UI thread). The
    // handler only raises this flag and posts a dispatcher job; the UI thread executes
    // the Stop. (EnsureStarted consuming the flag is the backstop if the post fails.)
    std::atomic<bool> m_idleStopRequested { false };
    // Captured when the graph starts, so the quantum handler can marshal back to the UI
    // thread (null until the first graph).
    Windows::UI::Core::CoreDispatcher^ m_uiDispatcher;
};

} // namespace Harness
