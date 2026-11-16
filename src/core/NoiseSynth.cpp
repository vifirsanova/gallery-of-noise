#include "core/NoiseSynth.hpp"
#include <algorithm>
#include <cmath>

namespace biomus {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr double kTwoPiD = 6.28318530717958647692;

inline float clampf(float v, float lo, float hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}
} // namespace

void NoiseVoiceDsp::prepare(double sampleRate, uint32_t seed) noexcept {
    sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    svf_.prepare(sampleRate_);
    reset(seed);
}

void NoiseVoiceDsp::reset(uint32_t seed) noexcept {
    rng_ = (seed != 0) ? seed : 0xCAFEBABEu;
    b0_ = b1_ = b2_ = 0.0f;
    brown_ = 0.0f;
    heldNoise_ = 0.0f;
    holdPhase_ = 1.0f;
    envStage_ = 0;
    ampEnv_ = 0.0f;
    modEnv_ = 0.0f;
    lfo1Phase_ = 0.0;
    lfo2Phase_ = 0.0;
    activeParams_ = NoiseVoiceParams{};
    usingLockedParams_ = false;
    svf_.reset();
}

void NoiseVoiceDsp::trigger(const NoiseVoiceParams& effectiveParams, bool hasLocks) noexcept {
    activeParams_ = effectiveParams;
    usingLockedParams_ = hasLocks;
    envStage_ = 1; // Attack stage
}

void NoiseVoiceDsp::updateUnlockedBase(const NoiseVoiceParams& baseParams) noexcept {
    activeParams_ = baseParams;
    usingLockedParams_ = false;
}

float NoiseVoiceDsp::nextNoiseSample(NoiseType type, float pitchHoldStep) noexcept {
    holdPhase_ += pitchHoldStep;
    if (holdPhase_ >= 1.0f) {
        holdPhase_ -= std::floor(holdPhase_);
        const float w = nextWhite();
        switch (type) {
            case NoiseType::White:
                heldNoise_ = w;
                break;
            case NoiseType::Pink:
                // Paul Kellet economy pink filter
                b0_ = 0.99765f * b0_ + w * 0.0990460f;
                b1_ = 0.96300f * b1_ + w * 0.2965164f;
                b2_ = 0.57000f * b2_ + w * 1.0526913f;
                heldNoise_ = (b0_ + b1_ + b2_ + w * 0.1848f) * 0.22f;
                break;
            case NoiseType::Brown:
                brown_ = clampf((brown_ + 0.04f * w) * 0.985f, -1.0f, 1.0f);
                heldNoise_ = brown_ * 1.8f;
                break;
        }
    }
    return heldNoise_;
}

void NoiseVoiceDsp::render(int frames,
                           const NoiseVoiceParams& currentBaseParams,
                           float* accumMono,
                           float* accumL,
                           float* accumR) noexcept {
    if (frames <= 0) return;

    // If not holding step-locked overrides, track live edits to base voice params
    if (!usingLockedParams_) {
        activeParams_ = currentBaseParams;
    }

    if (envStage_ == 0 && ampEnv_ <= 1e-6f) {
        // Advance LFO phases even while silent so free-running modulation stays natural
        const double dt = static_cast<double>(frames) / sampleRate_;
        lfo1Phase_ += activeParams_.lfo1.rate * dt;
        lfo1Phase_ -= std::floor(lfo1Phase_);
        lfo2Phase_ += activeParams_.lfo2.rate * dt;
        lfo2Phase_ -= std::floor(lfo2Phase_);
        return;
    }

    const NoiseVoiceParams& p = activeParams_;
    const float attSamples = std::max(1.0f, p.ampAttack * static_cast<float>(sampleRate_));
    const float decSamples = std::max(8.0f, p.ampDecay * static_cast<float>(sampleRate_));
    const float attInc = 1.0f / attSamples;
    const float decMul = std::exp(-4.6f / decSamples); // reaches -40 dB in ampDecay seconds
    const float modDecMul = std::exp(-6.0f / decSamples);

    const double lfo1Inc = static_cast<double>(clampf(p.lfo1.rate, 0.05f, 20.0f)) / sampleRate_;
    const double lfo2Inc = static_cast<double>(clampf(p.lfo2.rate, 0.05f, 20.0f)) / sampleRate_;
    const float baseQ = 0.55f + 9.0f * clampf(p.resonance, 0.0f, 0.95f);

    for (int i = 0; i < frames; ++i) {
        // Advance AD envelope
        if (envStage_ == 1) {
            ampEnv_ += attInc;
            modEnv_ = ampEnv_;
            if (ampEnv_ >= 1.0f) {
                ampEnv_ = 1.0f;
                modEnv_ = 1.0f;
                envStage_ = 2;
            }
        } else if (envStage_ == 2) {
            ampEnv_ *= decMul;
            modEnv_ *= modDecMul;
            if (ampEnv_ < 1e-5f) {
                ampEnv_ = 0.0f;
                modEnv_ = 0.0f;
                envStage_ = 0;
            }
        }

        const float lfo1Val = static_cast<float>(std::sin(kTwoPiD * lfo1Phase_)) * p.lfo1.depth;
        const float lfo2Val = static_cast<float>(std::sin(kTwoPiD * lfo2Phase_)) * p.lfo2.depth;
        lfo1Phase_ += lfo1Inc;
        if (lfo1Phase_ >= 1.0) lfo1Phase_ -= 1.0;
        lfo2Phase_ += lfo2Inc;
        if (lfo2Phase_ >= 1.0) lfo2Phase_ -= 1.0;

        float cutoffOctaves = p.modEnvAmount * modEnv_ * 3.0f;
        float pitchStep = 1.0f;
        float panMod = p.pan;

        auto applyLfo = [&](LfoTarget tgt, float v) {
            switch (tgt) {
                case LfoTarget::Cutoff:
                    cutoffOctaves += v * 2.5f;
                    break;
                case LfoTarget::Pitch:
                    // Clocked noise decimation modulation (0.12 .. 1.0x sample rate)
                    pitchStep *= std::pow(2.0f, (v - 0.5f) * 2.5f);
                    break;
                case LfoTarget::Pan:
                    panMod += v;
                    break;
            }
        };
        applyLfo(p.lfo1.target, lfo1Val);
        applyLfo(p.lfo2.target, lfo2Val);

        pitchStep = clampf(pitchStep, 0.05f, 1.0f);
        const float modCutoff = clampf(p.cutoff * std::pow(2.0f, cutoffOctaves), 20.0f, 16000.0f);
        svf_.setCutoffAndQ(modCutoff, baseQ);

        const float raw = nextNoiseSample(p.type, pitchStep);
        const float filtered = svf_.processSample(raw, TptStateVariableFilter::Mode::BandPass) * 0.55f
                             + svf_.processSample(raw, TptStateVariableFilter::Mode::LowPass) * 0.65f;
        const float sample = filtered * ampEnv_ * clampf(p.gain, 0.0f, 1.0f) * 0.55f;

        const float pan01 = 0.5f * (clampf(panMod, -1.0f, 1.0f) + 1.0f);
        const float angle = pan01 * (0.5f * kPi);
        const float gainL = std::cos(angle);
        const float gainR = std::sin(angle);

        if (accumMono) accumMono[i] += sample;
        if (accumL) accumL[i] += sample * gainL;
        if (accumR) accumR[i] += sample * gainR;
    }
}

// ============================================================================
// NoiseSynthEngine
// ============================================================================
void NoiseSynthEngine::prepare(double sampleRate) noexcept {
    sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    for (size_t t = 0; t < kNumNoiseTracks; ++t) {
        voices_[t].prepare(sampleRate_, static_cast<uint32_t>(0x13579BDFu + t * 0x2468ACE1u));
    }
    wasPlaying_ = false;
    samplesUntilNextStep_ = 0.0;
}

void NoiseSynthEngine::reset() noexcept {
    for (size_t t = 0; t < kNumNoiseTracks; ++t) {
        voices_[t].reset(static_cast<uint32_t>(0x13579BDFu + t * 0x2468ACE1u));
    }
    wasPlaying_ = false;
    samplesUntilNextStep_ = 0.0;
}

void NoiseSynthEngine::advanceSequencerStep(NoiseState& noiseState) noexcept {
    for (size_t t = 0; t < kNumNoiseTracks; ++t) {
        NoiseTrack& tr = noiseState.tracks[t];
        tr.currentStep = (tr.currentStep + 1) % static_cast<int>(kNumSeqSteps);
        const Step& step = tr.steps[static_cast<size_t>(tr.currentStep)];
        if (step.trigger) {
            const NoiseVoiceParams resolved = resolveStepVoice(tr.voice, step.locks);
            voices_[t].trigger(resolved, step.locks.mask != 0);
        } else {
            // Untriggered step expires previous step's transient locks back to base voice
            voices_[t].updateUnlockedBase(tr.voice);
        }
    }
}

void NoiseSynthEngine::process(int frames,
                               NoiseState& noiseState,
                               float* outMono,
                               float* outL,
                               float* outR) noexcept {
    if (outMono) std::fill(outMono, outMono + frames, 0.0f);
    if (outL) std::fill(outL, outL + frames, 0.0f);
    if (outR) std::fill(outR, outR + frames, 0.0f);
    if (frames <= 0) return;

    const double tempo = std::clamp<double>(noiseState.tempo, 30.0, 300.0);
    const double samplesPerStep = sampleRate_ * 60.0 / (tempo * 4.0); // 16th note

    if (!noiseState.playing) {
        wasPlaying_ = false;
        for (size_t t = 0; t < kNumNoiseTracks; ++t) {
            voices_[t].render(frames, noiseState.tracks[t].voice, outMono, outL, outR);
        }
        return;
    }

    if (!wasPlaying_) {
        wasPlaying_ = true;
        samplesUntilNextStep_ = samplesPerStep;
        // Fire current step (typically step 0) immediately on transport start
        for (size_t t = 0; t < kNumNoiseTracks; ++t) {
            NoiseTrack& tr = noiseState.tracks[t];
            const int stIdx = std::clamp(tr.currentStep, 0, static_cast<int>(kNumSeqSteps - 1));
            const Step& step = tr.steps[static_cast<size_t>(stIdx)];
            voices_[t].updateUnlockedBase(tr.voice);
            if (step.trigger) {
                voices_[t].trigger(resolveStepVoice(tr.voice, step.locks), step.locks.mask != 0);
            }
        }
    }

    int offset = 0;
    while (offset < frames) {
        const int remaining = frames - offset;
        int chunk = remaining;
        bool stepBoundary = false;

        if (samplesUntilNextStep_ <= static_cast<double>(remaining)) {
            chunk = std::max(0, static_cast<int>( std::floor(samplesUntilNextStep_) ));
            stepBoundary = true;
        }

        if (chunk > 0) {
            for (size_t t = 0; t < kNumNoiseTracks; ++t) {
                // Pass activeParams_ when locked so base edits don't overwrite step lock mid-step
                const Step& curStep = noiseState.tracks[t].steps[static_cast<size_t>(
                    std::clamp(noiseState.tracks[t].currentStep, 0, static_cast<int>(kNumSeqSteps - 1)))];
                const NoiseVoiceParams stepEffective = (curStep.trigger && curStep.locks.mask != 0)
                                                           ? resolveStepVoice(noiseState.tracks[t].voice, curStep.locks)
                                                           : noiseState.tracks[t].voice;
                voices_[t].render(chunk,
                                  stepEffective,
                                  outMono ? (outMono + offset) : nullptr,
                                  outL ? (outL + offset) : nullptr,
                                  outR ? (outR + offset) : nullptr);
            }
            samplesUntilNextStep_ -= static_cast<double>(chunk);
            offset += chunk;
        }

        if (stepBoundary) {
            advanceSequencerStep(noiseState);
            samplesUntilNextStep_ += samplesPerStep;
            // Ensure progress if chunk was 0
            if (chunk == 0 && offset < frames) {
                for (size_t t = 0; t < kNumNoiseTracks; ++t) {
                    const Step& curStep = noiseState.tracks[t].steps[static_cast<size_t>(
                        noiseState.tracks[t].currentStep)];
                    const NoiseVoiceParams stepEffective = (curStep.trigger && curStep.locks.mask != 0)
                                                               ? resolveStepVoice(noiseState.tracks[t].voice, curStep.locks)
                                                               : noiseState.tracks[t].voice;
                    voices_[t].render(1,
                                      stepEffective,
                                      outMono ? (outMono + offset) : nullptr,
                                      outL ? (outL + offset) : nullptr,
                                      outR ? (outR + offset) : nullptr);
                }
                samplesUntilNextStep_ -= 1.0;
                offset += 1;
            }
        }
    }
}

} // namespace biomus
