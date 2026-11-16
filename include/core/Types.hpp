#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>

namespace biomus {

// ============================================================================
// Fixed Real-Time Safe Identifiers (Zero heap allocations in audio thread)
// ============================================================================

enum class VoiceFxId : uint8_t {
    PAN = 0,
    HPF,
    COMP,
    FOLD,
    BITCRUSH,
    RINGMOD,
    CHORUS,
    REVERB,
    SHIMMER,
    DELAY,
    LPF,
    Count
};

constexpr size_t kNumVoiceFx = static_cast<size_t>(VoiceFxId::Count); // 11

enum class DroneFxId : uint8_t {
    LPF = 0,
    DRIVE,
    FOLD,
    BITCRUSH,
    RINGMOD,
    CHORUS,
    REVERB,
    Count
};

constexpr size_t kNumDroneFx = static_cast<size_t>(DroneFxId::Count); // 7

enum class BioSource : uint8_t {
    None = 0,
    Breath,
    Heart,
    EyeX,
    EyeY,
    Count
};

enum class PresetId : uint8_t {
    VOICE = 0,
    WALL,
    AMBIENT,
    NOISE,
    CLEAN,
    BREATH,
    DRIFT,
    Count
};

constexpr size_t kNumPresets = static_cast<size_t>(PresetId::Count); // 7

enum class KnobMode : uint8_t {
    XY = 0,
    PARAM
};

enum class MasterParamId : uint8_t {
    VoiceOut = 0,
    DroneVol,
    Feedback,
    Lpf,
    Count
};

constexpr size_t kNumMasterParams = static_cast<size_t>(MasterParamId::Count); // 4

enum class NoiseType : uint8_t {
    White = 0,
    Pink,
    Brown
};

enum class LfoTarget : uint8_t {
    Cutoff = 0,
    Pitch,
    Pan
};

constexpr size_t kNumNoiseTracks = 4;
constexpr size_t kNumSeqSteps = 16;
constexpr size_t kMaxKnobsPerFx = 2;

// ============================================================================
// Flat Modulation Target Indexing (O(1) lookup in audio thread)
// Layout:
//   [0 .. 21]   Voice FX XY     (11 * 2: x, y)
//   [22 .. 43]  Voice FX Knobs  (11 * 2: knob0, knob1)
//   [44 .. 57]  Drone FX XY     (7 * 2:  x, y)
//   [58 .. 71]  Drone FX Knobs  (7 * 2:  knob0, knob1)
//   [72 .. 75]  Master Params   (4: voiceOut, droneVol, fb, lpf)
// ============================================================================

constexpr uint16_t kInvalidModTarget = 0xFFFF;
constexpr size_t kVoiceXyModBase    = 0;
constexpr size_t kVoiceKnobModBase  = kVoiceXyModBase + kNumVoiceFx * 2;     // 22
constexpr size_t kDroneXyModBase    = kVoiceKnobModBase + kNumVoiceFx * 2;   // 44
constexpr size_t kDroneKnobModBase  = kDroneXyModBase + kNumDroneFx * 2;     // 58
constexpr size_t kMasterModBase     = kDroneKnobModBase + kNumDroneFx * 2;   // 72
constexpr size_t kNumModTargets     = kMasterModBase + kNumMasterParams;     // 76

constexpr uint16_t voiceXyModIndex(VoiceFxId fx, int axis) noexcept {
    return static_cast<uint16_t>(kVoiceXyModBase + static_cast<size_t>(fx) * 2 + (axis & 1));
}

constexpr uint16_t voiceKnobModIndex(VoiceFxId fx, int knobIdx) noexcept {
    return static_cast<uint16_t>(kVoiceKnobModBase + static_cast<size_t>(fx) * 2 + (knobIdx & 1));
}

constexpr uint16_t droneXyModIndex(DroneFxId fx, int axis) noexcept {
    return static_cast<uint16_t>(kDroneXyModBase + static_cast<size_t>(fx) * 2 + (axis & 1));
}

constexpr uint16_t droneKnobModIndex(DroneFxId fx, int knobIdx) noexcept {
    return static_cast<uint16_t>(kDroneKnobModBase + static_cast<size_t>(fx) * 2 + (knobIdx & 1));
}

constexpr uint16_t masterModIndex(MasterParamId param) noexcept {
    return static_cast<uint16_t>(kMasterModBase + static_cast<size_t>(param));
}

// ============================================================================
// Knob Metadata Descriptors (Compile-time static tables)
// ============================================================================

struct KnobDef {
    const char* key;
    const char* label;
    float minVal;
    float maxVal;
    float defaultVal;
};

struct FxDef {
    const char* name;
    uint8_t numKnobs;
    KnobDef knobs[kMaxKnobsPerFx];
};

const FxDef& getVoiceFxDef(VoiceFxId id) noexcept;
const FxDef& getDroneFxDef(DroneFxId id) noexcept;
const KnobDef& getMasterParamDef(MasterParamId id) noexcept;

// String <-> Enum conversions (for JSON / WebSocket / CLI boundaries only)
const char* toString(VoiceFxId id) noexcept;
const char* toString(DroneFxId id) noexcept;
const char* toString(BioSource src) noexcept;
const char* toString(PresetId preset) noexcept;
const char* toString(KnobMode mode) noexcept;
const char* toString(MasterParamId param) noexcept;
const char* toString(NoiseType type) noexcept;
const char* toString(LfoTarget tgt) noexcept;

bool parseVoiceFxId(std::string_view s, VoiceFxId& out) noexcept;
bool parseDroneFxId(std::string_view s, DroneFxId& out) noexcept;
bool parseBioSource(std::string_view s, BioSource& out) noexcept;
bool parsePresetId(std::string_view s, PresetId& out) noexcept;
bool parseMasterParamId(std::string_view s, MasterParamId& out) noexcept;
bool parseNoiseType(std::string_view s, NoiseType& out) noexcept;
bool parseLfoTarget(std::string_view s, LfoTarget& out) noexcept;

// Parses e.g. "voice:REVERB:x", "voice:CHORUS:mix", "drone:LPF:x", "master:fb"
uint16_t parseModTarget(std::string_view targetStr) noexcept;
// Formats a ModTarget index back into buffer; returns length written
size_t formatModTarget(uint16_t targetIdx, char* buf, size_t bufCap) noexcept;

} // namespace biomus
