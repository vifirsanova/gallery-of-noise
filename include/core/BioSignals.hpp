#pragma once

#include "core/State.hpp"
#include <cstdint>

namespace biomus {

// ============================================================================
// BioSignalGenerator (Spec Sections 3.2, 3.3, 3.4)
// Generates organic mockups for Breath, Heart (BPM), EyeX, EyeY in [0..1]
// while seamlessly passing through any channels driven by real sensors.
// ============================================================================
class BioSignalGenerator {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 48000.0;
        reset();
    }

    void reset() noexcept {
        timeSec_ = 0.0;
        rngState_ = 0x12345678u;
        smoothNoise_ = 0.0f;
    }

    void advance(int frames, BioState& bio) noexcept;

    double elapsedSeconds() const noexcept { return timeSec_; }

private:
    float nextWhite() noexcept {
        // xorshift32 — deterministic, 3 instructions, zero allocations
        uint32_t x = rngState_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rngState_ = x;
        return static_cast<float>(static_cast<int32_t>(x)) * (1.0f / 2147483648.0f);
    }

    double sampleRate_ = 48000.0;
    double timeSec_ = 0.0;
    uint32_t rngState_ = 0x12345678u;
    float smoothNoise_ = 0.0f;
};

} // namespace biomus
