// MediaAudio.cpp — see MediaAudio.h.
//
// The harness half of media audio: an AudioGraph whose frame input node is filled, once
// per quantum, from the driver's PCM ring via WebCoreMediaAudioTake().

#include "pch.h"
#include "MediaAudio.h"
#include "WebCoreDriver.h"

#include <MemoryBuffer.h>   // IMemoryBufferByteAccess
#include <wrl/client.h>     // Microsoft::WRL::ComPtr

using namespace concurrency;
using namespace Microsoft::WRL;
using namespace Platform;
using namespace Windows::Foundation;
using namespace Windows::Media::Audio;
using namespace Windows::Media::MediaProperties;

namespace Harness {

// The single device for the process. Held by a file-static because the public API is
// static: there is exactly one audio output no matter how many <video> elements a page
// has (the driver mixes nothing — it has one queue).
static MediaAudio^ g_instance = nullptr;

void MediaAudio::EnsureStarted()
{
    if (!g_instance)
        g_instance = ref new MediaAudio();

    if (g_instance->m_creating || g_instance->m_failed)
        return;

    // Nothing is playing yet (or the page only has images): 0/0 until a stream opens.
    const int sampleRate = WebCoreMediaAudioSampleRate();
    const int channels = WebCoreMediaAudioChannels();
    if (sampleRate <= 0 || channels <= 0)
        return;

    const bool sameFormat = g_instance->m_graph
        && g_instance->m_sampleRate == sampleRate
        && g_instance->m_channels == channels;
    if (sameFormat)
        return;

    // Either the first stream or a differently-formatted one: rebuild.
    g_instance->Reset();
    g_instance->StartGraphFor(sampleRate, channels);
}

void MediaAudio::Stop()
{
    if (!g_instance)
        return;
    g_instance->Reset();
    // A new session (or a retry after the app came back) deserves a fresh attempt.
    g_instance->m_failed = false;
}

bool MediaAudio::IsRunning()
{
    return g_instance && g_instance->m_graph && g_instance->m_inputNode;
}

void MediaAudio::StartGraphFor(int sampleRate, int channels)
{
    m_creating = true;
    m_sampleRate = sampleRate;
    m_channels = channels;

    // AudioRenderCategory lives in Windows::Media::Render, not Windows::Media::Audio.
    auto settings = ref new AudioGraphSettings(Windows::Media::Render::AudioRenderCategory::Media);
    // SystemDefault keeps the device's own quantum; the graph resamples our frame node
    // into the device format, so we only have to match the engine's stream.
    settings->QuantumSizeSelectionMode = QuantumSizeSelectionMode::SystemDefault;

    MediaAudio^ self = this;
    create_task(AudioGraph::CreateAsync(settings)).then([self](task<CreateAudioGraphResult^> creation) {
        if (self->m_graph || !self->m_creating) {
            // Stop() ran while the graph was being created: honour it, but still make sure
            // nothing is left running.
            CreateAudioGraphResult^ late = nullptr;
            try { late = creation.get(); } catch (...) { }
            if (late && late->Status == AudioGraphCreationStatus::Success && late->Graph)
                late->Graph->Stop();
            self->m_creating = false;
            return;
        }

        CreateAudioGraphResult^ result = nullptr;
        try { result = creation.get(); } catch (...) { result = nullptr; }

        if (!result || result->Status != AudioGraphCreationStatus::Success || !result->Graph) {
            // Most likely cause on this device: no audio endpoint, or the App Container
            // refused the activation. Recorded rather than thrown — <video> still plays.
            self->m_failed = true;
            self->m_creating = false;
            return;
        }

        self->m_graph = result->Graph;

        AudioEncodingProperties^ format = AudioEncodingProperties::CreatePcm(
            static_cast<unsigned>(self->m_sampleRate), static_cast<unsigned>(self->m_channels), 32);
        // CreatePcm(32) alone means 32-bit *integer*; the engine's queue is float32, so the
        // subtype is what actually makes this a float format.
        format->Subtype = MediaEncodingSubtypes::Float;
        self->m_inputNode = self->m_graph->CreateFrameInputNode(format);
        if (!self->m_inputNode) {
            self->m_failed = true;
            self->m_creating = false;
            self->m_graph->Stop();
            self->m_graph = nullptr;
            return;
        }

        auto quantumHandler = ref new TypedEventHandler<AudioFrameInputNode^, FrameInputNodeQuantumStartedEventArgs^>(
            [self](AudioFrameInputNode^, FrameInputNodeQuantumStartedEventArgs^ args) {
                self->OnQuantumStarted(nullptr, args);
            });
        self->m_quantumToken = self->m_inputNode->QuantumStarted += quantumHandler;
        self->m_quantumHooked = true;

        self->m_graph->Start();
        self->m_quanta = 0;
        self->m_underruns = 0;
        self->m_creating = false;
    });
}

void MediaAudio::OnQuantumStarted(AudioFrameInputNode^, FrameInputNodeQuantumStartedEventArgs^ args)
{
    if (!m_inputNode || m_sampleRate <= 0 || m_channels <= 0)
        return;

    const int required = static_cast<int>(args->RequiredSamples);
    // The graph may ask for "whatever you have" (0); skipping is legal and just emits
    // silence, which is the right answer when the engine has nothing for us anyway.
    if (required <= 0)
        return;

    const unsigned bytes = static_cast<unsigned>(required * m_channels * static_cast<int>(sizeof(float)));
    // AudioFrame/AudioBuffer/AudioBufferAccessMode live in Windows::Media, NOT
    // Windows::Media::Audio (which only has the graph, the nodes and the nodes' args).
    Windows::Media::AudioFrame^ frame = ref new Windows::Media::AudioFrame(bytes);

    bool filled = false;
    Windows::Media::AudioBuffer^ buffer = frame->LockBuffer(Windows::Media::AudioBufferAccessMode::Write);
    if (buffer) {
        // AudioBuffer implements IMemoryBuffer; the reference is what carries the raw byte
        // access, which is why the QI goes on the reference and not on the frame.
        IMemoryBufferReference^ reference = buffer->CreateReference();
        if (reference) {
            ComPtr<IMemoryBufferByteAccess> access;
            if (SUCCEEDED(reinterpret_cast<IInspectable*>(reference)->QueryInterface(IID_PPV_ARGS(&access)))) {
                BYTE* data = nullptr;
                UINT32 capacity = 0;
                if (SUCCEEDED(access->GetBuffer(&data, &capacity)) && data && capacity >= bytes) {
                    int underran = 0;
                    const int taken = WebCoreMediaAudioTake(reinterpret_cast<float*>(data), required, &underran);
                    filled = true;
                    ++m_quanta;
                    // An underrun at start-up or right after a seek is expected and looks
                    // like silence; counting them is how we tell "starved" from "broken".
                    if (underran || taken < required)
                        ++m_underruns;
                }
            }
            // Release the byte access first, then the reference and the buffer: there is no
            // Close() on IMemoryBufferReference in this projection, and dropping the handles
            // is what ends the lock. The frame must be unlocked before AddFrame().
            access.Reset();
            reference = nullptr;
        }
        buffer = nullptr;
    }

    if (filled)
        m_inputNode->AddFrame(frame);
    // On failure the frame is simply not added: the graph emits silence for this quantum,
    // which keeps audio timing intact instead of stalling the source.
}

void MediaAudio::Reset()
{
    if (m_inputNode) {
        if (m_quantumHooked) {
            m_inputNode->QuantumStarted -= m_quantumToken;
            m_quantumHooked = false;
        }
        m_inputNode = nullptr;
    }

    if (m_graph) {
        m_graph->Stop();
        m_graph = nullptr;
    }

    m_sampleRate = 0;
    m_channels = 0;
    m_creating = false;
}

} // namespace Harness
