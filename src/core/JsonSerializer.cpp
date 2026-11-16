#include "core/JsonSerializer.hpp"
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace biomus {

namespace {

void appendFloat(std::string& out, float v) {
    if (!std::isfinite(v)) v = 0.0f;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4f", static_cast<double>(v));
    out += buf;
}

// Lightweight flat JSON key finder (sufficient for all command messages)
bool extractJsonString(std::string_view json, std::string_view key, std::string& outVal) {
    std::string needle = "\"";
    needle += key;
    needle += "\"";
    size_t pos = json.find(needle);
    if (pos == std::string_view::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string_view::npos) return false;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size()) return false;
    if (json.substr(pos, 4) == "null") {
        outVal.clear();
        return true;
    }
    if (json[pos] != '"') return false;
    ++pos;
    outVal.clear();
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) ++pos;
        outVal.push_back(json[pos]);
        ++pos;
    }
    return true;
}

bool extractJsonNumber(std::string_view json, std::string_view key, double& outNum) {
    std::string needle = "\"";
    needle += key;
    needle += "\"";
    size_t pos = json.find(needle);
    if (pos == std::string_view::npos) return false;
    pos = json.find(':', pos + needle.size());
    if (pos == std::string_view::npos) return false;
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos]))) ++pos;
    if (pos >= json.size()) return false;
    if (json.substr(pos, 4) == "true") { outNum = 1.0; return true; }
    if (json.substr(pos, 5) == "false") { outNum = 0.0; return true; }

    char buf[64];
    size_t n = 0;
    while (pos < json.size() && n + 1 < sizeof(buf) &&
           (std::isdigit(static_cast<unsigned char>(json[pos])) ||
            json[pos] == '-' || json[pos] == '+' || json[pos] == '.' ||
            json[pos] == 'e' || json[pos] == 'E')) {
        buf[n++] = json[pos++];
    }
    if (n == 0) return false;
    buf[n] = '\0';
    char* endp = nullptr;
    outNum = std::strtod(buf, &endp);
    return endp != buf;
}

} // namespace

std::string serializeStateJson(const FullState& s) {
    std::string out;
    out.reserve(8192);

    out += "{\"type\":\"state\",\"preset\":\"";
    out += toString(s.preset);
    out += "\",\"mic\":";
    out += (s.mic ? "true" : "false");
    out += ",\"droneOn\":";
    out += (s.droneOn ? "true" : "false");
    out += ",\"fileOn\":";
    out += (s.fileOn ? "true" : "false");
    out += ",\"bypass\":";
    out += (s.bypass ? "true" : "false");
    out += ",\"knobMode\":\"";
    out += toString(s.knobMode);
    out += "\",\"activeBio\":\"";
    out += toString(s.activeBio);
    out += "\",\"lastModTarget\":";
    if (s.lastModTarget < kNumModTargets) {
        char tbuf[64];
        formatModTarget(s.lastModTarget, tbuf, sizeof(tbuf));
        out += "\"";
        out += tbuf;
        out += "\"";
    } else {
        out += "null";
    }
    out += ",\"masterSel\":";
    out += std::to_string(s.master.selectedParam);
    out += ",\"masterLevel\":";
    appendFloat(out, s.masterLevel);
    out += ",\"clip\":";
    out += (s.clip ? "true" : "false");

    // Bio
    out += ",\"bio\":{\"breath\":";
    appendFloat(out, s.bio.breath);
    out += ",\"heart\":";
    appendFloat(out, s.bio.heart);
    out += ",\"eyex\":";
    appendFloat(out, s.bio.eyeX);
    out += ",\"eyey\":";
    appendFloat(out, s.bio.eyeY);
    out += ",\"bpm\":";
    out += std::to_string(s.bio.bpm);
    out += "}";

    // Master
    out += ",\"master\":{\"voiceOut\":";
    appendFloat(out, s.master.voiceOut);
    out += ",\"droneVol\":";
    appendFloat(out, s.master.droneVol);
    out += ",\"fb\":";
    appendFloat(out, s.master.fb);
    out += ",\"lpf\":";
    appendFloat(out, s.master.lpf);
    out += "}";

    // Voice chain
    out += ",\"voice\":{\"order\":[";
    for (size_t i = 0; i < kNumVoiceFx; ++i) {
        if (i) out += ",";
        out += "\"";
        out += toString(s.voice.order[i]);
        out += "\"";
    }
    out += "],\"enabled\":{";
    for (size_t i = 0; i < kNumVoiceFx; ++i) {
        if (i) out += ",";
        out += "\"";
        out += toString(static_cast<VoiceFxId>(i));
        out += "\":";
        out += (s.voice.enabled[i] ? "true" : "false");
    }
    out += "},\"active\":\"";
    out += toString(s.voice.active);
    out += "\",\"xy\":{";
    for (size_t i = 0; i < kNumVoiceFx; ++i) {
        if (i) out += ",";
        out += "\"";
        out += toString(static_cast<VoiceFxId>(i));
        out += "\":{\"x\":";
        appendFloat(out, s.voice.xy[i].x);
        out += ",\"y\":";
        appendFloat(out, s.voice.xy[i].y);
        out += "}";
    }
    out += "},\"params\":{";
    bool firstFxParam = true;
    for (size_t i = 0; i < kNumVoiceFx; ++i) {
        const auto fx = static_cast<VoiceFxId>(i);
        const FxDef& def = getVoiceFxDef(fx);
        if (def.numKnobs == 0) continue;
        if (!firstFxParam) out += ",";
        firstFxParam = false;
        out += "\"";
        out += def.name;
        out += "\":{";
        for (uint8_t k = 0; k < def.numKnobs; ++k) {
            if (k) out += ",";
            out += "\"";
            out += def.knobs[k].key;
            out += "\":";
            appendFloat(out, s.voice.knobs[i][k]);
        }
        out += "}";
    }
    out += "}}";

    // Drone chain
    out += ",\"drone\":{\"order\":[";
    for (size_t i = 0; i < kNumDroneFx; ++i) {
        if (i) out += ",";
        out += "\"";
        out += toString(s.drone.order[i]);
        out += "\"";
    }
    out += "],\"enabled\":{";
    for (size_t i = 0; i < kNumDroneFx; ++i) {
        if (i) out += ",";
        out += "\"";
        out += toString(static_cast<DroneFxId>(i));
        out += "\":";
        out += (s.drone.enabled[i] ? "true" : "false");
    }
    out += "},\"active\":\"";
    out += toString(s.drone.active);
    out += "\",\"xy\":{";
    for (size_t i = 0; i < kNumDroneFx; ++i) {
        if (i) out += ",";
        out += "\"";
        out += toString(static_cast<DroneFxId>(i));
        out += "\":{\"x\":";
        appendFloat(out, s.drone.xy[i].x);
        out += ",\"y\":";
        appendFloat(out, s.drone.xy[i].y);
        out += "}";
    }
    out += "},\"params\":{";
    bool firstDroneParam = true;
    for (size_t i = 0; i < kNumDroneFx; ++i) {
        const auto fx = static_cast<DroneFxId>(i);
        const FxDef& def = getDroneFxDef(fx);
        if (def.numKnobs == 0) continue;
        if (!firstDroneParam) out += ",";
        firstDroneParam = false;
        out += "\"";
        out += def.name;
        out += "\":{";
        for (uint8_t k = 0; k < def.numKnobs; ++k) {
            if (k) out += ",";
            out += "\"";
            out += def.knobs[k].key;
            out += "\":";
            appendFloat(out, s.drone.knobs[i][k]);
        }
        out += "}";
    }
    out += "}}";

    // Modulation matrix (sparse JSON matching JS state.modulation)
    out += ",\"modulation\":{";
    bool firstMod = true;
    for (uint16_t m = 0; m < kNumModTargets; ++m) {
        const ModEntry me = s.modulation.entries[m];
        if (me.source == BioSource::None) continue;
        char tbuf[64];
        if (formatModTarget(m, tbuf, sizeof(tbuf)) == 0) continue;
        if (!firstMod) out += ",";
        firstMod = false;
        out += "\"";
        out += tbuf;
        out += "\":{\"source\":\"";
        out += toString(me.source);
        out += "\",\"depth\":";
        appendFloat(out, me.depth);
        out += "}";
    }
    out += "}";

    // Noise synth & 4-track 16-step sequencer
    out += ",\"noise\":{\"playing\":";
    out += (s.noise.playing ? "true" : "false");
    out += ",\"tempo\":";
    appendFloat(out, s.noise.tempo);
    out += ",\"activeTrack\":";
    out += std::to_string(s.noise.activeTrack);
    out += ",\"selectedStep\":";
    out += std::to_string(s.noise.selectedStep);
    out += ",\"tracks\":[";
    for (size_t t = 0; t < kNumNoiseTracks; ++t) {
        if (t) out += ",";
        const NoiseTrack& tr = s.noise.tracks[t];
        const NoiseVoiceParams& v = tr.voice;
        out += "{\"currentStep\":";
        out += std::to_string(tr.currentStep);
        out += ",\"voice\":{\"type\":\"";
        out += toString(v.type);
        out += "\",\"cutoff\":";
        appendFloat(out, v.cutoff);
        out += ",\"resonance\":";
        appendFloat(out, v.resonance);
        out += ",\"ampAttack\":";
        appendFloat(out, v.ampAttack);
        out += ",\"ampDecay\":";
        appendFloat(out, v.ampDecay);
        out += ",\"modEnvAmount\":";
        appendFloat(out, v.modEnvAmount);
        out += ",\"lfo1Rate\":";
        appendFloat(out, v.lfo1.rate);
        out += ",\"lfo1Depth\":";
        appendFloat(out, v.lfo1.depth);
        out += ",\"lfo1Target\":\"";
        out += toString(v.lfo1.target);
        out += "\",\"lfo2Rate\":";
        appendFloat(out, v.lfo2.rate);
        out += ",\"lfo2Depth\":";
        appendFloat(out, v.lfo2.depth);
        out += ",\"lfo2Target\":\"";
        out += toString(v.lfo2.target);
        out += "\",\"pan\":";
        appendFloat(out, v.pan);
        out += ",\"gain\":";
        appendFloat(out, v.gain);
        out += "},\"steps\":[";
        for (size_t st = 0; st < kNumSeqSteps; ++st) {
            if (st) out += ",";
            const Step& step = tr.steps[st];
            out += "{\"trigger\":";
            out += (step.trigger ? "true" : "false");
            out += ",\"locks\":{";
            bool firstLock = true;
            const StepLocks& lk = step.locks;
            if (lk.mask != 0) {
                auto emitFloatLock = [&](NoiseLockField f, float val) {
                    if (!lk.has(f)) return;
                    if (!firstLock) out += ",";
                    firstLock = false;
                    out += "\"";
                    out += toString(f);
                    out += "\":";
                    appendFloat(out, val);
                };
                auto emitStrLock = [&](NoiseLockField f, const char* sval) {
                    if (!lk.has(f)) return;
                    if (!firstLock) out += ",";
                    firstLock = false;
                    out += "\"";
                    out += toString(f);
                    out += "\":\"";
                    out += sval;
                    out += "\"";
                };
                emitStrLock(NoiseLockField::Type, toString(lk.values.type));
                emitFloatLock(NoiseLockField::Cutoff, lk.values.cutoff);
                emitFloatLock(NoiseLockField::Resonance, lk.values.resonance);
                emitFloatLock(NoiseLockField::AmpAttack, lk.values.ampAttack);
                emitFloatLock(NoiseLockField::AmpDecay, lk.values.ampDecay);
                emitFloatLock(NoiseLockField::ModEnvAmount, lk.values.modEnvAmount);
                emitFloatLock(NoiseLockField::Lfo1Rate, lk.values.lfo1.rate);
                emitFloatLock(NoiseLockField::Lfo1Depth, lk.values.lfo1.depth);
                emitStrLock(NoiseLockField::Lfo1Target, toString(lk.values.lfo1.target));
                emitFloatLock(NoiseLockField::Lfo2Rate, lk.values.lfo2.rate);
                emitFloatLock(NoiseLockField::Lfo2Depth, lk.values.lfo2.depth);
                emitStrLock(NoiseLockField::Lfo2Target, toString(lk.values.lfo2.target));
                emitFloatLock(NoiseLockField::Pan, lk.values.pan);
                emitFloatLock(NoiseLockField::Gain, lk.values.gain);
            }
            out += "}}";
        }
        out += "]}";
    }
    out += "]}}";
    return out;
}

bool parseJsonCommand(std::string_view json, std::vector<Command>& outCommands) {
    outCommands.clear();
    std::string cmdName;
    if (!extractJsonString(json, "cmd", cmdName)) {
        return false;
    }

    if (cmdName == "preset") {
        std::string name;
        PresetId pid{};
        if (extractJsonString(json, "name", name) && parsePresetId(name, pid)) {
            outCommands.push_back({CommandType::ApplyPreset, static_cast<int32_t>(pid), 0, 0.0f, 0.0f});
            return true;
        }
        return false;
    }

    if (cmdName == "cycleEffect") {
        std::string side;
        double dir = 1.0;
        extractJsonString(json, "side", side);
        extractJsonNumber(json, "dir", dir);
        const int d = (dir < 0.0) ? -1 : 1;
        outCommands.push_back({side == "drone" ? CommandType::CycleDroneFx : CommandType::CycleVoiceFx, d, 0, 0, 0});
        return true;
    }

    if (cmdName == "selectEffect") {
        std::string side, fxStr;
        extractJsonString(json, "side", side);
        extractJsonString(json, "fx", fxStr);
        if (side == "drone") {
            DroneFxId fx{};
            if (parseDroneFxId(fxStr, fx)) {
                outCommands.push_back({CommandType::SelectDroneFx, static_cast<int32_t>(fx), 0, 0, 0});
                return true;
            }
        } else {
            VoiceFxId fx{};
            if (parseVoiceFxId(fxStr, fx)) {
                outCommands.push_back({CommandType::SelectVoiceFx, static_cast<int32_t>(fx), 0, 0, 0});
                return true;
            }
        }
        return false;
    }

    if (cmdName == "toggleEffect") {
        std::string side;
        extractJsonString(json, "side", side);
        outCommands.push_back({side == "drone" ? CommandType::ToggleDroneFx : CommandType::ToggleVoiceFx, 0, 0, 0, 0});
        return true;
    }

    if (cmdName == "moveEffect") {
        std::string side;
        double dir = 1.0;
        extractJsonString(json, "side", side);
        extractJsonNumber(json, "dir", dir);
        const int d = (dir < 0.0) ? -1 : 1;
        outCommands.push_back({side == "drone" ? CommandType::MoveDroneFx : CommandType::MoveVoiceFx, d, 0, 0, 0});
        return true;
    }

    if (cmdName == "adjustXY") {
        std::string side, axis;
        double delta = 0.0;
        extractJsonString(json, "side", side);
        extractJsonString(json, "axis", axis);
        extractJsonNumber(json, "delta", delta);
        const float dx = (axis == "x") ? static_cast<float>(delta) : 0.0f;
        const float dy = (axis == "y") ? static_cast<float>(delta) : 0.0f;
        outCommands.push_back({side == "drone" ? CommandType::AdjustDroneXY : CommandType::AdjustVoiceXY,
                               0, 0, dx, dy});
        return true;
    }

    if (cmdName == "setXY") {
        std::string side, fxStr;
        double x = 0.5, y = 0.5;
        extractJsonString(json, "side", side);
        extractJsonString(json, "fx", fxStr);
        if (!extractJsonNumber(json, "x", x) || !extractJsonNumber(json, "y", y)) return false;
        if (side == "drone") {
            DroneFxId fx{};
            if (!parseDroneFxId(fxStr, fx)) return false;
            outCommands.push_back({CommandType::SetDroneXY, static_cast<int32_t>(fx), 0,
                                   static_cast<float>(x), static_cast<float>(y)});
            return true;
        } else {
            VoiceFxId fx{};
            if (!parseVoiceFxId(fxStr, fx)) return false;
            outCommands.push_back({CommandType::SetVoiceXY, static_cast<int32_t>(fx), 0,
                                   static_cast<float>(x), static_cast<float>(y)});
            return true;
        }
    }

    if (cmdName == "adjustParam") {
        std::string side;
        double index = 0.0, delta = 0.0;
        extractJsonString(json, "side", side);
        extractJsonNumber(json, "index", index);
        extractJsonNumber(json, "delta", delta);
        outCommands.push_back({side == "drone" ? CommandType::AdjustDroneKnob : CommandType::AdjustVoiceKnob,
                               static_cast<int32_t>(index), 0, static_cast<float>(delta), 0.0f});
        return true;
    }

    if (cmdName == "setParam") {
        std::string side, fxStr;
        double index = 0.0, value = 0.0;
        extractJsonString(json, "side", side);
        extractJsonString(json, "fx", fxStr);
        extractJsonNumber(json, "index", index);
        if (!extractJsonNumber(json, "value", value)) return false;
        if (side == "drone") {
            DroneFxId fx{};
            if (!parseDroneFxId(fxStr, fx)) return false;
            outCommands.push_back({CommandType::SetDroneKnob, static_cast<int32_t>(fx),
                                   static_cast<int32_t>(index), static_cast<float>(value), 0.0f});
            return true;
        } else {
            VoiceFxId fx{};
            if (!parseVoiceFxId(fxStr, fx)) return false;
            outCommands.push_back({CommandType::SetVoiceKnob, static_cast<int32_t>(fx),
                                   static_cast<int32_t>(index), static_cast<float>(value), 0.0f});
            return true;
        }
    }

    if (cmdName == "selectMaster") {
        double dir = 1.0;
        if (extractJsonNumber(json, "dir", dir)) {
            outCommands.push_back({CommandType::SelectMasterParam, (dir < 0.0) ? -1 : 1, 0, 0, 0});
            return true;
        }
        return false;
    }

    if (cmdName == "adjustMaster") {
        double delta = 0.0;
        if (extractJsonNumber(json, "delta", delta)) {
            outCommands.push_back({CommandType::AdjustMasterParam, 0, 0, static_cast<float>(delta), 0});
            return true;
        }
        return false;
    }

    if (cmdName == "setMaster") {
        std::string key;
        double val = 0.0;
        MasterParamId mp{};
        if (extractJsonString(json, "key", key) && extractJsonNumber(json, "value", val) &&
            parseMasterParamId(key, mp)) {
            outCommands.push_back({CommandType::SetMasterParam, static_cast<int32_t>(mp), 0,
                                   static_cast<float>(val), 0.0f});
            return true;
        }
        return false;
    }

    if (cmdName == "cycleMod") {
        std::string target;
        if (extractJsonString(json, "target", target)) {
            const uint16_t tid = parseModTarget(target);
            if (tid != kInvalidModTarget) {
                outCommands.push_back({CommandType::CycleModSource, static_cast<int32_t>(tid), 0, 0, 0});
                return true;
            }
        }
        return false;
    }

    if (cmdName == "setMod") {
        std::string target, sourceStr;
        double depth = 0.5;
        if (!extractJsonString(json, "target", target)) return false;
        extractJsonString(json, "source", sourceStr);
        extractJsonNumber(json, "depth", depth);
        const uint16_t tid = parseModTarget(target);
        BioSource src = BioSource::None;
        parseBioSource(sourceStr, src);
        if (tid != kInvalidModTarget) {
            outCommands.push_back({CommandType::SetModulation, static_cast<int32_t>(tid),
                                   static_cast<int32_t>(src), static_cast<float>(depth), 0});
            return true;
        }
        return false;
    }

    if (cmdName == "adjustModDepth") {
        double delta = 0.0;
        if (extractJsonNumber(json, "delta", delta)) {
            outCommands.push_back({CommandType::AdjustModDepth, 0, 0, static_cast<float>(delta), 0});
            return true;
        }
        return false;
    }

    if (cmdName == "setActiveBio") {
        std::string srcStr;
        BioSource src{};
        if (extractJsonString(json, "source", srcStr) && parseBioSource(srcStr, src) && src != BioSource::None) {
            outCommands.push_back({CommandType::SetActiveBio, static_cast<int32_t>(src), 0, 0, 0});
            return true;
        }
        return false;
    }

    if (cmdName == "toggleKnobMode") {
        outCommands.push_back({CommandType::ToggleKnobMode, 0, 0, 0, 0});
        return true;
    }

    if (cmdName == "toggleFlag") {
        std::string key;
        extractJsonString(json, "key", key);
        if (key == "mic")     { outCommands.push_back({CommandType::ToggleMic, 0, 0, 0, 0}); return true; }
        if (key == "droneOn") { outCommands.push_back({CommandType::ToggleDrone, 0, 0, 0, 0}); return true; }
        if (key == "bypass")  { outCommands.push_back({CommandType::ToggleBypass, 0, 0, 0, 0}); return true; }
        if (key == "fileOn")  {
            double v = 0.0;
            if (extractJsonNumber(json, "value", v)) {
                outCommands.push_back({CommandType::SetFileOn, (v != 0.0) ? 1 : 0, 0, 0, 0});
            } else {
                outCommands.push_back({CommandType::SetFileOn, 1, 0, 0, 0});
            }
            return true;
        }
        return false;
    }

    if (cmdName == "noiseTogglePlay") {
        outCommands.push_back({CommandType::NoiseTogglePlay, 0, 0, 0, 0});
        return true;
    }

    if (cmdName == "noiseSetPlaying") {
        double v = 0.0;
        if (extractJsonNumber(json, "playing", v)) {
            outCommands.push_back({CommandType::NoiseSetPlaying, (v != 0.0) ? 1 : 0, 0, 0, 0});
            return true;
        }
        return false;
    }

    if (cmdName == "noiseTempo") {
        double bpm = 120.0;
        if (extractJsonNumber(json, "tempo", bpm)) {
            outCommands.push_back({CommandType::NoiseSetTempo, 0, 0, static_cast<float>(bpm), 0});
            return true;
        }
        return false;
    }

    if (cmdName == "noiseSelectTrack") {
        double tr = 0.0, rel = 0.0;
        extractJsonNumber(json, "track", tr);
        extractJsonNumber(json, "relative", rel);
        outCommands.push_back({CommandType::NoiseSelectTrack, static_cast<int32_t>(tr),
                               (rel != 0.0) ? 1 : 0, 0, 0});
        return true;
    }

    if (cmdName == "noiseSelectStep") {
        double st = 0.0, rel = 0.0;
        extractJsonNumber(json, "step", st);
        extractJsonNumber(json, "relative", rel);
        outCommands.push_back({CommandType::NoiseSelectStep, static_cast<int32_t>(st),
                               (rel != 0.0) ? 1 : 0, 0, 0});
        return true;
    }

    if (cmdName == "noiseToggleStep") {
        double tr = -1.0, st = -1.0;
        extractJsonNumber(json, "track", tr);
        extractJsonNumber(json, "step", st);
        outCommands.push_back({CommandType::NoiseToggleStep, static_cast<int32_t>(tr),
                               static_cast<int32_t>(st), 0, 0});
        return true;
    }

    if (cmdName == "noiseSetVoiceParam") {
        std::string paramName;
        if (!extractJsonString(json, "param", paramName)) return false;
        NoiseLockField field{};
        if (!parseNoiseLockField(paramName, field)) return false;

        double tr = 255.0, st = 255.0;
        extractJsonNumber(json, "track", tr);
        extractJsonNumber(json, "step", st);
        const int32_t packed = ((static_cast<int32_t>(tr) & 0xFF) << 8) | (static_cast<int32_t>(st) & 0xFF);

        float fval = 0.0f;
        if (field == NoiseLockField::Type) {
            std::string tstr;
            NoiseType nt{};
            if (extractJsonString(json, "value", tstr) && parseNoiseType(tstr, nt)) {
                fval = static_cast<float>(nt);
            } else {
                double n = 0.0;
                if (!extractJsonNumber(json, "value", n)) return false;
                fval = static_cast<float>(n);
            }
        } else if (field == NoiseLockField::Lfo1Target || field == NoiseLockField::Lfo2Target) {
            std::string lstr;
            LfoTarget lt{};
            if (extractJsonString(json, "value", lstr) && parseLfoTarget(lstr, lt)) {
                fval = static_cast<float>(lt);
            } else {
                double n = 0.0;
                if (!extractJsonNumber(json, "value", n)) return false;
                fval = static_cast<float>(n);
            }
        } else {
            double n = 0.0;
            if (!extractJsonNumber(json, "value", n)) return false;
            fval = static_cast<float>(n);
        }

        outCommands.push_back({CommandType::NoiseSetVoiceParam, packed,
                               static_cast<int32_t>(field), fval, 0.0f});
        return true;
    }

    if (cmdName == "noiseClearLocks") {
        double tr = -1.0, st = -1.0;
        extractJsonNumber(json, "track", tr);
        extractJsonNumber(json, "step", st);
        outCommands.push_back({CommandType::NoiseClearStepLocks, static_cast<int32_t>(tr),
                               static_cast<int32_t>(st), 0, 0});
        return true;
    }

    return false;
}

} // namespace biomus
