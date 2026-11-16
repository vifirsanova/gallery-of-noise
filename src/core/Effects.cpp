#include "core/Effects.hpp"
#include <algorithm>
#include <cmath>

namespace biomus {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr double kTwoPiD = 6.28318530717958647692;

inline float clampf(float v, float lo, float hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

inline float flushDenorm(float v) noexcept {
    return (std::fabs(v) < 1e-20f) ? 0.0f : v;
}

inline float readFractional(const std::vector<float>& buf, size_t writePos, float delaySamples) noexcept {
    const size_t cap = buf.size();
    if (cap < 2) return 0.0f;
    const float maxDel = static_cast<float>(cap - 2);
    const float d = clampf(delaySamples, 1.0f, maxDel);
    const size_t dInt = static_cast<size_t>(d);
    const float frac = d - static_cast<float>(dInt);
    const size_t idx0 = (writePos + cap - dInt) % cap;
    const size_t idx1 = (idx0 + cap - 1) % cap;
    return buf[idx0] * (1.0f - frac) + buf[idx1] * frac;
}
} // namespace

// ============================================================================
// TptStateVariableFilter
// ============================================================================
void TptStateVariableFilter::setCutoffAndQ(float cutoffHz, float q) noexcept {
    const float maxFc = static_cast<float>(sampleRate_ * 0.47);
    const float fc = clampf(cutoffHz, 15.0f, maxFc);
    const float safeQ = clampf(q, 0.1f, 20.0f);
    g_ = std::tan(kPi * fc / static_cast<float>(sampleRate_));
    k_ = 1.0f / safeQ;
    a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
    a2_ = g_ * a1_;
    a3_ = g_ * a2_;
}

float TptStateVariableFilter::processSample(float x, Mode mode) noexcept {
    const float v3 = x - ic2eq_;
    const float v1 = a1_ * ic1eq_ + a2_ * v3;
    const float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;
    ic1eq_ = flushDenorm(2.0f * v1 - ic1eq_);
    ic2eq_ = flushDenorm(2.0f * v2 - ic2eq_);

    switch (mode) {
        case Mode::LowPass:  return v2;
        case Mode::BandPass: return v1;
        case Mode::HighPass: return x - k_ * v1 - v2;
    }
    return v2;
}

// ============================================================================
// 1. PanEffect
// ============================================================================
void PanEffect::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
}

void PanEffect::reset() {}

void PanEffect::process(float* buffer, int frames) {
    // At y == 0.5 -> exact unity gain 1.0f (transparent in CLEAN/VOICE presets)
    const float gain = 0.4f + 1.2f * clampf(y_, 0.0f, 1.0f);
    for (int i = 0; i < frames; ++i) {
        buffer[i] *= gain;
    }
}

// ============================================================================
// 2. HpfEffect
// ============================================================================
void HpfEffect::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    svf_.prepare(sampleRate);
}

void HpfEffect::reset() {
    svf_.reset();
}

void HpfEffect::process(float* buffer, int frames) {
    const float cutoff = 20.0f * std::pow(100.0f, clampf(x_, 0.0f, 1.0f)); // 20 .. 2000 Hz
    const float baseQ = (knob0_ > 0.05f) ? knob0_ : 0.707f;
    const float qMod = baseQ * (0.5f + clampf(y_, 0.0f, 1.0f)); // y=0.5 -> baseQ
    svf_.setCutoffAndQ(cutoff, qMod);
    for (int i = 0; i < frames; ++i) {
        buffer[i] = svf_.processSample(buffer[i], TptStateVariableFilter::Mode::HighPass);
    }
}

// ============================================================================
// 3. CompEffect
// ============================================================================
void CompEffect::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    reset();
}

void CompEffect::reset() {
    env_ = 0.0f;
}

void CompEffect::process(float* buffer, int frames) {
    const float threshDb = -36.0f * clampf(x_, 0.0f, 1.0f);
    const float ratio = 1.2f + 10.8f * clampf(y_, 0.0f, 1.0f);
    const float attackSec = clampf(knob0_ > 0.0f ? knob0_ : 0.003f, 0.001f, 0.1f);
    const float releaseSec = clampf(knob1_ > 0.0f ? knob1_ : 0.25f, 0.05f, 1.0f);

    const float attCoeff = std::exp(-1.0f / (static_cast<float>(sampleRate_) * attackSec));
    const float relCoeff = std::exp(-1.0f / (static_cast<float>(sampleRate_) * releaseSec));
    const float slope = 1.0f - (1.0f / ratio);
    const float makeupDb = -threshDb * slope * 0.45f;
    const float makeupLin = std::pow(10.0f, makeupDb / 20.0f);

    for (int i = 0; i < frames; ++i) {
        const float absIn = std::fabs(buffer[i]);
        if (absIn > env_) {
            env_ = attCoeff * env_ + (1.0f - attCoeff) * absIn;
        } else {
            env_ = relCoeff * env_ + (1.0f - relCoeff) * absIn;
        }
        env_ = flushDenorm(env_);

        float gainLin = makeupLin;
        if (env_ > 1e-6f) {
            const float envDb = 20.0f * std::log10(env_);
            if (envDb > threshDb) {
                const float grDb = -(envDb - threshDb) * slope;
                gainLin *= std::pow(10.0f, grDb / 20.0f);
            }
        }
        buffer[i] = std::tanh(buffer[i] * gainLin);
    }
}

// ============================================================================
// 4. FoldEffect
// ============================================================================
void FoldEffect::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
}

void FoldEffect::reset() {}

void FoldEffect::process(float* buffer, int frames) {
    const float drive = 1.0f + 9.0f * clampf(x_, 0.0f, 1.0f);
    const float wet = clampf(y_, 0.0f, 1.0f);
    for (int i = 0; i < frames; ++i) {
        const float dry = buffer[i];
        const float driven = dry * drive;
        const float folded = 0.75f * std::sin(driven * 1.5707963f) + 0.25f * std::tanh(driven);
        buffer[i] = (1.0f - wet) * dry + wet * folded;
    }
}

// ============================================================================
// 5. BitcrushEffect
// ============================================================================
void BitcrushEffect::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    reset();
}

void BitcrushEffect::reset() {
    heldSample_ = 0.0f;
    phase_ = 0.0f;
}

void BitcrushEffect::process(float* buffer, int frames) {
    const float amt = clampf(x_, 0.0f, 1.0f);
    const float wet = clampf(y_, 0.0f, 1.0f);
    const float decimStep = 1.0f / (1.0f + 31.0f * amt * amt); // 1 .. 1/32
    const float bits = 14.0f - 11.0f * amt;                    // 14 .. 3 bits
    const float quantLevels = std::pow(2.0f, bits);
    const float invQuant = 1.0f / quantLevels;

    for (int i = 0; i < frames; ++i) {
        const float dry = buffer[i];
        phase_ += decimStep;
        if (phase_ >= 1.0f) {
            phase_ -= std::floor(phase_);
            heldSample_ = std::round(dry * quantLevels) * invQuant;
        }
        buffer[i] = (1.0f - wet) * dry + wet * heldSample_;
    }
}

// ============================================================================
// 6. RingModEffect
// ============================================================================
void RingModEffect::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    reset();
}

void RingModEffect::reset() {
    phase_ = 0.0;
}

void RingModEffect::process(float* buffer, int frames) {
    const float freqHz = 30.0f * std::pow(66.6667f, clampf(x_, 0.0f, 1.0f)); // 30 .. 2000 Hz
    const float wet = clampf(y_, 0.0f, 1.0f);
    const double phaseInc = static_cast<double>(freqHz) / sampleRate_;

    for (int i = 0; i < frames; ++i) {
        const float carrier = static_cast<float>(std::sin(kTwoPiD * phase_));
        phase_ += phaseInc;
        if (phase_ >= 1.0) phase_ -= 1.0;
        const float dry = buffer[i];
        buffer[i] = (1.0f - wet) * dry + wet * (dry * carrier);
    }
}

// ============================================================================
// 7. ChorusEffect
// ============================================================================
void ChorusEffect::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    const size_t bufLen = static_cast<size_t>(sampleRate * 0.065) + 16;
    delayBuf_.assign(bufLen, 0.0f);
    reset();
}

void ChorusEffect::reset() {
    std::fill(delayBuf_.begin(), delayBuf_.end(), 0.0f);
    writePos_ = 0;
    lfoPhase_ = 0.0;
}

void ChorusEffect::process(float* buffer, int frames) {
    if (delayBuf_.empty()) return;
    const float rateHz = 0.1f + 5.9f * clampf(x_, 0.0f, 1.0f);
    const float depth = clampf(y_, 0.0f, 1.0f);
    const float mix = clampf(knob0_, 0.0f, 1.0f);
    const float fb = clampf(knob1_, 0.0f, 0.6f);

    const float baseDelay = static_cast<float>(sampleRate_ * 0.014); // 14 ms
    const float modSpan = static_cast<float>(sampleRate_ * 0.008) * depth;
    const double phaseInc = static_cast<double>(rateHz) / sampleRate_;
    const size_t cap = delayBuf_.size();

    for (int i = 0; i < frames; ++i) {
        const float s1 = static_cast<float>(std::sin(kTwoPiD * lfoPhase_));
        const float s2 = static_cast<float>(std::cos(kTwoPiD * lfoPhase_));
        lfoPhase_ += phaseInc;
        if (lfoPhase_ >= 1.0) lfoPhase_ -= 1.0;

        const float tap1 = readFractional(delayBuf_, writePos_, baseDelay + modSpan * s1);
        const float tap2 = readFractional(delayBuf_, writePos_, baseDelay * 1.31f + modSpan * s2);
        const float wetSig = 0.5f * (tap1 + tap2);

        const float dry = buffer[i];
        delayBuf_[writePos_] = flushDenorm(std::tanh(dry + fb * tap1));
        writePos_ = (writePos_ + 1) % cap;

        buffer[i] = (1.0f - mix) * dry + mix * wetSig;
    }
}

// ============================================================================
// 8. ReverbEffect
// ============================================================================
void ReverbEffect::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;

    // Reference Schroeder-Moorer tunings at 48 kHz scaled up to max room size = 6
    constexpr int kBaseComb48k[4] = {1499, 1613, 1747, 1879};
    constexpr int kBaseAp48k[2] = {241, 557};
    const double srScale = sampleRate / 48000.0;

    for (size_t c = 0; c < 4; ++c) {
        const size_t maxLen = static_cast<size_t>(kBaseComb48k[c] * srScale * 2.5) + 16;
        combs_[c].buf.assign(maxLen, 0.0f);
        combs_[c].activeLen = static_cast<size_t>(kBaseComb48k[c] * srScale);
    }
    for (size_t a = 0; a < 2; ++a) {
        const size_t maxLen = static_cast<size_t>(kBaseAp48k[a] * srScale * 2.0) + 16;
        allpasses_[a].buf.assign(maxLen, 0.0f);
        allpasses_[a].activeLen = static_cast<size_t>(kBaseAp48k[a] * srScale);
    }
    reset();
}

void ReverbEffect::reset() {
    for (auto& c : combs_) {
        std::fill(c.buf.begin(), c.buf.end(), 0.0f);
        c.pos = 0;
        c.filterStore = 0.0f;
    }
    for (auto& a : allpasses_) {
        std::fill(a.buf.begin(), a.buf.end(), 0.0f);
        a.pos = 0;
    }
}

void ReverbEffect::process(float* buffer, int frames) {
    constexpr int kBaseComb48k[4] = {1499, 1613, 1747, 1879};
    const double srScale = sampleRate_ / 48000.0;

    const float roomSize = clampf(knob0_ >= 1.0f ? knob0_ : 3.0f, 1.0f, 6.0f);
    const float sizeFactor = 0.55f + 0.30f * roomSize; // 0.85 .. 2.35
    for (size_t c = 0; c < 4; ++c) {
        if (combs_[c].buf.empty()) return;
        const size_t desired = static_cast<size_t>(kBaseComb48k[c] * srScale * sizeFactor);
        combs_[c].activeLen = std::clamp<size_t>(desired, 64, combs_[c].buf.size() - 1);
        if (combs_[c].pos >= combs_[c].activeLen) combs_[c].pos = 0;
    }

    const float feedback = 0.25f + 0.69f * clampf(x_, 0.0f, 1.0f); // 0.25 .. 0.94
    const float wet = 0.85f * clampf(y_, 0.0f, 1.0f);
    const float dampHz = clampf(knob1_ >= 500.0f ? knob1_ : 8000.0f, 500.0f, 12000.0f);
    const float dampCoeff = std::exp(-2.0f * kPi * dampHz / static_cast<float>(sampleRate_));
    const float dampInv = 1.0f - dampCoeff;

    for (int i = 0; i < frames; ++i) {
        const float dry = buffer[i];
        const float inScaled = dry * 0.22f;
        float acc = 0.0f;

        for (auto& c : combs_) {
            const float out = c.buf[c.pos];
            c.filterStore = flushDenorm(out * dampInv + c.filterStore * dampCoeff);
            c.buf[c.pos] = flushDenorm(inScaled + c.filterStore * feedback);
            if (++c.pos >= c.activeLen) c.pos = 0;
            acc += out;
        }

        // Series Schroeder allpass diffusers
        for (auto& a : allpasses_) {
            const float bufOut = a.buf[a.pos];
            const float apIn = acc + bufOut * 0.5f;
            a.buf[a.pos] = flushDenorm(apIn);
            acc = bufOut - apIn * 0.5f;
            if (++a.pos >= a.activeLen) a.pos = 0;
        }

        buffer[i] = (1.0f - wet * 0.55f) * dry + wet * acc;
    }
}

// ============================================================================
// 9. ShimmerEffect (+1 Octave Dual-Grain Pitch Shifter + Diffused Tank)
// ============================================================================
void ShimmerEffect::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    const size_t maxGrainSamples = static_cast<size_t>(sampleRate * 0.70) + 32;
    grainBuf_.assign(maxGrainSamples, 0.0f);
    diffBuf1_.assign(static_cast<size_t>(sampleRate * 0.017) + 8, 0.0f);
    diffBuf2_.assign(static_cast<size_t>(sampleRate * 0.029) + 8, 0.0f);
    reset();
}

void ShimmerEffect::reset() {
    std::fill(grainBuf_.begin(), grainBuf_.end(), 0.0f);
    std::fill(diffBuf1_.begin(), diffBuf1_.end(), 0.0f);
    std::fill(diffBuf2_.begin(), diffBuf2_.end(), 0.0f);
    writePos_ = 0;
    diffPos1_ = 0;
    diffPos2_ = 0;
    grainPhase_ = 0.0;
    feedbackSample_ = 0.0f;
    lpState_ = 0.0f;
}

void ShimmerEffect::process(float* buffer, int frames) {
    if (grainBuf_.empty()) return;
    const float fb = 0.82f * clampf(x_, 0.0f, 1.0f);
    const float wet = 0.75f * clampf(y_, 0.0f, 1.0f);
    const float winSec = clampf(knob0_ >= 0.05f ? knob0_ : 0.20f, 0.05f, 0.60f);
    const float winSamples = clampf(static_cast<float>(sampleRate_) * winSec,
                                    128.0f,
                                    static_cast<float>(grainBuf_.size() - 8));
    // For +1 octave (ratio = 2.0), delay decreases by 1 sample per sample -> phasor slope = -1 / winSamples
    const double phaseStep = 1.0 / static_cast<double>(winSamples);
    const size_t cap = grainBuf_.size();
    const size_t dCap1 = diffBuf1_.size();
    const size_t dCap2 = diffBuf2_.size();
    const float lpAlpha = std::exp(-2.0f * kPi * 5500.0f / static_cast<float>(sampleRate_));

    for (int i = 0; i < frames; ++i) {
        const float dry = buffer[i];
        grainBuf_[writePos_] = flushDenorm(std::tanh(dry + feedbackSample_ * fb));

        const double p0 = grainPhase_;
        const double p1 = (p0 + 0.5 >= 1.0) ? (p0 - 0.5) : (p0 + 0.5);

        // Decreasing delay from winSamples down to 2 samples -> doubles frequency (+12 st)
        const float del0 = 2.0f + static_cast<float>(1.0 - p0) * winSamples;
        const float del1 = 2.0f + static_cast<float>(1.0 - p1) * winSamples;

        const float w0 = 0.5f * (1.0f - static_cast<float>(std::cos(kTwoPiD * p0)));
        const float w1 = 0.5f * (1.0f - static_cast<float>(std::cos(kTwoPiD * p1)));

        const float g0 = readFractional(grainBuf_, writePos_, del0);
        const float g1 = readFractional(grainBuf_, writePos_, del1);
        float pitched = g0 * w0 + g1 * w1;

        // Diffuse + gentle high-frequency damping so shimmer feedback stays velvety
        const float d1Out = diffBuf1_[diffPos1_];
        const float d1In = pitched + 0.5f * d1Out;
        diffBuf1_[diffPos1_] = flushDenorm(d1In);
        pitched = d1Out - 0.5f * d1In;
        if (++diffPos1_ >= dCap1) diffPos1_ = 0;

        const float d2Out = diffBuf2_[diffPos2_];
        const float d2In = pitched + 0.5f * d2Out;
        diffBuf2_[diffPos2_] = flushDenorm(d2In);
        pitched = d2Out - 0.5f * d2In;
        if (++diffPos2_ >= dCap2) diffPos2_ = 0;

        lpState_ = flushDenorm((1.0f - lpAlpha) * pitched + lpAlpha * lpState_);
        feedbackSample_ = lpState_;

        writePos_ = (writePos_ + 1) % cap;
        grainPhase_ += phaseStep;
        if (grainPhase_ >= 1.0) grainPhase_ -= 1.0;

        buffer[i] = dry + wet * lpState_;
    }
}

// ============================================================================
// 10. DelayEffect
// ============================================================================
void DelayEffect::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    delayBuf_.assign(static_cast<size_t>(sampleRate * 1.5) + 16, 0.0f);
    reset();
}

void DelayEffect::reset() {
    std::fill(delayBuf_.begin(), delayBuf_.end(), 0.0f);
    writePos_ = 0;
    smoothedDelaySamples_ = static_cast<float>(sampleRate_ * 0.35);
    toneState_ = 0.0f;
}

void DelayEffect::process(float* buffer, int frames) {
    if (delayBuf_.empty()) return;
    const float targetSec = 0.02f + 1.20f * clampf(x_, 0.0f, 1.0f);
    const float targetSamples = clampf(targetSec * static_cast<float>(sampleRate_),
                                       16.0f,
                                       static_cast<float>(delayBuf_.size() - 4));
    const float fb = 0.91f * clampf(y_, 0.0f, 1.0f);
    const float toneHz = clampf(knob0_ >= 200.0f ? knob0_ : 3500.0f, 200.0f, 12000.0f);
    const float mix = clampf(knob1_, 0.0f, 1.0f);

    const float toneAlpha = std::exp(-2.0f * kPi * toneHz / static_cast<float>(sampleRate_));
    const float glideAlpha = 1.0f - std::exp(-1.0f / (static_cast<float>(sampleRate_) * 0.04f));
    const size_t cap = delayBuf_.size();

    for (int i = 0; i < frames; ++i) {
        smoothedDelaySamples_ += glideAlpha * (targetSamples - smoothedDelaySamples_);
        const float delayed = readFractional(delayBuf_, writePos_, smoothedDelaySamples_);
        toneState_ = flushDenorm((1.0f - toneAlpha) * delayed + toneAlpha * toneState_);

        const float dry = buffer[i];
        delayBuf_[writePos_] = flushDenorm(std::tanh(dry + toneState_ * fb));
        writePos_ = (writePos_ + 1) % cap;

        buffer[i] = (1.0f - mix * 0.5f) * dry + mix * toneState_;
    }
}

// ============================================================================
// 11. LpfEffect
// ============================================================================
void LpfEffect::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    svf_.prepare(sampleRate);
}

void LpfEffect::reset() {
    svf_.reset();
}

void LpfEffect::process(float* buffer, int frames) {
    float cutoff = 1000.0f;
    float q = 0.707f;
    if (droneRange_) {
        cutoff = 55.0f * std::pow(150.0f, clampf(x_, 0.0f, 1.0f)); // 55 .. 8250 Hz
        q = 0.55f + 6.5f * clampf(y_, 0.0f, 1.0f);
    } else {
        cutoff = 80.0f * std::pow(225.0f, clampf(x_, 0.0f, 1.0f)); // 80 .. 18000 Hz
        const float baseQ = (knob0_ > 0.05f) ? knob0_ : 0.707f;
        q = baseQ * (0.5f + clampf(y_, 0.0f, 1.0f)); // y=0.5 -> baseQ
    }
    svf_.setCutoffAndQ(cutoff, q);
    for (int i = 0; i < frames; ++i) {
        buffer[i] = svf_.processSample(buffer[i], TptStateVariableFilter::Mode::LowPass);
    }
}

// ============================================================================
// 12. DriveEffect
// ============================================================================
void DriveEffect::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = sampleRate;
    maxBlockSize_ = maxBlockSize;
    toneFilter_.prepare(sampleRate);
    reset();
}

void DriveEffect::reset() {
    toneFilter_.reset();
    dcX1_ = 0.0f;
    dcY1_ = 0.0f;
}

void DriveEffect::process(float* buffer, int frames) {
    const float drive = 1.0f + 20.0f * clampf(x_, 0.0f, 1.0f);
    const float toneCutoff = 600.0f * std::pow(16.0f, clampf(y_, 0.0f, 1.0f)); // 600 .. 9600 Hz
    toneFilter_.setCutoffAndQ(toneCutoff, 0.75f);

    for (int i = 0; i < frames; ++i) {
        const float in = buffer[i];
        const float pre = in * drive;
        // Asymmetric tube-like waveshaper + DC blocker
        const float shaped = std::tanh(pre + 0.12f * pre * pre);
        const float dcOut = shaped - dcX1_ + 0.995f * dcY1_;
        dcX1_ = shaped;
        dcY1_ = flushDenorm(dcOut);
        buffer[i] = toneFilter_.processSample(dcOut * 0.8f, TptStateVariableFilter::Mode::LowPass);
    }
}

} // namespace biomus
