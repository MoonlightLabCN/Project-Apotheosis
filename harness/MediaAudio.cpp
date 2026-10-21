// MediaAudio.cpp — see MediaAudio.h.
//
// The harness half of media audio: an AudioGraph whose frame input node is filled, once
// per quantum, from the driver's PCM ring via WebCoreMediaAudioTake().

#include "pch.h"
#include "MediaAudio.h"
#include "WebCoreDriver.h"

#include <MemoryBuffer.h>   // IMemoryBufferByteAccess
#include <wrl/client.h>     // Microsoft::WRL::ComPtr

#include <windows.ui.core.h>   // CoreDispatcher / DispatchedHandler (deferred idle stop)
#include <windows.devices.enumeration.h>   // DeviceInformation (render-device binding)
#include <windows.applicationmodel.core.h>   // CoreApplication::MainView->Dispatcher

#include <fstream>
#include <mutex>
#include <string>

namespace {

// Apotheosis (media, 2026-10-21): how long the ring may stay empty before the graph is
// stopped. Was 3 s, which fired mid-playback on device (the graph drains faster than the
// decoder fills, so brief empty stretches are normal); 20 s cannot happen while audio is
// actually playing, and still bounds the empty-ring spin that faults inside
// Windows.Media.Audio.dll after minutes.
constexpr unsigned long long kIdleStopWindowMs = 20000;

}   // namespace

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

// Apotheosis (media, 2026-10-18): device-side diagnostics. On device the media line reads
// qcons=0 - the AudioGraph never took a single frame from the driver's ring - and nothing
// in the app says why. Every decision point below appends one line to LocalState\
// mediaaudio.txt so the next WDP pull shows exactly where the chain stopped.
static std::mutex s_audioLogMtx;
static std::string NarrowForLog(Platform::String^ s)
{
    if (!s) return {};
    // Apotheosis (media, 2026-10-21): Data() can be null for a non-null but empty
    // Platform::String, and Length() can be 0. Guarding both keeps this out of the same
    // null-dereference class as the AudioLog fix above (this is called from it).
    const wchar_t* data = s->Data();
    const int len = static_cast<int>(s->Length());
    if (!data || len <= 0) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, data, len, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, data, len, &out[0], n, nullptr, nullptr);
    return out;
}
static void AudioLog(const std::string& msg)
{
    std::lock_guard<std::mutex> lock(s_audioLogMtx);
    try {
        SYSTEMTIME st = {};
        GetLocalTime(&st);
        char ts[32] = "";
        snprintf(ts, sizeof ts, "%02d:%02d:%02d ", st.wHour, st.wMinute, st.wSecond);
        // Apotheosis (media, 2026-10-21): ApplicationData::Current is null at the edges of
        // the app lifecycle (suspend/resume, page teardown), and so is LocalFolder. This runs
        // on every present wake via EnsureStarted, so an unguarded dereference there is a
        // null-pointer fault in Harness.exe - crash.txt 2026-10-21 23:31:26, pc resolving to
        // Harness::MediaAudio::EnsureStarted+0x231 with r0=0x00000000. The surrounding
        // try/catch cannot help: this is an SEH access violation, not a C++ exception, and
        // this project builds with exceptions off (/EHs-c-, _HAS_EXCEPTIONS=0). Checking the
        // handles is the only defence.
        Windows::Storage::ApplicationData^ appData = Windows::Storage::ApplicationData::Current;
        if (!appData)
            return;
        Windows::Storage::StorageFolder^ folder = appData->LocalFolder;
        if (!folder)
            return;
        Platform::String^ folderPath = folder->Path;
        if (!folderPath)
            return;
        std::string path = NarrowForLog(folderPath) + "\\mediaaudio.txt";
        std::ofstream f(path, std::ios::app | std::ios::binary);
        if (f) f << ts << msg << "\n";
    } catch (...) {}
}

void MediaAudio::EnsureStarted()
{
    if (!g_instance)
        g_instance = ref new MediaAudio();

    // Apotheosis (media, 2026-10-19): execute the quantum handler's deferred idle stop
    // HERE, on the UI thread. Doing it inside the callback crashed the app (see the flag's
    // declaration). EnsureStarted runs on every present wake, so this lands within a frame
    // or two of the request.
    if (g_instance->m_idleStopRequested) {
        g_instance->m_idleStopRequested = false;
        AudioLog("idle-stop: executing deferred Stop() on the UI thread");
        g_instance->Stop();
        return;
    }

    if (g_instance->m_creating || g_instance->m_failed) {
        AudioLog("EnsureStarted: early-return (creating=" + std::to_string(g_instance->m_creating ? 1 : 0)
                 + " failed=" + std::to_string(g_instance->m_failed ? 1 : 0) + ")");
        return;
    }

    // Nothing is playing yet (or the page only has images): 0/0 until a stream opens.
    const int sampleRate = WebCoreMediaAudioSampleRate();
    const int channels = WebCoreMediaAudioChannels();
    if (sampleRate <= 0 || channels <= 0) {
        AudioLog("EnsureStarted: early-return (no format yet: rate=" + std::to_string(sampleRate)
                 + " ch=" + std::to_string(channels) + ")");
        return;
    }

    const bool sameFormat = g_instance->m_graph
        && g_instance->m_sampleRate == sampleRate
        && g_instance->m_channels == channels;
    if (sameFormat) {
        AudioLog("EnsureStarted: already running at " + std::to_string(sampleRate) + "Hz/"
                 + std::to_string(channels) + "ch (quanta=" + std::to_string(g_instance->m_quanta) + ")");
        return;
    }

    // Either the first stream or a differently-formatted one: rebuild.
    AudioLog("EnsureStarted: starting graph for " + std::to_string(sampleRate) + "Hz/"
             + std::to_string(channels) + "ch");
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

    // Apotheosis (media, 2026-10-18/19): the endpoint enumeration is no longer a
    // fire-and-forget probe - it now FEEDS the graph. On device the graph created
    // "successfully" but consumed every frame into a null sink (media-diag: pcm
    // pushed == consumed == 480128 = the whole file, at real-time speed, yet silence),
    // so the graph's implicit default-device binding is suspect. The enumeration gives
    // us the real devices (name / IsDefault / IsEnabled) and one of them is handed to
    // AudioGraphSettings::PrimaryRenderDevice (its type IS DeviceInformation).
    //
    // 2026-10-21 device reading: on this Lumia BOTH render endpoints report
    // IsDefault=0 (Win10M does not populate that property), so an IsDefault-only pick
    // chose nothing and the graph fell back to the silent implicit binding:
    //   endpoint[0]: Speakers (QCAUD Real Time (WaveRT) Driver for Qualcomm SoC)  default=0
    //   endpoint[1]: Internal Handset Speaker (QCAUD ...)                          default=0
    //   CreateGraphFor: no default AudioRender device enumerated - leaving implicit binding
    // Selection is now: IsDefault wins if anyone claims it; otherwise prefer the name
    // that is NOT the handset (a "Speakers" endpoint is the media route); otherwise the
    // first enabled one. Media playback must never be pinned to the handset earpiece.
    MediaAudio^ self = this;
    concurrency::create_task(Windows::Devices::Enumeration::DeviceInformation::FindAllAsync(
        Windows::Devices::Enumeration::DeviceClass::AudioRender))
        .then([self, sampleRate, channels](
            concurrency::task<Windows::Devices::Enumeration::DeviceInformationCollection^> t) {
            Windows::Devices::Enumeration::DeviceInformation^ chosen = nullptr;
            Windows::Devices::Enumeration::DeviceInformation^ firstEnabled = nullptr;
            Windows::Devices::Enumeration::DeviceInformation^ nonHandset = nullptr;
            try {
                auto devices = t.get();
                if (!devices) {
                    AudioLog("endpoint probe: FindAllAsync returned null collection");
                    self->CreateGraphFor(sampleRate, channels, nullptr);
                    return;
                }
                AudioLog(std::string("endpoint probe: ") + std::to_string(devices->Size)
                         + " AudioRender device(s)");
                for (unsigned i = 0; i < devices->Size; ++i) {
                    auto d = devices->GetAt(i);
                    // A null entry is not worth a null-dereference: skip it and say so.
                    if (!d) {
                        AudioLog("  endpoint[" + std::to_string(i) + "]: <null>");
                        continue;
                    }
                    const std::string name = NarrowForLog(d->Name);
                    AudioLog("  endpoint[" + std::to_string(i) + "]: " + name
                             + " default=" + (d->IsDefault ? "1" : "0")
                             + " enabled=" + (d->IsEnabled ? "1" : "0"));
                    if (!d->IsEnabled)
                        continue;
                    if (!firstEnabled)
                        firstEnabled = d;
                    if (d->IsDefault && !chosen)
                        chosen = d;
                    // Case-insensitive "handset"/"earpiece" check without <algorithm>:
                    // lowercase a copy and look for the marker.
                    std::string lower;
                    lower.reserve(name.size());
                    for (char c : name)
                        lower.push_back(static_cast<char>(::tolower(static_cast<unsigned char>(c))));
                    if (lower.find("handset") == std::string::npos
                        && lower.find("earpiece") == std::string::npos
                        && !nonHandset)
                        nonHandset = d;
                }
            } catch (...) { AudioLog("endpoint probe: FindAllAsync threw"); }
            if (!chosen)
                chosen = nonHandset ? nonHandset : firstEnabled;
            self->CreateGraphFor(sampleRate, channels, chosen);
        });
}

void MediaAudio::CreateGraphFor(int sampleRate, int channels,
    Windows::Devices::Enumeration::DeviceInformation^ renderDevice)
{
    // AudioRenderCategory lives in Windows::Media::Render, not Windows::Media::Audio.
    auto settings = ref new AudioGraphSettings(Windows::Media::Render::AudioRenderCategory::Media);
    // SystemDefault keeps the device's own quantum; the graph resamples our frame node
    // into the device format, so we only have to match the engine's stream.
    settings->QuantumSizeSelectionMode = QuantumSizeSelectionMode::SystemDefault;
    if (renderDevice) {
        settings->PrimaryRenderDevice = renderDevice;
        AudioLog("CreateGraphFor: PrimaryRenderDevice bound to '"
                 + NarrowForLog(renderDevice->Name) + "'");
    } else {
        AudioLog("CreateGraphFor: no usable AudioRender device enumerated - leaving the graph's implicit binding");
    }

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
            AudioLog(std::string("StartGraphFor: CreateAsync FAILED status=")
                     + (result ? std::to_string(static_cast<int>(result->Status)) : std::string("exception"))
                     + " -> m_failed=true (no audio; <video> still plays)");
            return;
        }

        self->m_graph = result->Graph;
        AudioLog("StartGraphFor: graph created");

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
            AudioLog("StartGraphFor: CreateFrameInputNode returned null -> m_failed=true");
            return;
        }

        auto quantumHandler = ref new TypedEventHandler<AudioFrameInputNode^, FrameInputNodeQuantumStartedEventArgs^>(
            [self](AudioFrameInputNode^, FrameInputNodeQuantumStartedEventArgs^ args) {
                self->OnQuantumStarted(nullptr, args);
            });
        self->m_quantumToken = self->m_inputNode->QuantumStarted += quantumHandler;
        self->m_quantumHooked = true;

        // Apotheosis (media, 2026-10-21): THE MISSING LINK. Every earlier round created a
        // frame INPUT node, hooked QuantumStarted and started the graph - but never created
        // a device OUTPUT node nor connected the two. An AudioGraph with an unconnected
        // input node has nothing consuming its output, so it pulls frames at maximum speed
        // and discards them: exactly the ~17x-realtime spin this project has been reading
        // for days (und == quanta-100 roughly, required=480 always, ring永远被抽干).
        // It also explains why the decoder's counters looked healthy - the samples really
        // were being taken - and why FULL VOLUME produced silence: they went nowhere.
        // Nothing reaches the speaker until the frame node is connected to a device output
        // node, so the tone test was silent too.
        //
        // Both nodes must be created and connected BEFORE graph->Start().
        create_task(self->m_graph->CreateDeviceOutputNodeAsync())
            .then([self](task<CreateAudioDeviceOutputNodeResult^> outTask) {
                CreateAudioDeviceOutputNodeResult^ outResult = nullptr;
                try { outResult = outTask.get(); } catch (...) { outResult = nullptr; }
                if (!outResult || outResult->Status != AudioDeviceNodeCreationStatus::Success
                    || !outResult->DeviceOutputNode) {
                    // Not m_failed: this is worth retrying (the endpoint can be busy or
                    // briefly unavailable). Clearing m_graph is what matters - leaving it
                    // set would make EnsureStarted's sameFormat check early-return forever,
                    // silently pinning the app to a graph that has no output.
                    self->m_creating = false;
                    AudioLog(std::string("StartGraphFor: CreateDeviceOutputNodeAsync FAILED status=")
                             + (outResult ? std::to_string(static_cast<int>(outResult->Status))
                                          : std::string("exception"))
                             + " -> no output node, no sound (will retry)");
                    if (self->m_graph) {
                        self->m_graph->Stop();
                        self->m_graph = nullptr;
                    }
                    self->m_inputNode = nullptr;
                    self->m_sampleRate = 0;
                    self->m_channels = 0;
                    return;
                }
                self->m_outputNode = outResult->DeviceOutputNode;
                try {
                    self->m_inputNode->AddOutgoingConnection(self->m_outputNode);
                    AudioLog("StartGraphFor: input node CONNECTED to device output node");
                } catch (...) {
                    AudioLog("StartGraphFor: AddOutgoingConnection threw -> no sound");
                }

                self->m_graph->Start();
                self->m_quanta = 0;
                self->m_underruns = 0;
                self->m_lastNonEmptyQuantum = GetTickCount64();   // idle-stop reference point
                self->m_uiDispatcher = Windows::ApplicationModel::Core::CoreApplication::MainView->Dispatcher;
                self->m_creating = false;
                AudioLog("StartGraphFor: graph STARTED (quantum handler hooked)");
            });
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
                    // Apotheosis (media, 2026-10-19): null-sink guard. The device round
                    // showed the graph spinning on an empty ring (~17x realtime) and then
                    // faulting inside Windows.Media.Audio.dll minutes later. Once the ring
                    // has been empty for 3 s there is nothing left to play: request a stop
                    // (DEFERRED to the UI thread - tearing the graph down from inside this
                    // callback is what crashed the app, crash.txt 2026-10-19 23:20:26).
                    if (taken > 0)
                        m_lastNonEmptyQuantum = GetTickCount64();
                    else if (m_quanta > 100 && GetTickCount64() - m_lastNonEmptyQuantum > kIdleStopWindowMs) {
                        // DEFERRED stop: never tear the graph down from inside this
                        // callback (crash.txt 2026-10-19 23:20:26 - PC inside
                        // Windows.Media.Audio.dll). Raise the flag once, post the Stop to
                        // the UI thread, and stop touching the frame entirely.
                        //
                        // 2026-10-21: the window was 3 s and it fired DURING a live
                        // playback (23:13:52 round: stop posted while frames=499/156 and
                        // the decoder was still pushing audio), because the graph drains
                        // faster than the decoder fills and a momentary empty ring is
                        // normal. A window this long cannot occur while anything is
                        // playing, and the guard only exists to stop the empty-ring spin
                        // that eventually faults in Windows.Media.Audio.dll.
                        if (!m_idleStopRequested.exchange(true)) {
                            AudioLog("idle-stop: ring empty >" + std::to_string(kIdleStopWindowMs / 1000)
                                     + "s -> posting Stop() to the UI thread");
                            auto disp = m_uiDispatcher;
                            if (disp) {
                                MediaAudio^ self = this;   // handle capture: the lambda outlives this call
                                disp->RunAsync(Windows::UI::Core::CoreDispatcherPriority::Normal,
                                    ref new Windows::UI::Core::DispatchedHandler([self]() {
                                        if (self->m_idleStopRequested.exchange(false)) {
                                            AudioLog("idle-stop: executing deferred Stop() on the UI thread");
                                            MediaAudio::Stop();   // static: tears down g_instance
                                        }
                                    }));
                            }
                        }
                        return;   // graph is going away; do not add another silent frame
                    }
                    // Apotheosis (media, 2026-10-19): live decode-side counters. The
                    // 23:20 round showed the ring empty during playback - the graph was
                    // asking and the decoder was not pushing - and the media line (a
                    // per-navigation snapshot at load time) was written long before that.
                    // These lines show whether PCM is flowing at all.
                    if (m_quanta % 5000 == 0) {
                        char diag[512] = "";
                        WebCoreMediaDiagnostics(diag, sizeof diag);
                        AudioLog(std::string("media-diag #") + std::to_string(m_quanta) + ": " + diag);
                    }
                    // An underrun at start-up or right after a seek is expected and looks
                    // like silence; counting them is how we tell "starved" from "broken".
                    if (underran || taken < required)
                        ++m_underruns;
                    // Apotheosis (media, 2026-10-18): the first quanta prove the graph is
                    // alive and draining the ring; then one line per 100 keeps the log small.
                    if (m_quanta <= 3 || m_quanta % 100 == 0)
                        AudioLog("quantum #" + std::to_string(m_quanta) + " required=" + std::to_string(required)
                                 + " taken=" + std::to_string(taken) + " underran=" + std::to_string(underran));
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
    AudioLog(std::string("Reset() [quanta=") + std::to_string(m_quanta) + " underruns=" + std::to_string(m_underruns) + "]");
    if (m_inputNode) {
        if (m_quantumHooked) {
            m_inputNode->QuantumStarted -= m_quantumToken;
            m_quantumHooked = false;
        }
        // Disconnect before dropping the node: a connection outliving its endpoint is what
        // makes the audio stack fault on teardown.
        if (m_outputNode) {
            try { m_inputNode->RemoveOutgoingConnection(m_outputNode); } catch (...) { }
        }
        m_inputNode = nullptr;
    }
    m_outputNode = nullptr;

    if (m_graph) {
        m_graph->Stop();
        m_graph = nullptr;
    }

    m_sampleRate = 0;
    m_channels = 0;
    m_creating = false;
    // Apotheosis (media, 2026-10-21): clear the idle-stop state with the graph. Device log
    // showed the pair "posting Stop()" then "executing deferred Stop()" TWICE in a row
    // (23:13:27 and again 23:13:52): the handler could run once more between the post and
    // the UI-thread execution, raise the flag again, and post a second stop for a graph
    // that was already gone. Resetting the window here means a rebuilt graph starts a
    // fresh idle countdown instead of inheriting a stale one.
    m_idleStopRequested = false;
    m_lastNonEmptyQuantum = GetTickCount64();
}

} // namespace Harness
