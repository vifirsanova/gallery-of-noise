#pragma once

#include "core/Types.hpp"
#include <array>
#include <cstdint>
#include <type_traits>

namespace biomus {

struct XY {
    float x = 0.5f;
    float y = 0.5f;
};

template <typename FxEnum, size_t N>
struct ChainStateT {
    std::array<FxEnum, N> order{};
    std::array<bool, N> enabled{};
    FxEnum active{};
    std::array<XY, N> xy{};
    std::array<std::array<float, kMaxKnobsPerFx>, N> knobs{};
};

using VoiceState = ChainStateT<VoiceFxId, kNumVoiceFx>;
using DroneState = ChainStateT<DroneFxId, kNumDroneFx>;

// Lockable parameters on each 16-step noise sequencer step (zero std::map!)
enum class NoiseLockField : uint8_t {
    Type = 0,
    Cutoff,
    Resonance,
    AmpAttack,
    AmpDecay,
    ModEnvAmount,
    Lfo1Rate,
    Lfo1Depth,
    Lfo1Target,
    Lfo2Rate,
    Lfo2Depth,
    Lfo2Target,
    Pan,
    Gain,
    Count
};

constexpr size_t kNumNoiseLockFields = static_cast<size_t>(NoiseLockField::Count); // 14

struct LfoConfig {
    float rate = 0.5f;
    float depth = 0.3f;
    LfoTarget target = LfoTarget::Cutoff;
};

struct NoiseVoiceParams {
    NoiseType type = NoiseType::White;
    float cutoff = 1200.0f;
    float resonance = 0.4f;
    float ampAttack = 0.005f;
    float ampDecay = 0.3f;
    float modEnvAmount = 0.5f;
    LfoConfig lfo1{0.5f, 0.3f, LfoTarget::Cutoff};
    LfoConfig lfo2{4.0f, 0.2f, LfoTarget::Pitch};
    float pan = 0.0f;
    float gain = 0.7f;
};

struct StepLocks {
    uint16_t mask = 0; // Bitmask indexed by NoiseLockField
    NoiseVoiceParams values{};

    constexpr bool has(NoiseLockField f) const noexcept {
        return (mask & (1u << static_cast<uint8_t>(f))) != 0;
    }

    constexpr void setFlag(NoiseLockField f) noexcept {
        mask |= static_cast<uint16_t>(1u << static_cast<uint8_t>(f));
    }

    constexpr void clear() noexcept {
        mask = 0;
    }

    int count() const noexcept {
        int c = 0;
        uint16_t m = mask;
        while (m) {
            c += (m & 1u);
            m >>= 1u;
        }
        return c;
    }
};

struct Step {
    bool trigger = false;
    StepLocks locks{};
};

struct NoiseTrack {
    NoiseVoiceParams voice{};
    std::array<Step, kNumSeqSteps> steps{};
    int currentStep = 0;
};

struct NoiseState {
    bool playing = false;
    float tempo = 120.0f;
    int activeTrack = 0;
    int selectedStep = 0;
    std::array<NoiseTrack, kNumNoiseTracks> tracks{};
};

struct MasterState {
    float voiceOut = 1.0f;
    float droneVol = 0.5f;
    float fb = 0.0f;
    float lpf = 18000.0f;
    int selectedParam = 0;
};

struct BioState {
    float breath = 0.0f;
    float heart = 0.0f;
    float eyeX = 0.5f;
    float eyeY = 0.5f;
    int bpm = 72;
    bool externalBreath = false;
    bool externalHeart = false;
    bool externalEye = false;
};

struct ModEntry {
    BioSource source = BioSource::None;
    float depth = 0.0f;
};

struct ModulationMatrix {
    std::array<ModEntry, kNumModTargets> entries{};

    ModEntry get(uint16_t idx) const noexcept {
        if (idx >= kNumModTargets) return {};
        return entries[idx];
    }

    void set(uint16_t idx, BioSource src, float depth) noexcept {
        if (idx >= kNumModTargets) return;
        if (src == BioSource::None) {
            entries[idx] = {BioSource::None, 0.0f};
        } else {
            entries[idx] = {src, depth};
        }
    }

    void clearAll() noexcept {
        for (auto& e : entries) {
            e = {BioSource::None, 0.0f};
        }
    }
};

struct FullState {
    VoiceState voice{};
    DroneState drone{};
    NoiseState noise{};
    MasterState master{};
    BioState bio{};
    ModulationMatrix modulation{};

    PresetId preset = PresetId::VOICE;
    bool mic = true;
    bool droneOn = false;
    bool fileOn = false;
    bool bypass = false;
    KnobMode knobMode = KnobMode::XY;
    BioSource activeBio = BioSource::Breath;
    uint16_t lastModTarget = kInvalidModTarget;

    // Live metering outputs written by audio engine
    float masterLevel = 0.0f;
    bool clip = false;
};

static_assert(std::is_trivially_copyable_v<FullState>,
              "FullState must remain trivially copyable for lock-free real-time snapshots");

// ============================================================================
// State Initialization & Parameter Manipulation Functions (Section 4.1 & 7.1)
// ============================================================================

FullState makeDefaultState() noexcept;

void applyPreset(FullState& state, PresetId preset) noexcept;

void cycleVoiceEffect(FullState& state, int dir) noexcept;
void cycleDroneEffect(FullState& state, int dir) noexcept;

void toggleVoiceEffect(FullState& state) noexcept;
void toggleDroneEffect(FullState& state) noexcept;

void moveVoiceEffect(FullState& state, int dir) noexcept;
void moveDroneEffect(FullState& state, int dir) noexcept;

void applyVoiceXY(FullState& state, float dx, float dy) noexcept;
void applyDroneXY(FullState& state, float dx, float dy) noexcept;
void setVoiceXY(FullState& state, VoiceFxId fx, float x, float y) noexcept;
void setDroneXY(FullState& state, DroneFxId fx, float x, float y) noexcept;

void applyVoiceKnob(FullState& state, int knobIndex, float normalizedDelta) noexcept;
void applyDroneKnob(FullState& state, int knobIndex, float normalizedDelta) noexcept;
void setVoiceKnob(FullState& state, VoiceFxId fx, int knobIndex, float value) noexcept;
void setDroneKnob(FullState& state, DroneFxId fx, int knobIndex, float value) noexcept;

void selectMasterParam(FullState& state, int dir) noexcept;
void adjustMasterParam(FullState& state, float normalizedDelta) noexcept;
void setMasterParam(FullState& state, MasterParamId param, float value) noexcept;

// Modulation helpers
void cycleModSource(FullState& state, uint16_t targetIdx) noexcept;
void setModulation(FullState& state, uint16_t targetIdx, BioSource source, float depth) noexcept;
void adjustLastModDepth(FullState& state, float delta) noexcept;

// Computes modulated parameter value clamped to [minVal, maxVal].
// Per spec 7.1:
//  - source == None -> baseVal
//  - depth == 0     -> baseVal
//  - bio == 0.5     -> baseVal
//  - bio == 0 / 1   -> shifts by -/+ 0.5 * depth * (maxVal - minVal), clamped
float effectiveValue(float baseVal,
                     float minVal,
                     float maxVal,
                     BioSource source,
                     float depth,
                     const BioState& bio) noexcept;

float getBioValue(const BioState& bio, BioSource source) noexcept;

// Noise parameter-lock application helper
NoiseVoiceParams resolveStepVoice(const NoiseVoiceParams& base, const StepLocks& locks) noexcept;
void setNoiseVoiceParamAndLock(NoiseTrack& track, int stepIdx, NoiseLockField field, float value) noexcept;
bool parseNoiseLockField(std::string_view name, NoiseLockField& out) noexcept;
const char* toString(NoiseLockField field) noexcept;

} // namespace biomus
