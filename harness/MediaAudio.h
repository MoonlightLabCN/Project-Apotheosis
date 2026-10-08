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
    void OnQuantumStarted(Windows::Media::Audio::AudioFrameInputNode^ sender,
        Windows::Media::Audio::FrameInputNodeQuantumStartedEventArgs^ args);
    void Reset();

    Windows::Media::Audio::AudioGraph^ m_graph;
    Windows::Media::Audio::AudioFrameInputNode^ m_inputNode;
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
};

} // namespace Harness
