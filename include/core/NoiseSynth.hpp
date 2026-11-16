#pragma once

#include "core/Effects.hpp"
#include "core/State.hpp"
#include <array>
#include <cstdint>

namespace biomus {

// ============================================================================
// Single Noise Synth Voice DSP (White/Pink/Brown + SVF + AD Envs + 2 LFOs)
// ============================================================================
class NoiseVoiceDsp {
public:
    void prepare(double sampleRate, uint32_t seed) noexcept;
    void reset(uint32_t seed) noexcept;

    // Trigger AD envelopes with resolved voice parameters (including step locks)
    void trigger(const NoiseVoiceParams& effectiveParams, bool hasLocks = false) noexcept;

    // Called when a non-triggered step is reached so transient step locks expire
    // back to base track voice parameters cleanly on next step boundary
    void updateUnlockedBase(const NoiseVoiceParams& baseParams) noexcept;

    // Renders mono + stereo-panned contribution (accumulates into outL, outR, outMono)
    void render(int frames,
                const NoiseVoiceParams& currentBaseParams,
                float* accumMono,
                float* accumL,
                float* accumR) noexcept;

    const NoiseVoiceParams& activeVoiceParams() const noexcept { return activeParams_; }
    bool hasActiveStepLocks() const noexcept { return usingLockedParams_; }
    float envelopeLevel() const noexcept { return ampEnv_; }

private:
    float nextWhite() noexcept {
        uint32_t x = rng_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        rng_ = x;
        return static_cast<float>(static_cast<int32_t>(x)) * (1.0f / 2147483648.0f);
    }

    float nextNoiseSample(NoiseType type, float pitchHoldFactor) noexcept;

    double sampleRate_ = 48000.0;
    uint32_t rng_ = 0xCAFEBABEu;

    // Paul Kellet pink noise filter state
    float b0_ = 0.0f, b1_ = 0.0f, b2_ = 0.0f;
    // Brown noise leaky integrator
    float brown_ = 0.0f;
    // Clocked noise pitch decimation ("pitch" LFO target)
    float heldNoise_ = 0.0f;
    float holdPhase_ = 1.0f;

    // AD Envelopes (0 = idle, 1 = attack, 2 = decay)
    uint8_t envStage_ = 0;
    float ampEnv_ = 0.0f;
    float modEnv_ = 0.0f;

    // Dual LFO phases [0..1)
    double lfo1Phase_ = 0.0;
    double lfo2Phase_ = 0.0;

    NoiseVoiceParams activeParams_{};
    bool usingLockedParams_ = false;
    TptStateVariableFilter svf_;
};

// ============================================================================
// 4-Track 16-Step Sequencer + Noise Synthesizer Bank (Spec 3.5, 4.3, 7.1)
// ============================================================================
class NoiseSynthEngine {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Sample-accurate sequencer progression + 4-voice synthesis.
    // Updates `noiseState.tracks[t].currentStep` in place.
    void process(int frames,
                 NoiseState& noiseState,
                 float* outMono,
                 float* outL,
                 float* outR) noexcept;

    // Explicit step-advance helper (also exposed for deterministic unit tests)
    void advanceSequencerStep(NoiseState& noiseState) noexcept;

    const NoiseVoiceDsp& voiceDsp(size_t trackIdx) const noexcept {
        return voices_[trackIdx < kNumNoiseTracks ? trackIdx : 0];
    }

private:
    double sampleRate_ = 48000.0;
    bool wasPlaying_ = false;
    double samplesUntilNextStep_ = 0.0;
    std::array<NoiseVoiceDsp, kNumNoiseTracks> voices_{};
};

} // namespace biomus
