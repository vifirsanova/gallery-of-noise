#pragma once

#include "core/Engine.hpp"
#include "core/RingBuffer.hpp"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace biomus {

struct AudioConfig {
    std::string device = "default";
    uint32_t sampleRate = 48000;
    uint32_t periodFrames = 128; // Spec 2: 128-256 samples
    uint32_t periods = 3;
    bool preferSimulated = false;
};

// ============================================================================
// Abstract Audio Backend Adapter (allows ALSA now, JACK adapter later per Spec 2)
// ============================================================================
class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;
    virtual bool start(Engine& engine, const AudioConfig& cfg) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const noexcept = 0;
    virtual const char* backendName() const noexcept = 0;
    virtual uint64_t xrunCount() const noexcept = 0;

    // Optional live stereo PCM monitor tap (for WebSocket browser audio preview)
    virtual size_t popMonitorStereo(int16_t* dstInterleaved, size_t maxFrames) noexcept {
        (void)dstInterleaved;
        (void)maxFrames;
        return 0;
    }
    virtual void setMonitorEnabled(bool enabled) noexcept { (void)enabled; }
};

std::unique_ptr<IAudioBackend> createAlsaAudioBackend();
std::unique_ptr<IAudioBackend> createSimulatedAudioBackend();

// Tries ALSA hardware duplex first (unless preferSimulated); falls back to Simulated 48 kHz clock
std::unique_ptr<IAudioBackend> createAutoAudioBackend(Engine& engine, const AudioConfig& cfg);

} // namespace biomus
