#pragma once

#include "core/Effects.hpp"
#include <cstdint>

namespace biomus {

// ============================================================================
// DroneGenerator (Spec Section 3.6)
// Band-limited PolyBLEP 55 Hz Sawtooth + Filtered White Noise mixed together.
// Also accepts external feedback signal from Master bus.
// ============================================================================
class DroneGenerator {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Generates `frames` of drone source mixed with `feedbackIn` (if non-null)
    void generate(float* out, int frames, const float* feedbackIn = nullptr) noexcept;

    void setFrequency(float hz) noexcept { freqHz_ = hz; }

private:
    float nextWhite() noexcept {
        uint32_t x = rngState_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rngState_ = x;
        return static_cast<float>(static_cast<int32_t>(x)) * (1.0f / 2147483648.0f);
    }

    double sampleRate_ = 48000.0;
    double phase_ = 0.0;
    double subPhase_ = 0.0;
    float freqHz_ = 55.0f; // A1 = 55 Hz per Spec 3.6
    uint32_t rngState_ = 0x9E3779B9u;
    TptStateVariableFilter noiseFilter_;
};

} // namespace biomus
