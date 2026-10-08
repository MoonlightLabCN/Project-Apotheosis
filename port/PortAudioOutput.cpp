// PortAudioOutput.cpp — see PortAudioOutput.h.
// Apotheosis: this is deliberately independent of WASAPI. Keeping the device
// object in the MSVC/C++/CX harness avoids pulling COM apartment policy into
// the clang-cl WebCore driver; the harness drains this queue from its audio
// worker when the media backend is wired.

#include "PortAudioOutput.h"

#include <algorithm>
#include <cstring>

namespace WebCorePort {

PortAudioOutput::PortAudioOutput() = default;
PortAudioOutput::~PortAudioOutput() = default;

bool PortAudioOutput::configure(int sampleRate, int channels, int capacityFrames)
{
    if (channels < 1 || channels > 8 || capacityFrames < 1)
        return false;

    capacityFrames = std::clamp(capacityFrames, 256, 48000 * 2);
    std::lock_guard locker { m_mutex };
    m_sampleRate = sampleRate;
    m_channels = channels;
    m_capacityFrames = capacityFrames;
    m_ring.assign(static_cast<size_t>(capacityFrames) * static_cast<size_t>(channels), 0.0f);
    m_readFrame = 0;
    m_writeFrame = 0;
    m_bufferedFrames = 0;
    m_pushedFrames = 0;
    m_consumedFrames = 0;
    m_droppedFrames = 0;
    m_underrunEvents = 0;
    return true;
}

void PortAudioOutput::reset()
{
    std::lock_guard locker { m_mutex };
    m_readFrame = 0;
    m_writeFrame = 0;
    m_bufferedFrames = 0;
}

int PortAudioOutput::pushPCM(const float* interleaved, int frames)
{
    if (!interleaved || frames <= 0)
        return 0;

    std::lock_guard locker { m_mutex };
    if (!m_capacityFrames || !m_channels)
        return 0;

    int accepted = std::min(frames, m_capacityFrames - m_bufferedFrames);
    for (int frame = 0; frame < accepted; ++frame) {
        auto* dst = m_ring.data() + static_cast<size_t>(m_writeFrame) * m_channels;
        const auto* src = interleaved + static_cast<size_t>(frame) * m_channels;
        std::memcpy(dst, src, static_cast<size_t>(m_channels) * sizeof(float));
        m_writeFrame = (m_writeFrame + 1) % m_capacityFrames;
    }
    m_bufferedFrames += accepted;
    m_pushedFrames += static_cast<uint64_t>(accepted);
    m_droppedFrames += static_cast<uint64_t>(frames - accepted);
    return accepted;
}

int PortAudioOutput::takePCM(float* out, int frames, bool* outUnderran)
{
    if (outUnderran)
        *outUnderran = false;
    if (!out || frames <= 0)
        return 0;

    std::lock_guard locker { m_mutex };
    if (!m_capacityFrames || !m_channels)
        return 0;

    int copied = std::min(frames, m_bufferedFrames);
    for (int frame = 0; frame < copied; ++frame) {
        const auto* src = m_ring.data() + static_cast<size_t>(m_readFrame) * m_channels;
        auto* dst = out + static_cast<size_t>(frame) * m_channels;
        std::memcpy(dst, src, static_cast<size_t>(m_channels) * sizeof(float));
        m_readFrame = (m_readFrame + 1) % m_capacityFrames;
    }
    m_bufferedFrames -= copied;
    m_consumedFrames += static_cast<uint64_t>(copied);
    if (copied < frames) {
        std::memset(out + static_cast<size_t>(copied) * m_channels, 0,
            static_cast<size_t>(frames - copied) * m_channels * sizeof(float));
        ++m_underrunEvents;
        if (outUnderran)
            *outUnderran = true;
    }
    return copied;
}

int PortAudioOutput::bufferedFrames() const { std::lock_guard locker { m_mutex }; return m_bufferedFrames; }
int PortAudioOutput::capacityFrames() const { std::lock_guard locker { m_mutex }; return m_capacityFrames; }
int PortAudioOutput::channels() const { std::lock_guard locker { m_mutex }; return m_channels; }
int PortAudioOutput::sampleRate() const { std::lock_guard locker { m_mutex }; return m_sampleRate; }
uint64_t PortAudioOutput::pushedFrames() const { std::lock_guard locker { m_mutex }; return m_pushedFrames; }
uint64_t PortAudioOutput::consumedFrames() const { std::lock_guard locker { m_mutex }; return m_consumedFrames; }
uint64_t PortAudioOutput::droppedFrames() const { std::lock_guard locker { m_mutex }; return m_droppedFrames; }
uint64_t PortAudioOutput::underrunEvents() const { std::lock_guard locker { m_mutex }; return m_underrunEvents; }

} // namespace WebCorePort
