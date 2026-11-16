#include "core/Commands.hpp"
#include <algorithm>

namespace biomus {

namespace {
inline float clampf(float v, float lo, float hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}
} // namespace

void applyCommand(FullState& state, const Command& cmd) noexcept {
    switch (cmd.type) {
        case CommandType::ApplyPreset: {
            if (cmd.i0 >= 0 && cmd.i0 < static_cast<int32_t>(kNumPresets)) {
                applyPreset(state, static_cast<PresetId>(cmd.i0));
            }
            break;
        }
        case CommandType::CycleVoiceFx:
            cycleVoiceEffect(state, cmd.i0);
            break;
        case CommandType::CycleDroneFx:
            cycleDroneEffect(state, cmd.i0);
            break;
        case CommandType::SelectVoiceFx:
            if (cmd.i0 >= 0 && cmd.i0 < static_cast<int32_t>(kNumVoiceFx)) {
                state.voice.active = static_cast<VoiceFxId>(cmd.i0);
            }
            break;
        case CommandType::SelectDroneFx:
            if (cmd.i0 >= 0 && cmd.i0 < static_cast<int32_t>(kNumDroneFx)) {
                state.drone.active = static_cast<DroneFxId>(cmd.i0);
            }
            break;
        case CommandType::ToggleVoiceFx:
            toggleVoiceEffect(state);
            break;
        case CommandType::ToggleDroneFx:
            toggleDroneEffect(state);
            break;
        case CommandType::SetVoiceFxEnabled:
            if (cmd.i0 >= 0 && cmd.i0 < static_cast<int32_t>(kNumVoiceFx)) {
                state.voice.enabled[static_cast<size_t>(cmd.i0)] = (cmd.i1 != 0);
            }
            break;
        case CommandType::SetDroneFxEnabled:
            if (cmd.i0 >= 0 && cmd.i0 < static_cast<int32_t>(kNumDroneFx)) {
                state.drone.enabled[static_cast<size_t>(cmd.i0)] = (cmd.i1 != 0);
            }
            break;
        case CommandType::MoveVoiceFx:
            moveVoiceEffect(state, cmd.i0);
            break;
        case CommandType::MoveDroneFx:
            moveDroneEffect(state, cmd.i0);
            break;
        case CommandType::AdjustVoiceXY:
            applyVoiceXY(state, cmd.f0, cmd.f1);
            break;
        case CommandType::AdjustDroneXY:
            applyDroneXY(state, cmd.f0, cmd.f1);
            break;
        case CommandType::SetVoiceXY:
            if (cmd.i0 >= 0 && cmd.i0 < static_cast<int32_t>(kNumVoiceFx)) {
                setVoiceXY(state, static_cast<VoiceFxId>(cmd.i0), cmd.f0, cmd.f1);
            }
            break;
        case CommandType::SetDroneXY:
            if (cmd.i0 >= 0 && cmd.i0 < static_cast<int32_t>(kNumDroneFx)) {
                setDroneXY(state, static_cast<DroneFxId>(cmd.i0), cmd.f0, cmd.f1);
            }
            break;
        case CommandType::AdjustVoiceKnob:
            applyVoiceKnob(state, cmd.i0, cmd.f0);
            break;
        case CommandType::AdjustDroneKnob:
            applyDroneKnob(state, cmd.i0, cmd.f0);
            break;
        case CommandType::SetVoiceKnob:
            if (cmd.i0 >= 0 && cmd.i0 < static_cast<int32_t>(kNumVoiceFx)) {
                setVoiceKnob(state, static_cast<VoiceFxId>(cmd.i0), cmd.i1, cmd.f0);
            }
            break;
        case CommandType::SetDroneKnob:
            if (cmd.i0 >= 0 && cmd.i0 < static_cast<int32_t>(kNumDroneFx)) {
                setDroneKnob(state, static_cast<DroneFxId>(cmd.i0), cmd.i1, cmd.f0);
            }
            break;
        case CommandType::SelectMasterParam:
            selectMasterParam(state, cmd.i0);
            break;
        case CommandType::AdjustMasterParam:
            adjustMasterParam(state, cmd.f0);
            break;
        case CommandType::SetMasterParam:
            if (cmd.i0 >= 0 && cmd.i0 < static_cast<int32_t>(kNumMasterParams)) {
                setMasterParam(state, static_cast<MasterParamId>(cmd.i0), cmd.f0);
            }
            break;
        case CommandType::CycleModSource:
            cycleModSource(state, static_cast<uint16_t>(cmd.i0));
            break;
        case CommandType::SetModulation:
            if (cmd.i1 >= 0 && cmd.i1 < static_cast<int32_t>(BioSource::Count)) {
                setModulation(state, static_cast<uint16_t>(cmd.i0), static_cast<BioSource>(cmd.i1), cmd.f0);
            }
            break;
        case CommandType::AdjustModDepth:
            adjustLastModDepth(state, cmd.f0);
            break;
        case CommandType::SetActiveBio:
            if (cmd.i0 >= 1 && cmd.i0 < static_cast<int32_t>(BioSource::Count)) {
                state.activeBio = static_cast<BioSource>(cmd.i0);
            }
            break;
        case CommandType::ToggleKnobMode:
            state.knobMode = (state.knobMode == KnobMode::XY) ? KnobMode::PARAM : KnobMode::XY;
            break;
        case CommandType::SetKnobMode:
            state.knobMode = (cmd.i0 == 1) ? KnobMode::PARAM : KnobMode::XY;
            break;
        case CommandType::ToggleMic:
            state.mic = !state.mic;
            break;
        case CommandType::ToggleDrone:
            state.droneOn = !state.droneOn;
            break;
        case CommandType::SetFileOn:
            state.fileOn = (cmd.i0 != 0);
            break;
        case CommandType::ToggleBypass:
            state.bypass = !state.bypass;
            break;
        case CommandType::SetFlag:
            // i0: 0=mic, 1=droneOn, 2=fileOn, 3=bypass; i1: 0/1
            if (cmd.i0 == 0) state.mic = (cmd.i1 != 0);
            else if (cmd.i0 == 1) state.droneOn = (cmd.i1 != 0);
            else if (cmd.i0 == 2) state.fileOn = (cmd.i1 != 0);
            else if (cmd.i0 == 3) state.bypass = (cmd.i1 != 0);
            break;

        // Noise sequencer & voice commands
        case CommandType::NoiseSetPlaying:
            state.noise.playing = (cmd.i0 != 0);
            break;
        case CommandType::NoiseTogglePlay:
            state.noise.playing = !state.noise.playing;
            break;
        case CommandType::NoiseSetTempo:
            state.noise.tempo = clampf(cmd.f0, 30.0f, 300.0f);
            break;
        case CommandType::NoiseSelectTrack: {
            const int n = static_cast<int>(kNumNoiseTracks);
            if (cmd.i1 != 0) {
                // relative cycle
                state.noise.activeTrack = ((state.noise.activeTrack + cmd.i0) % n + n) % n;
            } else if (cmd.i0 >= 0 && cmd.i0 < n) {
                state.noise.activeTrack = cmd.i0;
            }
            state.noise.selectedStep = 0;
            break;
        }
        case CommandType::NoiseSelectStep: {
            const int n = static_cast<int>(kNumSeqSteps);
            if (cmd.i1 != 0) {
                state.noise.selectedStep = ((state.noise.selectedStep + cmd.i0) % n + n) % n;
            } else if (cmd.i0 >= 0 && cmd.i0 < n) {
                state.noise.selectedStep = cmd.i0;
            }
            break;
        }
        case CommandType::NoiseToggleStep: {
            const int tr = (cmd.i0 >= 0 && cmd.i0 < static_cast<int>(kNumNoiseTracks))
                               ? cmd.i0 : state.noise.activeTrack;
            const int st = (cmd.i1 >= 0 && cmd.i1 < static_cast<int>(kNumSeqSteps))
                               ? cmd.i1 : state.noise.selectedStep;
            auto& step = state.noise.tracks[static_cast<size_t>(tr)].steps[static_cast<size_t>(st)];
            step.trigger = !step.trigger;
            state.noise.selectedStep = st;
            break;
        }
        case CommandType::NoiseSetStepTrigger: {
            if (cmd.i0 >= 0 && cmd.i0 < static_cast<int>(kNumNoiseTracks) &&
                cmd.i1 >= 0 && cmd.i1 < static_cast<int>(kNumSeqSteps)) {
                state.noise.tracks[static_cast<size_t>(cmd.i0)]
                    .steps[static_cast<size_t>(cmd.i1)].trigger = (cmd.f0 > 0.5f);
            }
            break;
        }
        case CommandType::NoiseSetVoiceParam: {
            // i0 = packed (trackIdx << 8) | stepIdx (or -1 for activeTrack / selectedStep)
            // i1 = NoiseLockField
            int tr = (cmd.i0 >> 8) & 0xFF;
            int st = cmd.i0 & 0xFF;
            if (tr >= static_cast<int>(kNumNoiseTracks)) tr = state.noise.activeTrack;
            if (st >= static_cast<int>(kNumSeqSteps)) st = state.noise.selectedStep;
            if (cmd.i1 >= 0 && cmd.i1 < static_cast<int>(kNumNoiseLockFields)) {
                setNoiseVoiceParamAndLock(state.noise.tracks[static_cast<size_t>(tr)],
                                          st,
                                          static_cast<NoiseLockField>(cmd.i1),
                                          cmd.f0);
            }
            break;
        }
        case CommandType::NoiseClearStepLocks: {
            const int tr = (cmd.i0 >= 0 && cmd.i0 < static_cast<int>(kNumNoiseTracks))
                               ? cmd.i0 : state.noise.activeTrack;
            const int st = (cmd.i1 >= 0 && cmd.i1 < static_cast<int>(kNumSeqSteps))
                               ? cmd.i1 : state.noise.selectedStep;
            state.noise.tracks[static_cast<size_t>(tr)].steps[static_cast<size_t>(st)].locks.clear();
            break;
        }
        case CommandType::SetExternalBio: {
            // i0 = channel: 0=disable external, 1=breath, 2=heart(f0=pulse,f1=bpm), 3=eye(f0=x,f1=y)
            if (cmd.i0 == 0) {
                state.bio.externalBreath = false;
                state.bio.externalHeart = false;
                state.bio.externalEye = false;
            } else if (cmd.i0 == 1) {
                state.bio.externalBreath = true;
                state.bio.breath = clampf(cmd.f0, 0.0f, 1.0f);
            } else if (cmd.i0 == 2) {
                state.bio.externalHeart = true;
                state.bio.heart = clampf(cmd.f0, 0.0f, 1.0f);
                if (cmd.f1 > 20.0f) state.bio.bpm = static_cast<int>(cmd.f1);
            } else if (cmd.i0 == 3) {
                state.bio.externalEye = true;
                state.bio.eyeX = clampf(cmd.f0, 0.0f, 1.0f);
                state.bio.eyeY = clampf(cmd.f1, 0.0f, 1.0f);
            }
            break;
        }
    }
}

} // namespace biomus
