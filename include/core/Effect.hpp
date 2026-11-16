#pragma once

namespace biomus {

// ============================================================================
// Unified DSP Effect Interface (Spec Section 4.2)
// ============================================================================
class Effect {
public:
    virtual ~Effect() = default;
    virtual void prepare(double sampleRate, int maxBlockSize) = 0;
    virtual void process(float* buffer, int frames) = 0;
    virtual void reset() = 0;

    virtual void setParameters(float x, float y, float knob0 = 0.0f, float knob1 = 0.0f) noexcept {
        x_ = x;
        y_ = y;
        knob0_ = knob0;
        knob1_ = knob1;
    }

protected:
    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 256;
    float x_ = 0.5f;
    float y_ = 0.5f;
    float knob0_ = 0.0f;
    float knob1_ = 0.0f;
};

} // namespace biomus
