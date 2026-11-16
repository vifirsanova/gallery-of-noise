#include "core/DroneSynth.hpp"
#include <cmath>

namespace biomus {

namespace {
inline float polyBlep(double t, double dt) noexcept {
    if (t < dt) {
        t /= dt;
        return static_cast<float>(t + t - t * t - 1.0);
    }
    if (t > 1.0 - dt) {
        t = (t - 1.0) / dt;
        return static_cast<float>(t * t + t + t + 1.0);
    }
    return 0.0f;
}
} // namespace

void DroneGenerator::prepare(double sampleRate) noexcept {
    sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    noiseFilter_.prepare(sampleRate_);
    noiseFilter_.setCutoffAndQ(950.0f, 1.1f);
    reset();
}

void DroneGenerator::reset() noexcept {
    phase_ = 0.0;
    subPhase_ = 0.25;
    rngState_ = 0x9E3779B9u;
    noiseFilter_.reset();
    noiseFilter_.setCutoffAndQ(950.0f, 1.1f);
}

void DroneGenerator::generate(float* out, int frames, const float* feedbackIn) noexcept {
    const double dt = static_cast<double>(freqHz_) / sampleRate_;
    const double dtDetune = static_cast<double>(freqHz_ * 1.004f) / sampleRate_;

    for (int i = 0; i < frames; ++i) {
        // Primary 55 Hz PolyBLEP sawtooth + subtle detuned unison saw for rich analog drone body
        float saw1 = static_cast<float>(2.0 * phase_ - 1.0) - polyBlep(phase_, dt);
        float saw2 = static_cast<float>(2.0 * subPhase_ - 1.0) - polyBlep(subPhase_, dtDetune);

        phase_ += dt;
        if (phase_ >= 1.0) phase_ -= 1.0;
        subPhase_ += dtDetune;
        if (subPhase_ >= 1.0) subPhase_ -= 1.0;

        // Filtered white noise bed (Spec 3.6)
        const float w = nextWhite();
        const float filteredNoise = noiseFilter_.processSample(w, TptStateVariableFilter::Mode::LowPass);

        float sig = 0.36f * saw1 + 0.18f * saw2 + 0.22f * filteredNoise;
        if (feedbackIn) {
            sig += feedbackIn[i];
        }
        out[i] = sig;
    }
}

} // namespace biomus
