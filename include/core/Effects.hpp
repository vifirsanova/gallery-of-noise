#pragma once

#include "core/Effect.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace biomus {

// ============================================================================
// Topology-Preserving Transform (Cytomic SVF) — unconditionally stable
// ============================================================================
class TptStateVariableFilter {
public:
    enum class Mode : uint8_t { LowPass = 0, HighPass, BandPass };

    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 48000.0;
        reset();
    }

    void reset() noexcept {
        ic1eq_ = 0.0f;
        ic2eq_ = 0.0f;
    }

    void setCutoffAndQ(float cutoffHz, float q) noexcept;

    float processSample(float x, Mode mode) noexcept;

private:
    double sampleRate_ = 48000.0;
    float g_ = 0.1f;
    float k_ = 1.414f;
    float a1_ = 0.0f;
    float a2_ = 0.0f;
    float a3_ = 0.0f;
    float ic1eq_ = 0.0f;
    float ic2eq_ = 0.0f;
};

// ============================================================================
// 1. PAN Effect (Voice chain slot 0)
//    X: stereo pan position [0=L, 0.5=C, 1=R] (exposed for stereo output stage)
//    Y: level trim around unity [0.5 = 1.0x unity, 0..1 -> 0.4x .. 1.6x]
// ============================================================================
class PanEffect final : public Effect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int frames) override;
    void reset() override;

    float stereoPan() const noexcept { return x_; }
};

// ============================================================================
// 2. HPF Effect (2-pole TPT High-Pass Filter)
//    X: cutoff [20 .. 2000 Hz exponential]
//    Y: resonance accent around knob Q
//    Knob0: Q [0.1 .. 5.0]
// ============================================================================
class HpfEffect final : public Effect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int frames) override;
    void reset() override;

private:
    TptStateVariableFilter svf_;
};

// ============================================================================
// 3. COMP Effect (RMS/Peak Envelope Feed-Forward Vocal Compressor)
//    X: threshold amount [0 = 0 dB, 1 = -36 dB]
//    Y: ratio & auto-makeup [0 = 1.2:1, 1 = 12:1]
//    Knob0: attack [0.001 .. 0.1 s], Knob1: release [0.05 .. 1.0 s]
// ============================================================================
class CompEffect final : public Effect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int frames) override;
    void reset() override;

private:
    float env_ = 0.0f;
};

// ============================================================================
// 4. FOLD Effect (Buchla-style Sinusoidal Wavefolder)
//    X: fold drive [1 .. 10x]
//    Y: dry/wet mix [0 .. 1]
// ============================================================================
class FoldEffect final : public Effect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int frames) override;
    void reset() override;
};

// ============================================================================
// 5. BITCRUSH Effect (Sample-Rate Decimator + Bit-Depth Quantizer)
//    X: crush intensity (decimation 1..32x + bit reduction 14..3 bits)
//    Y: dry/wet mix [0 .. 1]
// ============================================================================
class BitcrushEffect final : public Effect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int frames) override;
    void reset() override;

private:
    float heldSample_ = 0.0f;
    float phase_ = 0.0f;
};

// ============================================================================
// 6. RINGMOD Effect (Sine Carrier Ring Modulator)
//    X: carrier frequency [30 .. 2000 Hz exponential]
//    Y: dry/wet mix [0 .. 1]
// ============================================================================
class RingModEffect final : public Effect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int frames) override;
    void reset() override;

private:
    double phase_ = 0.0;
};

// ============================================================================
// 7. CHORUS Effect (Dual-Tap Modulated Delay Line Chorus)
//    X: LFO rate [0.1 .. 6.0 Hz]
//    Y: modulation depth [0 .. 1]
//    Knob0: mix [0 .. 1], Knob1: feedback [0 .. 0.6]
// ============================================================================
class ChorusEffect final : public Effect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int frames) override;
    void reset() override;

private:
    std::vector<float> delayBuf_;
    size_t writePos_ = 0;
    double lfoPhase_ = 0.0;
};

// ============================================================================
// 8. REVERB Effect (4-Comb + 2-Allpass Schroeder-Moorer Reverb with Damping)
//    X: decay / feedback [0.2 .. 0.96]
//    Y: wet mix [0 .. 0.85]
//    Knob0: size [1 .. 6], Knob1: damping LPF [500 .. 12000 Hz]
// ============================================================================
class ReverbEffect final : public Effect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int frames) override;
    void reset() override;

private:
    struct Comb {
        std::vector<float> buf;
        size_t pos = 0;
        size_t activeLen = 1;
        float filterStore = 0.0f;
    };
    struct Allpass {
        std::vector<float> buf;
        size_t pos = 0;
        size_t activeLen = 1;
    };
    std::array<Comb, 4> combs_;
    std::array<Allpass, 2> allpasses_;
};

// ============================================================================
// 9. SHIMMER Effect (Dual-Grain Hann Octave-Up Pitch Shifter + Diffused Tank)
//    X: shimmer feedback / sustain [0 .. 0.86]
//    Y: wet mix [0 .. 0.75]
//    Knob0: grain window time [0.05 .. 0.6 s]
// ============================================================================
class ShimmerEffect final : public Effect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int frames) override;
    void reset() override;

private:
    std::vector<float> grainBuf_;
    std::vector<float> diffBuf1_;
    std::vector<float> diffBuf2_;
    size_t writePos_ = 0;
    size_t diffPos1_ = 0;
    size_t diffPos2_ = 0;
    double grainPhase_ = 0.0;
    float feedbackSample_ = 0.0f;
    float lpState_ = 0.0f;
};

// ============================================================================
// 10. DELAY Effect (Filtered Feedback Delay with Soft Tape Saturation)
//     X: delay time [0.02 .. 1.25 s]
//     Y: feedback [0 .. 0.92]
//     Knob0: tone LPF [200 .. 12000 Hz], Knob1: mix [0 .. 1]
// ============================================================================
class DelayEffect final : public Effect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int frames) override;
    void reset() override;

private:
    std::vector<float> delayBuf_;
    size_t writePos_ = 0;
    float smoothedDelaySamples_ = 12000.0f;
    float toneState_ = 0.0f;
};

// ============================================================================
// 11. LPF Effect (2-Pole TPT Low-Pass Filter for Voice & Drone)
//     X: cutoff [60 .. 18000 Hz exponential for Voice, 50 .. 9000 Hz for Drone]
//     Y: resonance [0.1 .. 0.92] (or offset around knob Q)
//     Knob0: Q [0.1 .. 10.0] (when > 0)
// ============================================================================
class LpfEffect final : public Effect {
public:
    explicit LpfEffect(bool droneRange = false) noexcept : droneRange_(droneRange) {}
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int frames) override;
    void reset() override;

private:
    bool droneRange_ = false;
    TptStateVariableFilter svf_;
};

// ============================================================================
// 12. DRIVE Effect (Asymmetric Warm Overdrive + Post Tone Filter, Drone chain)
//     X: drive gain [1 .. 22x]
//     Y: tone [800 .. 10000 Hz] & saturation blend
// ============================================================================
class DriveEffect final : public Effect {
public:
    void prepare(double sampleRate, int maxBlockSize) override;
    void process(float* buffer, int frames) override;
    void reset() override;

private:
    TptStateVariableFilter toneFilter_;
    float dcX1_ = 0.0f;
    float dcY1_ = 0.0f;
};

} // namespace biomus
