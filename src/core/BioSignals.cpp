#include "core/BioSignals.hpp"
#include <algorithm>
#include <cmath>

namespace biomus {

namespace {
constexpr double kTwoPi = 6.28318530717958647692;
inline float clamp01(float v) noexcept {
    return (v < 0.0f) ? 0.0f : ((v > 1.0f) ? 1.0f : v);
}
} // namespace

void BioSignalGenerator::advance(int frames, BioState& bio) noexcept {
    if (frames <= 0) return;
    const double dt = static_cast<double>(frames) / sampleRate_;
    timeSec_ += dt;

    // Organic low-passed micro-jitter for breath
    smoothNoise_ = 0.94f * smoothNoise_ + 0.06f * nextWhite();

    if (!bio.externalBreath) {
        const float b = 0.5f + 0.45f * static_cast<float>(std::sin(timeSec_ * kTwoPi * 0.20))
                      + 0.05f * smoothNoise_;
        bio.breath = clamp01(b);
    }

    if (!bio.externalHeart) {
        if (bio.bpm <= 0) bio.bpm = 72;
        const double beats = timeSec_ * (static_cast<double>(bio.bpm) / 60.0);
        const float phase = static_cast<float>(beats - std::floor(beats));
        bio.heart = (phase < 0.10f)
                        ? 1.0f
                        : std::max(0.0f, 0.15f - (phase - 0.10f) * 0.20f);
    }

    if (!bio.externalEye) {
        const float ex = 0.5f + 0.35f * static_cast<float>(std::sin(timeSec_ * 0.30))
                       + 0.05f * static_cast<float>(std::sin(timeSec_ * 3.0));
        const float ey = 0.5f + 0.20f * static_cast<float>(std::sin(timeSec_ * 0.17 + 1.0));
        bio.eyeX = clamp01(ex);
        bio.eyeY = clamp01(ey);
    }
}

} // namespace biomus
