#include "app/MidiController.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

#ifdef BIOMUSIC_HAVE_RTMIDI
#include <RtMidi.h>
#endif

namespace biomus {

struct MidiController::Impl {
#ifdef BIOMUSIC_HAVE_RTMIDI
    std::unique_ptr<RtMidiIn> midiIn;
#endif
};

MidiController::MidiController(Engine& engine)
    : engine_(engine), impl_(std::make_unique<Impl>()) {}

MidiController::~MidiController() {
    stop();
}

bool MidiController::start(const std::string& preferredPortSubstring) {
    if (running_.load()) return true;

#ifdef BIOMUSIC_HAVE_RTMIDI
    try {
        impl_->midiIn = std::make_unique<RtMidiIn>(RtMidi::LINUX_ALSA, "VoiceBoard");
        impl_->midiIn->ignoreTypes(true, true, true);

        impl_->midiIn->setCallback(
            [](double /*timeStamp*/, std::vector<unsigned char>* message, void* userData) {
                if (!message || message->empty() || !userData) return;
                auto* self = static_cast<MidiController*>(userData);
                self->handleMidiBytes(message->data(), message->size());
            },
            this);

        const unsigned int nPorts = impl_->midiIn->getPortCount();
        int matchedPort = -1;
        for (unsigned int i = 0; i < nPorts; ++i) {
            const std::string name = impl_->midiIn->getPortName(i);
            if (!preferredPortSubstring.empty() &&
                name.find(preferredPortSubstring) != std::string::npos) {
                matchedPort = static_cast<int>(i);
                portName_ = name;
                break;
            }
        }

        if (matchedPort >= 0) {
            impl_->midiIn->openPort(static_cast<unsigned int>(matchedPort), "VoiceBoard In");
        } else {
            impl_->midiIn->openVirtualPort("VoiceBoard MIDI In");
            portName_ = "Virtual: VoiceBoard MIDI In";
        }

        running_.store(true);
        return true;
    } catch (const RtMidiError&) {
        impl_->midiIn.reset();
        portName_ = "MIDI unavailable (no ALSA sequencer)";
        running_.store(false);
        return false;
    }
#else
    (void)preferredPortSubstring;
    portName_ = "RtMidi disabled at build time";
    return false;
#endif
}

void MidiController::stop() {
    if (!running_.exchange(false)) return;
#ifdef BIOMUSIC_HAVE_RTMIDI
    if (impl_ && impl_->midiIn) {
        try {
            impl_->midiIn->cancelCallback();
            impl_->midiIn->closePort();
        } catch (...) {
        }
        impl_->midiIn.reset();
    }
#endif
}

void MidiController::handleMidiBytes(const uint8_t* data, size_t length) noexcept {
    if (!data || length < 2) return;

    const uint8_t status = data[0] & 0xF0;

    // Program Change (0xC0): switch active Noise Track 0..3 (Spec 6.2)
    if (status == 0xC0) {
        const int prog = static_cast<int>(data[1] & 0x7F) % static_cast<int>(kNumNoiseTracks);
        engine_.enqueueCommand({CommandType::NoiseSelectTrack, prog, 0, 0.0f, 0.0f});
        return;
    }

    if (length < 3) return;
    const uint8_t d1 = data[1] & 0x7F;
    const uint8_t d2 = data[2] & 0x7F;

    // Note On (0x90 with vel > 0) / Note Off (0x80 or 0x90 with vel == 0)
    if (status == 0x90 && d2 > 0) {
        // Pads 1-7: Notes 36..42 (Akai MPD/MPK Bank A) or 0..6 -> Presets VOICE..DRIFT (Spec 6.2)
        int padIdx = -1;
        if (d1 >= 36 && d1 < 36 + static_cast<int>(kNumPresets)) {
            padIdx = d1 - 36;
        } else if (d1 < static_cast<int>(kNumPresets)) {
            padIdx = d1;
        }
        if (padIdx >= 0) {
            engine_.enqueueCommand({CommandType::ApplyPreset, padIdx, 0, 0.0f, 0.0f});
        }

        // Notes 44..59 (Bank B / Step pads): hold pad + turn knob for Parameter Lock (Spec 6.2)
        if (d1 >= 44 && d1 < 60) {
            heldPadStep_.store(d1 - 44, std::memory_order_relaxed);
            engine_.enqueueCommand({CommandType::NoiseSelectStep, d1 - 44, 0, 0.0f, 0.0f});
        }
        return;
    }

    if (status == 0x80 || (status == 0x90 && d2 == 0)) {
        if (d1 >= 44 && d1 < 60 && heldPadStep_.load(std::memory_order_relaxed) == (d1 - 44)) {
            heldPadStep_.store(-1, std::memory_order_relaxed);
        }
        return;
    }

    // Control Change (0xB0): 8 Akai Knobs (CC 1..8 or Akai MPK default CC 70..77)
    if (status == 0xB0) {
        int knob = -1;
        if (d1 >= 1 && d1 <= 8) knob = d1;
        else if (d1 >= 70 && d1 <= 77) knob = (d1 - 70) + 1;
        if (knob < 1 || knob > 8) return;

        const float norm = static_cast<float>(d2) / 127.0f;
        const FullState snap = engine_.readSnapshot();
        const int heldStep = heldPadStep_.load(std::memory_order_relaxed);

        // Combo: Hold step pad + turn knob -> record parameter lock onto step! (Spec 6.2)
        if (heldStep >= 0 && heldStep < static_cast<int>(kNumSeqSteps) && knob >= 3 && knob <= 6) {
            const int32_t packed = ((snap.noise.activeTrack & 0xFF) << 8) | (heldStep & 0xFF);
            if (knob == 3) {
                const float cutoff = 20.0f + norm * (12000.0f - 20.0f);
                engine_.enqueueCommand({CommandType::NoiseSetVoiceParam, packed,
                                        static_cast<int32_t>(NoiseLockField::Cutoff), cutoff, 0.0f});
            } else if (knob == 4) {
                engine_.enqueueCommand({CommandType::NoiseSetVoiceParam, packed,
                                        static_cast<int32_t>(NoiseLockField::Resonance), norm * 0.95f, 0.0f});
            } else if (knob == 5) {
                const float dec = 0.01f + norm * 1.99f;
                engine_.enqueueCommand({CommandType::NoiseSetVoiceParam, packed,
                                        static_cast<int32_t>(NoiseLockField::AmpDecay), dec, 0.0f});
            } else if (knob == 6) {
                engine_.enqueueCommand({CommandType::NoiseSetVoiceParam, packed,
                                        static_cast<int32_t>(NoiseLockField::ModEnvAmount), norm, 0.0f});
            }
            return;
        }

        switch (knob) {
            case 1: {
                // Knob 1: Select Voice effect along chain order
                const size_t ordIdx = std::min<size_t>(
                    kNumVoiceFx - 1, static_cast<size_t>(std::lround(norm * (kNumVoiceFx - 1))));
                const VoiceFxId fx = snap.voice.order[ordIdx];
                engine_.enqueueCommand({CommandType::SelectVoiceFx, static_cast<int32_t>(fx), 0, 0, 0});
                break;
            }
            case 2: {
                // Knob 2: Select Drone effect along chain order
                const size_t ordIdx = std::min<size_t>(
                    kNumDroneFx - 1, static_cast<size_t>(std::lround(norm * (kNumDroneFx - 1))));
                const DroneFxId fx = snap.drone.order[ordIdx];
                engine_.enqueueCommand({CommandType::SelectDroneFx, static_cast<int32_t>(fx), 0, 0, 0});
                break;
            }
            case 3: {
                // Knob 3: Voice active X (or Knob 0 in PARAM mode)
                const VoiceFxId fx = snap.voice.active;
                if (snap.knobMode == KnobMode::XY) {
                    const float curY = snap.voice.xy[static_cast<size_t>(fx)].y;
                    engine_.enqueueCommand({CommandType::SetVoiceXY, static_cast<int32_t>(fx), 0, norm, curY});
                } else {
                    const FxDef& def = getVoiceFxDef(fx);
                    if (def.numKnobs > 0) {
                        const float v = def.knobs[0].minVal + norm * (def.knobs[0].maxVal - def.knobs[0].minVal);
                        engine_.enqueueCommand({CommandType::SetVoiceKnob, static_cast<int32_t>(fx), 0, v, 0});
                    }
                }
                break;
            }
            case 4: {
                // Knob 4: Voice active Y (or Knob 1 in PARAM mode)
                const VoiceFxId fx = snap.voice.active;
                if (snap.knobMode == KnobMode::XY) {
                    const float curX = snap.voice.xy[static_cast<size_t>(fx)].x;
                    engine_.enqueueCommand({CommandType::SetVoiceXY, static_cast<int32_t>(fx), 0, curX, norm});
                } else {
                    const FxDef& def = getVoiceFxDef(fx);
                    if (def.numKnobs > 1) {
                        const float v = def.knobs[1].minVal + norm * (def.knobs[1].maxVal - def.knobs[1].minVal);
                        engine_.enqueueCommand({CommandType::SetVoiceKnob, static_cast<int32_t>(fx), 1, v, 0});
                    }
                }
                break;
            }
            case 5: {
                // Knob 5: Drone active X
                const DroneFxId fx = snap.drone.active;
                const float curY = snap.drone.xy[static_cast<size_t>(fx)].y;
                engine_.enqueueCommand({CommandType::SetDroneXY, static_cast<int32_t>(fx), 0, norm, curY});
                break;
            }
            case 6: {
                // Knob 6: Drone active Y
                const DroneFxId fx = snap.drone.active;
                const float curX = snap.drone.xy[static_cast<size_t>(fx)].x;
                engine_.enqueueCommand({CommandType::SetDroneXY, static_cast<int32_t>(fx), 0, curX, norm});
                break;
            }
            case 7: {
                // Knob 7: Select Master parameter (0..3)
                const int targetSel = std::min<int>(
                    static_cast<int>(kNumMasterParams - 1),
                    static_cast<int>(std::lround(norm * (kNumMasterParams - 1))));
                const int diff = targetSel - snap.master.selectedParam;
                if (diff != 0) {
                    engine_.enqueueCommand({CommandType::SelectMasterParam, diff, 0, 0, 0});
                }
                break;
            }
            case 8: {
                // Knob 8: Value of selected Master parameter
                const auto mp = static_cast<MasterParamId>(
                    std::clamp(snap.master.selectedParam, 0, static_cast<int>(kNumMasterParams - 1)));
                const KnobDef& def = getMasterParamDef(mp);
                const float val = def.minVal + norm * (def.maxVal - def.minVal);
                engine_.enqueueCommand({CommandType::SetMasterParam, static_cast<int32_t>(mp), 0, val, 0});
                break;
            }
        }
    }
}

} // namespace biomus
