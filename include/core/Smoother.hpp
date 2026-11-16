#pragma once

#include <cmath>

namespace biomus {

// ============================================================================
// One-Pole Parameter Smoother (Time constant tau ~ 30 ms by default, Spec 4.4)
// ============================================================================
class OnePoleSmoother {
public:
    void prepare(double sampleRate, float tauSeconds = 0.030f) noexcept {
        if (sampleRate <= 0.0 || tauSeconds <= 0.0f) {
            coeff_ = 1.0f;
        } else {
            coeff_ = static_cast<float>(1.0 - std::exp(-1.0 / (sampleRate * static_cast<double>(tauSeconds))));
        }
    }

    void reset(float initialValue = 0.0f) noexcept {
        current_ = initialValue;
        target_ = initialValue;
    }

    void setTarget(float target) noexcept {
        target_ = target;
    }

    float next() noexcept {
        current_ += coeff_ * (target_ - current_);
        return current_;
    }

    // Advance by N samples analytically (for block-rate updates when appropriate)
    float advance(int samples) noexcept {
        if (samples <= 0) return current_;
        if (samples == 1) return next();
        const float retain = std::pow(1.0f - coeff_, static_cast<float>(samples));
        current_ = target_ + retain * (current_ - target_);
        return current_;
    }

    float current() const noexcept { return current_; }
    float target() const noexcept { return target_; }

private:
    float coeff_ = 1.0f;
    float current_ = 0.0f;
    float target_ = 0.0f;
};

} // namespace biomus
