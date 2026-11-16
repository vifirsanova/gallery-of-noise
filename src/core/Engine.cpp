#include "core/Engine.hpp"
#include <algorithm>
#include <cmath>

namespace biomus {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr double kTwoPiD = 6.28318530717958647692;

inline float clampf(float v, float lo, float hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

inline float softSafetyClip(float x) noexcept {
    // Linear up to +-0.85, smooth asymptotic saturation toward +-1.05
    if (x > -0.85f && x < 0.85f) return x;
    return std::tanh(x);
}
} // namespace

Engine::Engine() : state_(makeDefaultState()), snapshot_(state_) {
    voiceFxPtrs_ = {
        &voicePan_,
        &voiceHpf_,
        &voiceComp_,
        &voiceFold_,
        &voiceBitcrush_,
        &voiceRingMod_,
        &voiceChorus_,
        &voiceReverb_,
        &voiceShimmer_,
        &voiceDelay_,
        &voiceLpf_
    };

    droneFxPtrs_ = {
        &droneLpf_,
        &droneDrive_,
        &droneFold_,
        &droneBitcrush_,
        &droneRingMod_,
        &droneChorus_,
        &droneReverb_
    };

    prepare(48000.0, 512);
}

void Engine::prepare(double sampleRate, int maxBlockSize) {
    sampleRate_ = (sampleRate > 1000.0) ? sampleRate : 48000.0;
    maxBlockSize_ = std::max(64, maxBlockSize);

    for (Effect* fx : voiceFxPtrs_) fx->prepare(sampleRate_, maxBlockSize_);
    for (Effect* fx : droneFxPtrs_) fx->prepare(sampleRate_, maxBlockSize_);

    bioGen_.prepare(sampleRate_);
    droneGen_.prepare(sampleRate_);
    noiseEngine_.prepare(sampleRate_);

    masterLpfL_.prepare(sampleRate_);
    masterLpfR_.prepare(sampleRate_);
    formant1_.prepare(sampleRate_);
    formant2_.prepare(sampleRate_);

    fbDelayLen_ = std::max<size_t>(64, static_cast<size_t>(sampleRate_ * 0.037)); // ~37 ms
    fbDelayBuf_.assign(fbDelayLen_ + 8, 0.0f);
    fbWritePos_ = 0;
    fbDcX1_ = 0.0f;
    fbDcY1_ = 0.0f;

    voiceBuf_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    droneBuf_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    fbInputBuf_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    noiseMonoBuf_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    noiseLeftBuf_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);
    noiseRightBuf_.assign(static_cast<size_t>(maxBlockSize_), 0.0f);

    for (auto& sm : smoothers_) {
        sm.prepare(sampleRate_, 0.030f); // 30 ms smoothing time constant (Spec 4.4)
    }
    syncSmoothersToState();
    snapshot_.publish(state_);
}

void Engine::reset() {
    for (Effect* fx : voiceFxPtrs_) fx->reset();
    for (Effect* fx : droneFxPtrs_) fx->reset();
    bioGen_.reset();
    droneGen_.reset();
    noiseEngine_.reset();
    masterLpfL_.reset();
    masterLpfR_.reset();
    formant1_.reset();
    formant2_.reset();
    std::fill(fbDelayBuf_.begin(), fbDelayBuf_.end(), 0.0f);
    fbWritePos_ = 0;
    fbDcX1_ = 0.0f;
    fbDcY1_ = 0.0f;
    meterEnv_ = 0.0f;
    filePhase_ = 0.0;
    fileEnvPhase_ = 0.0;
    syncSmoothersToState();
}

void Engine::syncSmoothersToState() noexcept {
    // Initialize all 76 smoothers directly to current modulated targets (no startup ramp)
    for (size_t i = 0; i < kNumVoiceFx; ++i) {
        const auto fx = static_cast<VoiceFxId>(i);
        const uint16_t ix = voiceXyModIndex(fx, 0);
        const uint16_t iy = voiceXyModIndex(fx, 1);
        const ModEntry mx = state_.modulation.get(ix);
        const ModEntry my = state_.modulation.get(iy);
        smoothers_[ix].reset(effectiveValue(state_.voice.xy[i].x, 0.0f, 1.0f, mx.source, mx.depth, state_.bio));
        smoothers_[iy].reset(effectiveValue(state_.voice.xy[i].y, 0.0f, 1.0f, my.source, my.depth, state_.bio));

        const FxDef& def = getVoiceFxDef(fx);
        for (size_t k = 0; k < kMaxKnobsPerFx; ++k) {
            const uint16_t ik = voiceKnobModIndex(fx, static_cast<int>(k));
            if (k < def.numKnobs) {
                const ModEntry mk = state_.modulation.get(ik);
                smoothers_[ik].reset(effectiveValue(state_.voice.knobs[i][k],
                                                    def.knobs[k].minVal,
                                                    def.knobs[k].maxVal,
                                                    mk.source, mk.depth, state_.bio));
            } else {
                smoothers_[ik].reset(0.0f);
            }
        }
    }

    for (size_t i = 0; i < kNumDroneFx; ++i) {
        const auto fx = static_cast<DroneFxId>(i);
        const uint16_t ix = droneXyModIndex(fx, 0);
        const uint16_t iy = droneXyModIndex(fx, 1);
        const ModEntry mx = state_.modulation.get(ix);
        const ModEntry my = state_.modulation.get(iy);
        smoothers_[ix].reset(effectiveValue(state_.drone.xy[i].x, 0.0f, 1.0f, mx.source, mx.depth, state_.bio));
        smoothers_[iy].reset(effectiveValue(state_.drone.xy[i].y, 0.0f, 1.0f, my.source, my.depth, state_.bio));

        const FxDef& def = getDroneFxDef(fx);
        for (size_t k = 0; k < kMaxKnobsPerFx; ++k) {
            const uint16_t ik = droneKnobModIndex(fx, static_cast<int>(k));
            if (k < def.numKnobs) {
                const ModEntry mk = state_.modulation.get(ik);
                smoothers_[ik].reset(effectiveValue(state_.drone.knobs[i][k],
                                                    def.knobs[k].minVal,
                                                    def.knobs[k].maxVal,
                                                    mk.source, mk.depth, state_.bio));
            } else {
                smoothers_[ik].reset(0.0f);
            }
        }
    }

    const float masterVals[kNumMasterParams] = {
        state_.master.voiceOut,
        state_.master.droneVol,
        state_.master.fb,
        state_.master.lpf
    };
    for (size_t m = 0; m < kNumMasterParams; ++m) {
        const auto mp = static_cast<MasterParamId>(m);
        const KnobDef& def = getMasterParamDef(mp);
        const uint16_t im = masterModIndex(mp);
        const ModEntry mm = state_.modulation.get(im);
        smoothers_[im].reset(effectiveValue(masterVals[m], def.minVal, def.maxVal, mm.source, mm.depth, state_.bio));
    }
}

bool Engine::enqueueCommand(const Command& cmd) noexcept {
    std::lock_guard<std::mutex> lock(producerMutex_);
    return commandQueue_.push(cmd);
}

void Engine::drainCommands() noexcept {
    Command cmd{};
    while (commandQueue_.pop(cmd)) {
        applyCommand(state_, cmd);
    }
}

void Engine::updateSmoothedTargets(int frames) noexcept {
    for (size_t i = 0; i < kNumVoiceFx; ++i) {
        const auto fx = static_cast<VoiceFxId>(i);
        const uint16_t ix = voiceXyModIndex(fx, 0);
        const uint16_t iy = voiceXyModIndex(fx, 1);
        const ModEntry mx = state_.modulation.get(ix);
        const ModEntry my = state_.modulation.get(iy);
        smoothers_[ix].setTarget(effectiveValue(state_.voice.xy[i].x, 0.0f, 1.0f, mx.source, mx.depth, state_.bio));
        smoothers_[iy].setTarget(effectiveValue(state_.voice.xy[i].y, 0.0f, 1.0f, my.source, my.depth, state_.bio));
        const float sx = smoothers_[ix].advance(frames);
        const float sy = smoothers_[iy].advance(frames);

        const FxDef& def = getVoiceFxDef(fx);
        float sk0 = 0.0f, sk1 = 0.0f;
        if (def.numKnobs > 0) {
            const uint16_t ik0 = voiceKnobModIndex(fx, 0);
            const ModEntry mk0 = state_.modulation.get(ik0);
            smoothers_[ik0].setTarget(effectiveValue(state_.voice.knobs[i][0],
                                                     def.knobs[0].minVal,
                                                     def.knobs[0].maxVal,
                                                     mk0.source, mk0.depth, state_.bio));
            sk0 = smoothers_[ik0].advance(frames);
        }
        if (def.numKnobs > 1) {
            const uint16_t ik1 = voiceKnobModIndex(fx, 1);
            const ModEntry mk1 = state_.modulation.get(ik1);
            smoothers_[ik1].setTarget(effectiveValue(state_.voice.knobs[i][1],
                                                     def.knobs[1].minVal,
                                                     def.knobs[1].maxVal,
                                                     mk1.source, mk1.depth, state_.bio));
            sk1 = smoothers_[ik1].advance(frames);
        }
        voiceFxPtrs_[i]->setParameters(sx, sy, sk0, sk1);
    }

    for (size_t i = 0; i < kNumDroneFx; ++i) {
        const auto fx = static_cast<DroneFxId>(i);
        const uint16_t ix = droneXyModIndex(fx, 0);
        const uint16_t iy = droneXyModIndex(fx, 1);
        const ModEntry mx = state_.modulation.get(ix);
        const ModEntry my = state_.modulation.get(iy);
        smoothers_[ix].setTarget(effectiveValue(state_.drone.xy[i].x, 0.0f, 1.0f, mx.source, mx.depth, state_.bio));
        smoothers_[iy].setTarget(effectiveValue(state_.drone.xy[i].y, 0.0f, 1.0f, my.source, my.depth, state_.bio));
        const float sx = smoothers_[ix].advance(frames);
        const float sy = smoothers_[iy].advance(frames);

        const FxDef& def = getDroneFxDef(fx);
        float sk0 = 0.0f;
        if (def.numKnobs > 0) {
            const uint16_t ik0 = droneKnobModIndex(fx, 0);
            const ModEntry mk0 = state_.modulation.get(ik0);
            smoothers_[ik0].setTarget(effectiveValue(state_.drone.knobs[i][0],
                                                     def.knobs[0].minVal,
                                                     def.knobs[0].maxVal,
                                                     mk0.source, mk0.depth, state_.bio));
            sk0 = smoothers_[ik0].advance(frames);
        }
        droneFxPtrs_[i]->setParameters(sx, sy, sk0, 0.0f);
    }

    const float masterVals[kNumMasterParams] = {
        state_.master.voiceOut,
        state_.master.droneVol,
        state_.master.fb,
        state_.master.lpf
    };
    for (size_t m = 0; m < kNumMasterParams; ++m) {
        const auto mp = static_cast<MasterParamId>(m);
        const KnobDef& def = getMasterParamDef(mp);
        const uint16_t im = masterModIndex(mp);
        const ModEntry mm = state_.modulation.get(im);
        smoothers_[im].setTarget(effectiveValue(masterVals[m], def.minVal, def.maxVal, mm.source, mm.depth, state_.bio));
    }
}

void Engine::generateSyntheticFileVoice(float* dst, int frames) noexcept {
    // Expressive vowel-formant vocal phrase loop so pressing L/P ("File") feeds a musical voice
    for (int i = 0; i < frames; ++i) {
        fileEnvPhase_ += 1.0 / sampleRate_;
        if (fileEnvPhase_ >= 4.0) fileEnvPhase_ -= 4.0;

        // Melodic pitch contour (A2 -> C3 -> E3 -> D3) with natural vibrato
        float baseFreq = 110.0f;
        if (fileEnvPhase_ < 1.0) baseFreq = 110.0f;
        else if (fileEnvPhase_ < 2.0) baseFreq = 130.81f;
        else if (fileEnvPhase_ < 3.0) baseFreq = 164.81f;
        else baseFreq = 146.83f;

        const float vibrato = 1.0f + 0.012f * static_cast<float>(std::sin(kTwoPiD * 5.2 * fileEnvPhase_));
        const double dt = static_cast<double>(baseFreq * vibrato) / sampleRate_;
        filePhase_ += dt;
        if (filePhase_ >= 1.0) filePhase_ -= 1.0;

        // Glottal-like harmonic rich waveform
        const float glottal = static_cast<float>(
            0.55 * std::sin(kTwoPiD * filePhase_) +
            0.30 * std::sin(2.0 * kTwoPiD * filePhase_) +
            0.18 * std::sin(3.0 * kTwoPiD * filePhase_) +
            0.10 * std::sin(4.0 * kTwoPiD * filePhase_));

        // Morphing vowel formants (F1: 400..750 Hz, F2: 1100..2200 Hz)
        const float f1 = 520.0f + 210.0f * static_cast<float>(std::sin(kTwoPiD * 0.35 * fileEnvPhase_));
        const float f2 = 1550.0f + 550.0f * static_cast<float>(std::cos(kTwoPiD * 0.23 * fileEnvPhase_));
        formant1_.setCutoffAndQ(f1, 4.5f);
        formant2_.setCutoffAndQ(f2, 5.5f);

        const float syllPhase = static_cast<float>(fileEnvPhase_ - std::floor(fileEnvPhase_));
        const float env = std::sin(kPi * std::min(1.0f, syllPhase * 1.15f));
        const float voiced = (0.65f * formant1_.processSample(glottal, TptStateVariableFilter::Mode::BandPass)
                            + 0.45f * formant2_.processSample(glottal, TptStateVariableFilter::Mode::BandPass))
                           * std::max(0.0f, env) * 0.45f;
        dst[i] += voiced;
    }
}

void Engine::process(const float* in, float* out, int frames) noexcept {
    if (!out || frames <= 0) return;
    drainCommands();

    int offset = 0;
    float peakBlock = 0.0f;

    while (offset < frames) {
        const int chunk = std::min(frames - offset, maxBlockSize_);

        if (bioMockupEnabled_) {
            bioGen_.advance(chunk, state_.bio);
        }
        updateSmoothedTargets(chunk);

        if (state_.bypass) {
            for (int i = 0; i < chunk; ++i) {
                const float s = (in && state_.mic) ? in[offset + i] : 0.0f;
                out[offset + i] = s;
                peakBlock = std::max(peakBlock, std::fabs(s));
            }
            offset += chunk;
            continue;
        }

        // 1. Voice input -> Voice FX chain
        if (in && state_.mic) {
            std::copy(in + offset, in + offset + chunk, voiceBuf_.begin());
        } else {
            std::fill(voiceBuf_.begin(), voiceBuf_.begin() + chunk, 0.0f);
        }
        if (state_.fileOn) {
            generateSyntheticFileVoice(voiceBuf_.data(), chunk);
        }

        for (size_t s = 0; s < kNumVoiceFx; ++s) {
            const VoiceFxId fxId = state_.voice.order[s];
            const auto fxIdx = static_cast<size_t>(fxId);
            if (state_.voice.enabled[fxIdx]) {
                voiceFxPtrs_[fxIdx]->process(voiceBuf_.data(), chunk);
            }
        }

        // 2. Pull feedback from masterFbDelay -> Drone generator + Drone FX chain
        auto& fbSm = smoothers_[masterModIndex(MasterParamId::Feedback)];
        for (int i = 0; i < chunk; ++i) {
            const float fbGain = fbSm.next();
            const size_t readPos = (fbWritePos_ + fbDelayBuf_.size() - fbDelayLen_) % fbDelayBuf_.size();
            fbInputBuf_[static_cast<size_t>(i)] = fbDelayBuf_[readPos] * fbGain * 0.75f;
        }

        const bool hasFbEnergy = (fbSm.current() > 0.001f);
        if (state_.droneOn) {
            droneGen_.generate(droneBuf_.data(), chunk, fbInputBuf_.data());
            for (size_t s = 0; s < kNumDroneFx; ++s) {
                const DroneFxId fxId = state_.drone.order[s];
                const auto fxIdx = static_cast<size_t>(fxId);
                if (state_.drone.enabled[fxIdx]) {
                    droneFxPtrs_[fxIdx]->process(droneBuf_.data(), chunk);
                }
            }
        } else if (hasFbEnergy) {
            std::copy(fbInputBuf_.begin(), fbInputBuf_.begin() + chunk, droneBuf_.begin());
            for (size_t s = 0; s < kNumDroneFx; ++s) {
                const DroneFxId fxId = state_.drone.order[s];
                const auto fxIdx = static_cast<size_t>(fxId);
                if (state_.drone.enabled[fxIdx]) {
                    droneFxPtrs_[fxIdx]->process(droneBuf_.data(), chunk);
                }
            }
        } else {
            std::fill(droneBuf_.begin(), droneBuf_.begin() + chunk, 0.0f);
        }

        // 3. Noise 4-track synth & sequencer
        noiseEngine_.process(chunk, state_.noise, noiseMonoBuf_.data(), nullptr, nullptr);

        // 4. Master Mix -> Master LPF -> Out & 5. Feedback Delay Write
        auto& vOutSm = smoothers_[masterModIndex(MasterParamId::VoiceOut)];
        auto& dVolSm = smoothers_[masterModIndex(MasterParamId::DroneVol)];
        auto& mLpfSm = smoothers_[masterModIndex(MasterParamId::Lpf)];

        for (int i = 0; i < chunk; ++i) {
            const float vGain = vOutSm.next();
            const float dGain = dVolSm.next();
            const float lpfHz = mLpfSm.next();
            masterLpfL_.setCutoffAndQ(lpfHz, 0.707f);

            const float mixed = voiceBuf_[static_cast<size_t>(i)] * vGain
                              + droneBuf_[static_cast<size_t>(i)] * dGain
                              + noiseMonoBuf_[static_cast<size_t>(i)];

            const float filtered = masterLpfL_.processSample(mixed, TptStateVariableFilter::Mode::LowPass);

            // Feed DC-blocked soft-saturated signal into feedback delay line
            const float dcOut = filtered - fbDcX1_ + 0.995f * fbDcY1_;
            fbDcX1_ = filtered;
            fbDcY1_ = (std::fabs(dcOut) < 1e-20f) ? 0.0f : dcOut;
            fbDelayBuf_[fbWritePos_] = std::tanh(dcOut);
            fbWritePos_ = (fbWritePos_ + 1) % fbDelayBuf_.size();

            peakBlock = std::max(peakBlock, std::fabs(filtered));
            out[offset + i] = softSafetyClip(filtered);
        }

        offset += chunk;
    }

    meterEnv_ = (peakBlock > meterEnv_) ? (0.6f * peakBlock + 0.4f * meterEnv_) : (meterEnv_ * 0.92f);
    state_.masterLevel = clampf(meterEnv_, 0.0f, 1.0f);
    state_.clip = (peakBlock > 0.95f);
    snapshot_.publish(state_);
}

void Engine::processStereo(const float* inMono, float* outStereoInterleaved, int frames) noexcept {
    if (!outStereoInterleaved || frames <= 0) return;
    drainCommands();

    int offset = 0;
    float peakBlock = 0.0f;

    while (offset < frames) {
        const int chunk = std::min(frames - offset, maxBlockSize_);

        if (bioMockupEnabled_) {
            bioGen_.advance(chunk, state_.bio);
        }
        updateSmoothedTargets(chunk);

        if (state_.bypass) {
            for (int i = 0; i < chunk; ++i) {
                const float s = (inMono && state_.mic) ? inMono[offset + i] : 0.0f;
                outStereoInterleaved[(offset + i) * 2]     = s;
                outStereoInterleaved[(offset + i) * 2 + 1] = s;
                peakBlock = std::max(peakBlock, std::fabs(s));
            }
            offset += chunk;
            continue;
        }

        // 1. Voice
        if (inMono && state_.mic) {
            std::copy(inMono + offset, inMono + offset + chunk, voiceBuf_.begin());
        } else {
            std::fill(voiceBuf_.begin(), voiceBuf_.begin() + chunk, 0.0f);
        }
        if (state_.fileOn) {
            generateSyntheticFileVoice(voiceBuf_.data(), chunk);
        }

        for (size_t s = 0; s < kNumVoiceFx; ++s) {
            const VoiceFxId fxId = state_.voice.order[s];
            const auto fxIdx = static_cast<size_t>(fxId);
            if (state_.voice.enabled[fxIdx]) {
                voiceFxPtrs_[fxIdx]->process(voiceBuf_.data(), chunk);
            }
        }

        const float voicePan01 = state_.voice.enabled[static_cast<size_t>(VoiceFxId::PAN)]
                                     ? clampf(voicePan_.stereoPan(), 0.0f, 1.0f)
                                     : 0.5f;
        const float panAngle = voicePan01 * (0.5f * kPi);
        const float voicePanL = std::cos(panAngle) * 1.41421356f * 0.70710678f * 1.41421356f; // unity at center=1.0
        const float voicePanR = std::sin(panAngle) * 1.41421356f;
        // Note: cos(pi/4)*sqrt(2) == 1.0, sin(pi/4)*sqrt(2) == 1.0!
        const float vPanGainL = std::cos(panAngle) * 1.41421356f;
        const float vPanGainR = voicePanR;
        (void)voicePanL;

        // 2. Drone + Feedback
        auto& fbSm = smoothers_[masterModIndex(MasterParamId::Feedback)];
        for (int i = 0; i < chunk; ++i) {
            const float fbGain = fbSm.next();
            const size_t readPos = (fbWritePos_ + fbDelayBuf_.size() - fbDelayLen_) % fbDelayBuf_.size();
            fbInputBuf_[static_cast<size_t>(i)] = fbDelayBuf_[readPos] * fbGain * 0.75f;
        }

        const bool hasFbEnergy = (fbSm.current() > 0.001f);
        if (state_.droneOn) {
            droneGen_.generate(droneBuf_.data(), chunk, fbInputBuf_.data());
            for (size_t s = 0; s < kNumDroneFx; ++s) {
                const DroneFxId fxId = state_.drone.order[s];
                const auto fxIdx = static_cast<size_t>(fxId);
                if (state_.drone.enabled[fxIdx]) {
                    droneFxPtrs_[fxIdx]->process(droneBuf_.data(), chunk);
                }
            }
        } else if (hasFbEnergy) {
            std::copy(fbInputBuf_.begin(), fbInputBuf_.begin() + chunk, droneBuf_.begin());
            for (size_t s = 0; s < kNumDroneFx; ++s) {
                const DroneFxId fxId = state_.drone.order[s];
                const auto fxIdx = static_cast<size_t>(fxId);
                if (state_.drone.enabled[fxIdx]) {
                    droneFxPtrs_[fxIdx]->process(droneBuf_.data(), chunk);
                }
            }
        } else {
            std::fill(droneBuf_.begin(), droneBuf_.begin() + chunk, 0.0f);
        }

        // 3. Noise stereo tracks
        noiseEngine_.process(chunk,
                             state_.noise,
                             noiseMonoBuf_.data(),
                             noiseLeftBuf_.data(),
                             noiseRightBuf_.data());

        // 4. Master stereo LPF + feedback tap
        auto& vOutSm = smoothers_[masterModIndex(MasterParamId::VoiceOut)];
        auto& dVolSm = smoothers_[masterModIndex(MasterParamId::DroneVol)];
        auto& mLpfSm = smoothers_[masterModIndex(MasterParamId::Lpf)];

        for (int i = 0; i < chunk; ++i) {
            const float vGain = vOutSm.next();
            const float dGain = dVolSm.next();
            const float lpfHz = mLpfSm.next();
            masterLpfL_.setCutoffAndQ(lpfHz, 0.707f);
            masterLpfR_.setCutoffAndQ(lpfHz, 0.707f);

            const float vSample = voiceBuf_[static_cast<size_t>(i)] * vGain;
            const float dSample = droneBuf_[static_cast<size_t>(i)] * dGain;

            const float mixL = vSample * vPanGainL + dSample + noiseLeftBuf_[static_cast<size_t>(i)];
            const float mixR = vSample * vPanGainR + dSample + noiseRightBuf_[static_cast<size_t>(i)];

            const float outL = masterLpfL_.processSample(mixL, TptStateVariableFilter::Mode::LowPass);
            const float outR = masterLpfR_.processSample(mixR, TptStateVariableFilter::Mode::LowPass);

            const float monoSum = 0.5f * (outL + outR);
            const float dcOut = monoSum - fbDcX1_ + 0.995f * fbDcY1_;
            fbDcX1_ = monoSum;
            fbDcY1_ = (std::fabs(dcOut) < 1e-20f) ? 0.0f : dcOut;
            fbDelayBuf_[fbWritePos_] = std::tanh(dcOut);
            fbWritePos_ = (fbWritePos_ + 1) % fbDelayBuf_.size();

            peakBlock = std::max(peakBlock, std::max(std::fabs(outL), std::fabs(outR)));
            outStereoInterleaved[(offset + i) * 2]     = softSafetyClip(outL);
            outStereoInterleaved[(offset + i) * 2 + 1] = softSafetyClip(outR);
        }

        offset += chunk;
    }

    meterEnv_ = (peakBlock > meterEnv_) ? (0.6f * peakBlock + 0.4f * meterEnv_) : (meterEnv_ * 0.92f);
    state_.masterLevel = clampf(meterEnv_, 0.0f, 1.0f);
    state_.clip = (peakBlock > 0.95f);
    snapshot_.publish(state_);
}

} // namespace biomus
