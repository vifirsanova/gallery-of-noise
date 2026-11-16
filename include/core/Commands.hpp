#pragma once

#include "core/State.hpp"
#include <cstdint>
#include <type_traits>

namespace biomus {

enum class CommandType : uint8_t {
    ApplyPreset = 0,
    CycleVoiceFx,
    CycleDroneFx,
    SelectVoiceFx,
    SelectDroneFx,
    ToggleVoiceFx,
    ToggleDroneFx,
    SetVoiceFxEnabled,
    SetDroneFxEnabled,
    MoveVoiceFx,
    MoveDroneFx,
    AdjustVoiceXY,
    AdjustDroneXY,
    SetVoiceXY,
    SetDroneXY,
    AdjustVoiceKnob,
    AdjustDroneKnob,
    SetVoiceKnob,
    SetDroneKnob,
    SelectMasterParam,
    AdjustMasterParam,
    SetMasterParam,
    CycleModSource,
    SetModulation,
    AdjustModDepth,
    SetActiveBio,
    ToggleKnobMode,
    SetKnobMode,
    ToggleMic,
    ToggleDrone,
    SetFileOn,
    ToggleBypass,
    SetFlag,
    // Noise sequencer & voice commands
    NoiseSetPlaying,
    NoiseTogglePlay,
    NoiseSetTempo,
    NoiseSelectTrack,
    NoiseSelectStep,
    NoiseToggleStep,
    NoiseSetStepTrigger,
    NoiseSetVoiceParam,
    NoiseClearStepLocks,
    // External Bio Sensor Injection (for Stage 2/3 OpenCV / Polar H10 / Breath ADC)
    SetExternalBio
};

struct Command {
    CommandType type = CommandType::ApplyPreset;
    int32_t i0 = 0;
    int32_t i1 = 0;
    float f0 = 0.0f;
    float f1 = 0.0f;
};

static_assert(std::is_trivially_copyable_v<Command>,
              "Command must be trivially copyable for lock-free SPSC queue");

// Applies a single command directly to FullState in O(1) with zero heap allocations
void applyCommand(FullState& state, const Command& cmd) noexcept;

} // namespace biomus
