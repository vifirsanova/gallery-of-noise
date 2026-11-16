#include "core/State.hpp"
#include <algorithm>
#include <cmath>

namespace biomus {

namespace {

inline float clampf(float v, float lo, float hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

template <typename FxEnum, size_t N>
int findInOrder(const std::array<FxEnum, N>& order, FxEnum target) noexcept {
    for (size_t i = 0; i < N; ++i) {
        if (order[i] == target) return static_cast<int>(i);
    }
    return 0;
}

} // namespace

FullState makeDefaultState() noexcept {
    FullState s{};

    // Initialize Voice chain order, enabled, default XY, default knobs
    for (size_t i = 0; i < kNumVoiceFx; ++i) {
        const auto fx = static_cast<VoiceFxId>(i);
        s.voice.order[i] = fx;
        s.voice.enabled[i] = true;
        s.voice.xy[i] = {0.5f, 0.5f};
        const FxDef& def = getVoiceFxDef(fx);
        for (size_t k = 0; k < kMaxKnobsPerFx; ++k) {
            s.voice.knobs[i][k] = (k < def.numKnobs) ? def.knobs[k].defaultVal : 0.0f;
        }
    }
    s.voice.active = VoiceFxId::REVERB;

    // Initialize Drone chain order, enabled, default XY, default knobs
    for (size_t i = 0; i < kNumDroneFx; ++i) {
        const auto fx = static_cast<DroneFxId>(i);
        s.drone.order[i] = fx;
        s.drone.enabled[i] = true;
        s.drone.xy[i] = {0.5f, 0.5f};
        const FxDef& def = getDroneFxDef(fx);
        for (size_t k = 0; k < kMaxKnobsPerFx; ++k) {
            s.drone.knobs[i][k] = (k < def.numKnobs) ? def.knobs[k].defaultVal : 0.0f;
        }
    }
    s.drone.active = DroneFxId::LPF;

    // Distinct musical voices across the 4 noise tracks + default rhythmic pattern from Spec 5
    for (size_t t = 0; t < kNumNoiseTracks; ++t) {
        s.noise.tracks[t].voice = NoiseVoiceParams{};
        s.noise.tracks[t].currentStep = 0;
    }
    // Track 1: Kick/Low thump brown/white sub-percussion [0, 4, 8, 12]
    for (int st : {0, 4, 8, 12}) s.noise.tracks[0].steps[st].trigger = true;
    // Track 2: Offbeat snare/burst [2, 6, 10, 14]
    s.noise.tracks[1].voice.type = NoiseType::Pink;
    s.noise.tracks[1].voice.cutoff = 2400.0f;
    s.noise.tracks[1].voice.ampDecay = 0.22f;
    for (int st : {2, 6, 10, 14}) s.noise.tracks[1].steps[st].trigger = true;
    // Track 3: Crisp hi-hat ticks [1, 3, 5, 7]
    s.noise.tracks[2].voice.type = NoiseType::White;
    s.noise.tracks[2].voice.cutoff = 6500.0f;
    s.noise.tracks[2].voice.ampDecay = 0.09f;
    s.noise.tracks[2].voice.pan = -0.35f;
    for (int st : {1, 3, 5, 7}) s.noise.tracks[2].steps[st].trigger = true;
    // Track 4: Textured brown drift [3, 7, 11, 15]
    s.noise.tracks[3].voice.type = NoiseType::Brown;
    s.noise.tracks[3].voice.cutoff = 900.0f;
    s.noise.tracks[3].voice.ampDecay = 0.45f;
    s.noise.tracks[3].voice.pan = 0.35f;
    for (int st : {3, 7, 11, 15}) s.noise.tracks[3].steps[st].trigger = true;

    applyPreset(s, PresetId::VOICE);
    return s;
}

void applyPreset(FullState& s, PresetId preset) noexcept {
    s.preset = preset;
    s.modulation.clearAll();

    auto setV = [&](VoiceFxId id, bool en, float x, float y, bool updateXy = true) {
        const auto idx = static_cast<size_t>(id);
        s.voice.enabled[idx] = en;
        if (updateXy) s.voice.xy[idx] = {x, y};
    };
    auto setD = [&](DroneFxId id, bool en, float x, float y, bool updateXy = true) {
        const auto idx = static_cast<size_t>(id);
        s.drone.enabled[idx] = en;
        if (updateXy) s.drone.xy[idx] = {x, y};
    };

    switch (preset) {
        case PresetId::VOICE:
            setV(VoiceFxId::PAN,      true,  0.50f, 0.50f);
            setV(VoiceFxId::HPF,      true,  0.20f, 0.50f);
            setV(VoiceFxId::COMP,     true,  0.50f, 0.40f);
            setV(VoiceFxId::FOLD,     false, 0.50f, 0.50f, false);
            setV(VoiceFxId::BITCRUSH, false, 0.50f, 0.50f, false);
            setV(VoiceFxId::RINGMOD,  false, 0.50f, 0.50f, false);
            setV(VoiceFxId::CHORUS,   true,  0.15f, 0.60f);
            setV(VoiceFxId::REVERB,   true,  0.60f, 0.40f);
            setV(VoiceFxId::SHIMMER,  true,  0.50f, 0.60f);
            setV(VoiceFxId::DELAY,    true,  0.35f, 0.50f);
            setV(VoiceFxId::LPF,      true,  0.90f, 0.50f);

            setD(DroneFxId::LPF,      true,  0.40f, 0.70f);
            setD(DroneFxId::DRIVE,    false, 0.50f, 0.50f, false);
            setD(DroneFxId::FOLD,     false, 0.50f, 0.50f, false);
            setD(DroneFxId::BITCRUSH, false, 0.50f, 0.50f, false);
            setD(DroneFxId::RINGMOD,  false, 0.50f, 0.50f, false);
            setD(DroneFxId::CHORUS,   true,  0.20f, 0.70f);
            setD(DroneFxId::REVERB,   true,  0.60f, 0.50f);

            s.master.voiceOut = 1.2f;
            s.master.droneVol = 0.25f;
            s.master.fb       = 0.3f;
            s.master.lpf      = 12000.0f;
            break;

        case PresetId::WALL:
            setV(VoiceFxId::PAN,      true,  0.50f, 0.50f);
            setV(VoiceFxId::HPF,      true,  0.15f, 0.50f);
            setV(VoiceFxId::COMP,     true,  0.70f, 0.80f);
            setV(VoiceFxId::FOLD,     true,  0.50f, 0.70f);
            setV(VoiceFxId::BITCRUSH, false, 0.50f, 0.50f, false);
            setV(VoiceFxId::RINGMOD,  false, 0.50f, 0.50f, false);
            setV(VoiceFxId::CHORUS,   true,  0.30f, 0.90f);
            setV(VoiceFxId::REVERB,   true,  0.95f, 0.30f);
            setV(VoiceFxId::SHIMMER,  true,  0.90f, 0.90f);
            setV(VoiceFxId::DELAY,    true,  0.50f, 0.75f);
            setV(VoiceFxId::LPF,      true,  0.70f, 0.50f);

            setD(DroneFxId::LPF,      true,  0.50f, 0.60f);
            setD(DroneFxId::DRIVE,    true,  0.70f, 0.50f);
            setD(DroneFxId::FOLD,     true,  0.60f, 0.60f);
            setD(DroneFxId::BITCRUSH, false, 0.50f, 0.50f, false);
            setD(DroneFxId::RINGMOD,  false, 0.50f, 0.50f, false);
            setD(DroneFxId::CHORUS,   true,  0.40f, 0.80f);
            setD(DroneFxId::REVERB,   true,  0.80f, 0.40f);

            s.master.voiceOut = 1.0f;
            s.master.droneVol = 0.7f;
            s.master.fb       = 0.6f;
            s.master.lpf      = 9000.0f;
            break;

        case PresetId::AMBIENT:
            setV(VoiceFxId::PAN,      true,  0.50f, 0.50f);
            setV(VoiceFxId::HPF,      true,  0.25f, 0.50f);
            setV(VoiceFxId::COMP,     false, 0.50f, 0.50f, false);
            setV(VoiceFxId::FOLD,     false, 0.50f, 0.50f, false);
            setV(VoiceFxId::BITCRUSH, false, 0.50f, 0.50f, false);
            setV(VoiceFxId::RINGMOD,  false, 0.50f, 0.50f, false);
            setV(VoiceFxId::CHORUS,   true,  0.10f, 0.40f);
            setV(VoiceFxId::REVERB,   true,  0.95f, 0.50f);
            setV(VoiceFxId::SHIMMER,  true,  0.80f, 0.50f);
            setV(VoiceFxId::DELAY,    true,  0.60f, 0.60f);
            setV(VoiceFxId::LPF,      true,  0.80f, 0.50f);

            setD(DroneFxId::LPF,      true,  0.30f, 0.60f);
            setD(DroneFxId::DRIVE,    false, 0.50f, 0.50f, false);
            setD(DroneFxId::FOLD,     false, 0.50f, 0.50f, false);
            setD(DroneFxId::BITCRUSH, false, 0.50f, 0.50f, false);
            setD(DroneFxId::RINGMOD,  false, 0.50f, 0.50f, false);
            setD(DroneFxId::CHORUS,   true,  0.10f, 0.90f);
            setD(DroneFxId::REVERB,   true,  0.90f, 0.50f);

            s.master.voiceOut = 1.1f;
            s.master.droneVol = 0.6f;
            s.master.fb       = 0.4f;
            s.master.lpf      = 7000.0f;
            break;

        case PresetId::NOISE:
            setV(VoiceFxId::PAN,      true,  0.50f, 0.50f);
            setV(VoiceFxId::HPF,      true,  0.40f, 0.50f);
            setV(VoiceFxId::COMP,     true,  0.80f, 0.90f);
            setV(VoiceFxId::FOLD,     true,  0.70f, 0.80f);
            setV(VoiceFxId::BITCRUSH, true,  0.30f, 0.80f);
            setV(VoiceFxId::RINGMOD,  true,  0.50f, 0.70f);
            setV(VoiceFxId::CHORUS,   false, 0.50f, 0.50f, false);
            setV(VoiceFxId::REVERB,   false, 0.50f, 0.50f, false);
            setV(VoiceFxId::SHIMMER,  true,  0.90f, 0.95f);
            setV(VoiceFxId::DELAY,    true,  0.10f, 0.90f);
            setV(VoiceFxId::LPF,      true,  0.50f, 0.50f);

            setD(DroneFxId::LPF,      true,  0.60f, 0.80f);
            setD(DroneFxId::DRIVE,    true,  0.90f, 0.70f);
            setD(DroneFxId::FOLD,     true,  0.80f, 0.70f);
            setD(DroneFxId::BITCRUSH, true,  0.40f, 0.90f);
            setD(DroneFxId::RINGMOD,  true,  0.60f, 0.80f);
            setD(DroneFxId::CHORUS,   true,  0.70f, 0.50f);
            setD(DroneFxId::REVERB,   true,  0.50f, 0.50f);

            s.master.voiceOut = 0.9f;
            s.master.droneVol = 1.0f;
            s.master.fb       = 0.8f;
            s.master.lpf      = 14000.0f;
            break;

        case PresetId::CLEAN:
            setV(VoiceFxId::PAN,      true,  0.50f, 0.50f);
            setV(VoiceFxId::HPF,      true,  0.20f, 0.50f);
            setV(VoiceFxId::COMP,     true,  0.50f, 0.30f);
            setV(VoiceFxId::FOLD,     false, 0.50f, 0.50f, false);
            setV(VoiceFxId::BITCRUSH, false, 0.50f, 0.50f, false);
            setV(VoiceFxId::RINGMOD,  false, 0.50f, 0.50f, false);
            setV(VoiceFxId::CHORUS,   false, 0.50f, 0.50f, false);
            setV(VoiceFxId::REVERB,   false, 0.50f, 0.50f, false);
            setV(VoiceFxId::SHIMMER,  false, 0.50f, 0.50f, false);
            setV(VoiceFxId::DELAY,    false, 0.50f, 0.50f, false);
            setV(VoiceFxId::LPF,      true,  0.90f, 0.50f);

            setD(DroneFxId::LPF,      true,  0.40f, 0.30f);
            setD(DroneFxId::DRIVE,    false, 0.50f, 0.50f, false);
            setD(DroneFxId::FOLD,     false, 0.50f, 0.50f, false);
            setD(DroneFxId::BITCRUSH, false, 0.50f, 0.50f, false);
            setD(DroneFxId::RINGMOD,  false, 0.50f, 0.50f, false);
            setD(DroneFxId::CHORUS,   false, 0.50f, 0.50f, false);
            setD(DroneFxId::REVERB,   false, 0.50f, 0.50f, false);

            s.master.voiceOut = 1.0f;
            s.master.droneVol = 0.0f;
            s.master.fb       = 0.0f;
            s.master.lpf      = 18000.0f;
            break;

        case PresetId::BREATH:
            setV(VoiceFxId::PAN,      true,  0.50f, 0.50f);
            setV(VoiceFxId::HPF,      true,  0.30f, 0.50f);
            setV(VoiceFxId::COMP,     true,  0.40f, 0.60f);
            setV(VoiceFxId::FOLD,     false, 0.50f, 0.50f, false);
            setV(VoiceFxId::BITCRUSH, false, 0.50f, 0.50f, false);
            setV(VoiceFxId::RINGMOD,  false, 0.50f, 0.50f, false);
            setV(VoiceFxId::CHORUS,   true,  0.10f, 0.30f);
            setV(VoiceFxId::REVERB,   true,  0.80f, 0.60f);
            setV(VoiceFxId::SHIMMER,  true,  0.70f, 0.40f);
            setV(VoiceFxId::DELAY,    true,  0.40f, 0.50f);
            setV(VoiceFxId::LPF,      true,  0.85f, 0.50f);

            setD(DroneFxId::LPF,      true,  0.35f, 0.50f);
            setD(DroneFxId::DRIVE,    false, 0.50f, 0.50f, false);
            setD(DroneFxId::FOLD,     false, 0.50f, 0.50f, false);
            setD(DroneFxId::BITCRUSH, false, 0.50f, 0.50f, false);
            setD(DroneFxId::RINGMOD,  false, 0.50f, 0.50f, false);
            setD(DroneFxId::CHORUS,   true,  0.10f, 0.60f);
            setD(DroneFxId::REVERB,   true,  0.85f, 0.50f);

            s.master.voiceOut = 1.1f;
            s.master.droneVol = 0.4f;
            s.master.fb       = 0.35f;
            s.master.lpf      = 8000.0f;

            s.modulation.set(masterModIndex(MasterParamId::Feedback), BioSource::Breath, 0.8f);
            s.modulation.set(masterModIndex(MasterParamId::Lpf),      BioSource::Breath, 0.6f);
            break;

        case PresetId::DRIFT:
            setV(VoiceFxId::PAN,      true,  0.50f, 0.50f);
            setV(VoiceFxId::HPF,      true,  0.10f, 0.50f);
            setV(VoiceFxId::COMP,     false, 0.50f, 0.50f, false);
            setV(VoiceFxId::FOLD,     true,  0.40f, 0.60f);
            setV(VoiceFxId::BITCRUSH, true,  0.20f, 0.50f);
            setV(VoiceFxId::RINGMOD,  false, 0.50f, 0.50f, false);
            setV(VoiceFxId::CHORUS,   true,  0.40f, 0.70f);
            setV(VoiceFxId::REVERB,   true,  0.90f, 0.30f);
            setV(VoiceFxId::SHIMMER,  true,  0.60f, 0.70f);
            setV(VoiceFxId::DELAY,    true,  0.55f, 0.80f);
            setV(VoiceFxId::LPF,      true,  0.60f, 0.50f);

            setD(DroneFxId::LPF,      true,  0.40f, 0.70f);
            setD(DroneFxId::DRIVE,    true,  0.50f, 0.50f);
            setD(DroneFxId::FOLD,     false, 0.50f, 0.50f, false);
            setD(DroneFxId::BITCRUSH, false, 0.50f, 0.50f, false);
            setD(DroneFxId::RINGMOD,  false, 0.50f, 0.50f, false);
            setD(DroneFxId::CHORUS,   true,  0.30f, 0.60f);
            setD(DroneFxId::REVERB,   true,  0.70f, 0.60f);

            s.master.voiceOut = 1.0f;
            s.master.droneVol = 0.55f;
            s.master.fb       = 0.5f;
            s.master.lpf      = 10000.0f;

            s.modulation.set(voiceXyModIndex(VoiceFxId::REVERB, 0), BioSource::EyeX,  0.7f);
            s.modulation.set(droneXyModIndex(DroneFxId::LPF, 0),    BioSource::Heart, 0.5f);
            break;

        default:
            break;
    }
}

void cycleVoiceEffect(FullState& state, int dir) noexcept {
    const int n = static_cast<int>(kNumVoiceFx);
    int idx = findInOrder(state.voice.order, state.voice.active);
    idx = ((idx + dir) % n + n) % n;
    state.voice.active = state.voice.order[static_cast<size_t>(idx)];
}

void cycleDroneEffect(FullState& state, int dir) noexcept {
    const int n = static_cast<int>(kNumDroneFx);
    int idx = findInOrder(state.drone.order, state.drone.active);
    idx = ((idx + dir) % n + n) % n;
    state.drone.active = state.drone.order[static_cast<size_t>(idx)];
}

void toggleVoiceEffect(FullState& state) noexcept {
    const auto idx = static_cast<size_t>(state.voice.active);
    state.voice.enabled[idx] = !state.voice.enabled[idx];
}

void toggleDroneEffect(FullState& state) noexcept {
    const auto idx = static_cast<size_t>(state.drone.active);
    state.drone.enabled[idx] = !state.drone.enabled[idx];
}

void moveVoiceEffect(FullState& state, int dir) noexcept {
    const int i = findInOrder(state.voice.order, state.voice.active);
    const int j = i + dir;
    if (j < 0 || j >= static_cast<int>(kNumVoiceFx)) return;
    std::swap(state.voice.order[static_cast<size_t>(i)], state.voice.order[static_cast<size_t>(j)]);
}

void moveDroneEffect(FullState& state, int dir) noexcept {
    const int i = findInOrder(state.drone.order, state.drone.active);
    const int j = i + dir;
    if (j < 0 || j >= static_cast<int>(kNumDroneFx)) return;
    std::swap(state.drone.order[static_cast<size_t>(i)], state.drone.order[static_cast<size_t>(j)]);
}

void applyVoiceXY(FullState& state, float dx, float dy) noexcept {
    auto& xy = state.voice.xy[static_cast<size_t>(state.voice.active)];
    xy.x = clampf(xy.x + dx, 0.0f, 1.0f);
    xy.y = clampf(xy.y + dy, 0.0f, 1.0f);
}

void applyDroneXY(FullState& state, float dx, float dy) noexcept {
    auto& xy = state.drone.xy[static_cast<size_t>(state.drone.active)];
    xy.x = clampf(xy.x + dx, 0.0f, 1.0f);
    xy.y = clampf(xy.y + dy, 0.0f, 1.0f);
}

void setVoiceXY(FullState& state, VoiceFxId fx, float x, float y) noexcept {
    const auto idx = static_cast<size_t>(fx);
    if (idx < kNumVoiceFx) {
        state.voice.xy[idx] = {clampf(x, 0.0f, 1.0f), clampf(y, 0.0f, 1.0f)};
    }
}

void setDroneXY(FullState& state, DroneFxId fx, float x, float y) noexcept {
    const auto idx = static_cast<size_t>(fx);
    if (idx < kNumDroneFx) {
        state.drone.xy[idx] = {clampf(x, 0.0f, 1.0f), clampf(y, 0.0f, 1.0f)};
    }
}

void applyVoiceKnob(FullState& state, int knobIndex, float normalizedDelta) noexcept {
    if (knobIndex < 0 || knobIndex >= static_cast<int>(kMaxKnobsPerFx)) return;
    const VoiceFxId fx = state.voice.active;
    const FxDef& def = getVoiceFxDef(fx);
    if (knobIndex >= def.numKnobs) return;
    const KnobDef& kd = def.knobs[knobIndex];
    float& val = state.voice.knobs[static_cast<size_t>(fx)][static_cast<size_t>(knobIndex)];
    val = clampf(val + normalizedDelta * (kd.maxVal - kd.minVal), kd.minVal, kd.maxVal);
}

void applyDroneKnob(FullState& state, int knobIndex, float normalizedDelta) noexcept {
    if (knobIndex < 0 || knobIndex >= static_cast<int>(kMaxKnobsPerFx)) return;
    const DroneFxId fx = state.drone.active;
    const FxDef& def = getDroneFxDef(fx);
    if (knobIndex >= def.numKnobs) return;
    const KnobDef& kd = def.knobs[knobIndex];
    float& val = state.drone.knobs[static_cast<size_t>(fx)][static_cast<size_t>(knobIndex)];
    val = clampf(val + normalizedDelta * (kd.maxVal - kd.minVal), kd.minVal, kd.maxVal);
}

void setVoiceKnob(FullState& state, VoiceFxId fx, int knobIndex, float value) noexcept {
    const auto fxIdx = static_cast<size_t>(fx);
    if (fxIdx >= kNumVoiceFx || knobIndex < 0 || knobIndex >= static_cast<int>(kMaxKnobsPerFx)) return;
    const FxDef& def = getVoiceFxDef(fx);
    if (knobIndex >= def.numKnobs) return;
    const KnobDef& kd = def.knobs[knobIndex];
    state.voice.knobs[fxIdx][static_cast<size_t>(knobIndex)] = clampf(value, kd.minVal, kd.maxVal);
}

void setDroneKnob(FullState& state, DroneFxId fx, int knobIndex, float value) noexcept {
    const auto fxIdx = static_cast<size_t>(fx);
    if (fxIdx >= kNumDroneFx || knobIndex < 0 || knobIndex >= static_cast<int>(kMaxKnobsPerFx)) return;
    const FxDef& def = getDroneFxDef(fx);
    if (knobIndex >= def.numKnobs) return;
    const KnobDef& kd = def.knobs[knobIndex];
    state.drone.knobs[fxIdx][static_cast<size_t>(knobIndex)] = clampf(value, kd.minVal, kd.maxVal);
}

void selectMasterParam(FullState& state, int dir) noexcept {
    const int n = static_cast<int>(kNumMasterParams);
    state.master.selectedParam = ((state.master.selectedParam + dir) % n + n) % n;
}

void setMasterParam(FullState& state, MasterParamId param, float value) noexcept {
    const KnobDef& def = getMasterParamDef(param);
    const float clamped = clampf(value, def.minVal, def.maxVal);
    switch (param) {
        case MasterParamId::VoiceOut: state.master.voiceOut = clamped; break;
        case MasterParamId::DroneVol: state.master.droneVol = clamped; break;
        case MasterParamId::Feedback: state.master.fb       = clamped; break;
        case MasterParamId::Lpf:      state.master.lpf      = clamped; break;
        default: break;
    }
}

void adjustMasterParam(FullState& state, float normalizedDelta) noexcept {
    const auto param = static_cast<MasterParamId>(
        clampf(static_cast<float>(state.master.selectedParam), 0.0f, static_cast<float>(kNumMasterParams - 1)));
    const KnobDef& def = getMasterParamDef(param);
    float cur = 0.0f;
    switch (param) {
        case MasterParamId::VoiceOut: cur = state.master.voiceOut; break;
        case MasterParamId::DroneVol: cur = state.master.droneVol; break;
        case MasterParamId::Feedback: cur = state.master.fb;       break;
        case MasterParamId::Lpf:      cur = state.master.lpf;      break;
        default: break;
    }
    setMasterParam(state, param, cur + normalizedDelta * (def.maxVal - def.minVal));
}

void cycleModSource(FullState& state, uint16_t targetIdx) noexcept {
    if (targetIdx >= kNumModTargets) return;
    const ModEntry cur = state.modulation.get(targetIdx);
    const auto nextRaw = (static_cast<uint8_t>(cur.source) + 1) % static_cast<uint8_t>(BioSource::Count);
    const auto nextSrc = static_cast<BioSource>(nextRaw);
    if (nextSrc == BioSource::None) {
        state.modulation.set(targetIdx, BioSource::None, 0.0f);
    } else {
        const float d = (cur.depth > 0.0f) ? cur.depth : 0.5f;
        state.modulation.set(targetIdx, nextSrc, d);
    }
    state.lastModTarget = targetIdx;
}

void setModulation(FullState& state, uint16_t targetIdx, BioSource source, float depth) noexcept {
    if (targetIdx >= kNumModTargets) return;
    if (source == BioSource::None) {
        state.modulation.set(targetIdx, BioSource::None, 0.0f);
    } else {
        state.modulation.set(targetIdx, source, clampf(depth, 0.0f, 1.0f));
        state.lastModTarget = targetIdx;
    }
}

void adjustLastModDepth(FullState& state, float delta) noexcept {
    if (state.lastModTarget >= kNumModTargets) return;
    ModEntry cur = state.modulation.get(state.lastModTarget);
    if (cur.source == BioSource::None) return;
    cur.depth = clampf(cur.depth + delta, 0.0f, 1.0f);
    state.modulation.set(state.lastModTarget, cur.source, cur.depth);
}

float getBioValue(const BioState& bio, BioSource source) noexcept {
    switch (source) {
        case BioSource::Breath: return clampf(bio.breath, 0.0f, 1.0f);
        case BioSource::Heart:  return clampf(bio.heart,  0.0f, 1.0f);
        case BioSource::EyeX:   return clampf(bio.eyeX,   0.0f, 1.0f);
        case BioSource::EyeY:   return clampf(bio.eyeY,   0.0f, 1.0f);
        default:                return 0.5f;
    }
}

float effectiveValue(float baseVal,
                     float minVal,
                     float maxVal,
                     BioSource source,
                     float depth,
                     const BioState& bio) noexcept {
    const float clampedBase = clampf(baseVal, minVal, maxVal);
    if (source == BioSource::None || depth == 0.0f) {
        return clampedBase;
    }
    const float bioVal = getBioValue(bio, source);
    // Centered around bio == 0.5 -> 0 offset; bio == 0 -> -0.5*depth*range; bio == 1 -> +0.5*depth*range
    const float offset = (bioVal - 0.5f) * clampf(depth, 0.0f, 1.0f) * (maxVal - minVal);
    return clampf(clampedBase + offset, minVal, maxVal);
}

NoiseVoiceParams resolveStepVoice(const NoiseVoiceParams& base, const StepLocks& locks) noexcept {
    if (locks.mask == 0) return base;
    NoiseVoiceParams r = base;
    if (locks.has(NoiseLockField::Type))         r.type         = locks.values.type;
    if (locks.has(NoiseLockField::Cutoff))       r.cutoff       = locks.values.cutoff;
    if (locks.has(NoiseLockField::Resonance))    r.resonance    = locks.values.resonance;
    if (locks.has(NoiseLockField::AmpAttack))    r.ampAttack    = locks.values.ampAttack;
    if (locks.has(NoiseLockField::AmpDecay))     r.ampDecay     = locks.values.ampDecay;
    if (locks.has(NoiseLockField::ModEnvAmount)) r.modEnvAmount = locks.values.modEnvAmount;
    if (locks.has(NoiseLockField::Lfo1Rate))     r.lfo1.rate    = locks.values.lfo1.rate;
    if (locks.has(NoiseLockField::Lfo1Depth))    r.lfo1.depth   = locks.values.lfo1.depth;
    if (locks.has(NoiseLockField::Lfo1Target))   r.lfo1.target  = locks.values.lfo1.target;
    if (locks.has(NoiseLockField::Lfo2Rate))     r.lfo2.rate    = locks.values.lfo2.rate;
    if (locks.has(NoiseLockField::Lfo2Depth))    r.lfo2.depth   = locks.values.lfo2.depth;
    if (locks.has(NoiseLockField::Lfo2Target))   r.lfo2.target  = locks.values.lfo2.target;
    if (locks.has(NoiseLockField::Pan))          r.pan          = locks.values.pan;
    if (locks.has(NoiseLockField::Gain))         r.gain         = locks.values.gain;
    return r;
}

void setNoiseVoiceParamAndLock(NoiseTrack& track, int stepIdx, NoiseLockField field, float value) noexcept {
    auto applyToParams = [&](NoiseVoiceParams& vp) {
        switch (field) {
            case NoiseLockField::Type: {
                const int ti = static_cast<int>(std::lround(clampf(value, 0.0f, 2.0f)));
                vp.type = static_cast<NoiseType>(ti);
                break;
            }
            case NoiseLockField::Cutoff:       vp.cutoff       = clampf(value, 20.0f, 12000.0f); break;
            case NoiseLockField::Resonance:    vp.resonance    = clampf(value, 0.0f, 0.95f);     break;
            case NoiseLockField::AmpAttack:    vp.ampAttack    = clampf(value, 0.001f, 1.0f);    break;
            case NoiseLockField::AmpDecay:     vp.ampDecay     = clampf(value, 0.01f, 2.0f);     break;
            case NoiseLockField::ModEnvAmount: vp.modEnvAmount = clampf(value, 0.0f, 1.0f);      break;
            case NoiseLockField::Lfo1Rate:     vp.lfo1.rate    = clampf(value, 0.05f, 20.0f);    break;
            case NoiseLockField::Lfo1Depth:    vp.lfo1.depth   = clampf(value, 0.0f, 1.0f);      break;
            case NoiseLockField::Lfo1Target: {
                const int tg = static_cast<int>(std::lround(clampf(value, 0.0f, 2.0f)));
                vp.lfo1.target = static_cast<LfoTarget>(tg);
                break;
            }
            case NoiseLockField::Lfo2Rate:     vp.lfo2.rate    = clampf(value, 0.05f, 20.0f);    break;
            case NoiseLockField::Lfo2Depth:    vp.lfo2.depth   = clampf(value, 0.0f, 1.0f);      break;
            case NoiseLockField::Lfo2Target: {
                const int tg = static_cast<int>(std::lround(clampf(value, 0.0f, 2.0f)));
                vp.lfo2.target = static_cast<LfoTarget>(tg);
                break;
            }
            case NoiseLockField::Pan:          vp.pan          = clampf(value, -1.0f, 1.0f);     break;
            case NoiseLockField::Gain:         vp.gain         = clampf(value, 0.0f, 1.0f);      break;
            default: break;
        }
    };

    applyToParams(track.voice);
    if (stepIdx >= 0 && stepIdx < static_cast<int>(kNumSeqSteps)) {
        StepLocks& lk = track.steps[static_cast<size_t>(stepIdx)].locks;
        applyToParams(lk.values);
        lk.setFlag(field);
    }
}

bool parseNoiseLockField(std::string_view name, NoiseLockField& out) noexcept {
    if (name == "type")         { out = NoiseLockField::Type;         return true; }
    if (name == "cutoff")       { out = NoiseLockField::Cutoff;       return true; }
    if (name == "resonance")    { out = NoiseLockField::Resonance;    return true; }
    if (name == "ampAttack")    { out = NoiseLockField::AmpAttack;    return true; }
    if (name == "ampDecay")     { out = NoiseLockField::AmpDecay;     return true; }
    if (name == "modEnvAmount") { out = NoiseLockField::ModEnvAmount; return true; }
    if (name == "lfo1Rate")     { out = NoiseLockField::Lfo1Rate;     return true; }
    if (name == "lfo1Depth")    { out = NoiseLockField::Lfo1Depth;    return true; }
    if (name == "lfo1Target")   { out = NoiseLockField::Lfo1Target;   return true; }
    if (name == "lfo2Rate")     { out = NoiseLockField::Lfo2Rate;     return true; }
    if (name == "lfo2Depth")    { out = NoiseLockField::Lfo2Depth;    return true; }
    if (name == "lfo2Target")   { out = NoiseLockField::Lfo2Target;   return true; }
    if (name == "pan")          { out = NoiseLockField::Pan;          return true; }
    if (name == "gain")         { out = NoiseLockField::Gain;         return true; }
    return false;
}

const char* toString(NoiseLockField field) noexcept {
    switch (field) {
        case NoiseLockField::Type:         return "type";
        case NoiseLockField::Cutoff:       return "cutoff";
        case NoiseLockField::Resonance:    return "resonance";
        case NoiseLockField::AmpAttack:    return "ampAttack";
        case NoiseLockField::AmpDecay:     return "ampDecay";
        case NoiseLockField::ModEnvAmount: return "modEnvAmount";
        case NoiseLockField::Lfo1Rate:     return "lfo1Rate";
        case NoiseLockField::Lfo1Depth:    return "lfo1Depth";
        case NoiseLockField::Lfo1Target:   return "lfo1Target";
        case NoiseLockField::Lfo2Rate:     return "lfo2Rate";
        case NoiseLockField::Lfo2Depth:    return "lfo2Depth";
        case NoiseLockField::Lfo2Target:   return "lfo2Target";
        case NoiseLockField::Pan:          return "pan";
        case NoiseLockField::Gain:         return "gain";
        default:                           return "cutoff";
    }
}

} // namespace biomus
