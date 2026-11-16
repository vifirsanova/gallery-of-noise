#include "core/Types.hpp"
#include <cstdio>
#include <cstring>

namespace biomus {

namespace {

constexpr FxDef kVoiceDefs[kNumVoiceFx] = {
    {"PAN",      0, {{"", "", 0.0f, 1.0f, 0.0f}, {"", "", 0.0f, 1.0f, 0.0f}}},
    {"HPF",      1, {{"q", "Q", 0.1f, 5.0f, 0.7f}, {"", "", 0.0f, 1.0f, 0.0f}}},
    {"COMP",     2, {{"attack", "Attack", 0.001f, 0.1f, 0.003f}, {"release", "Release", 0.05f, 1.0f, 0.25f}}},
    {"FOLD",     0, {{"", "", 0.0f, 1.0f, 0.0f}, {"", "", 0.0f, 1.0f, 0.0f}}},
    {"BITCRUSH", 0, {{"", "", 0.0f, 1.0f, 0.0f}, {"", "", 0.0f, 1.0f, 0.0f}}},
    {"RINGMOD",  0, {{"", "", 0.0f, 1.0f, 0.0f}, {"", "", 0.0f, 1.0f, 0.0f}}},
    {"CHORUS",   2, {{"mix", "Mix", 0.0f, 1.0f, 0.5f}, {"fb", "Feedback", 0.0f, 0.6f, 0.0f}}},
    {"REVERB",   2, {{"size", "Size", 1.0f, 6.0f, 3.0f}, {"damping", "Damping", 500.0f, 12000.0f, 8000.0f}}},
    {"SHIMMER",  1, {{"time", "Time", 0.05f, 0.6f, 0.2f}, {"", "", 0.0f, 1.0f, 0.0f}}},
    {"DELAY",    2, {{"tone", "Tone", 200.0f, 12000.0f, 3500.0f}, {"mix", "Mix", 0.0f, 1.0f, 0.4f}}},
    {"LPF",      1, {{"q", "Q", 0.1f, 10.0f, 0.7f}, {"", "", 0.0f, 1.0f, 0.0f}}},
};

constexpr FxDef kDroneDefs[kNumDroneFx] = {
    {"LPF",      0, {{"", "", 0.0f, 1.0f, 0.0f}, {"", "", 0.0f, 1.0f, 0.0f}}},
    {"DRIVE",    0, {{"", "", 0.0f, 1.0f, 0.0f}, {"", "", 0.0f, 1.0f, 0.0f}}},
    {"FOLD",     0, {{"", "", 0.0f, 1.0f, 0.0f}, {"", "", 0.0f, 1.0f, 0.0f}}},
    {"BITCRUSH", 0, {{"", "", 0.0f, 1.0f, 0.0f}, {"", "", 0.0f, 1.0f, 0.0f}}},
    {"RINGMOD",  0, {{"", "", 0.0f, 1.0f, 0.0f}, {"", "", 0.0f, 1.0f, 0.0f}}},
    {"CHORUS",   1, {{"mix", "Mix", 0.0f, 1.0f, 0.5f}, {"", "", 0.0f, 1.0f, 0.0f}}},
    {"REVERB",   0, {{"", "", 0.0f, 1.0f, 0.0f}, {"", "", 0.0f, 1.0f, 0.0f}}},
};

constexpr KnobDef kMasterDefs[kNumMasterParams] = {
    {"voiceOut", "Voice out",  0.0f,   3.0f,     1.0f},
    {"droneVol", "Drone vol",  0.0f,   1.5f,     0.5f},
    {"fb",       "Feedback",   0.0f,   0.85f,    0.0f},
    {"lpf",      "Master LPF", 500.0f, 18000.0f, 18000.0f},
};

} // namespace

const FxDef& getVoiceFxDef(VoiceFxId id) noexcept {
    const auto idx = static_cast<size_t>(id);
    return kVoiceDefs[idx < kNumVoiceFx ? idx : 0];
}

const FxDef& getDroneFxDef(DroneFxId id) noexcept {
    const auto idx = static_cast<size_t>(id);
    return kDroneDefs[idx < kNumDroneFx ? idx : 0];
}

const KnobDef& getMasterParamDef(MasterParamId id) noexcept {
    const auto idx = static_cast<size_t>(id);
    return kMasterDefs[idx < kNumMasterParams ? idx : 0];
}

const char* toString(VoiceFxId id) noexcept { return getVoiceFxDef(id).name; }
const char* toString(DroneFxId id) noexcept { return getDroneFxDef(id).name; }

const char* toString(BioSource src) noexcept {
    switch (src) {
        case BioSource::Breath: return "breath";
        case BioSource::Heart:  return "heart";
        case BioSource::EyeX:   return "eyex";
        case BioSource::EyeY:   return "eyey";
        default:                return "none";
    }
}

const char* toString(PresetId preset) noexcept {
    switch (preset) {
        case PresetId::VOICE:   return "VOICE";
        case PresetId::WALL:    return "WALL";
        case PresetId::AMBIENT: return "AMBIENT";
        case PresetId::NOISE:   return "NOISE";
        case PresetId::CLEAN:   return "CLEAN";
        case PresetId::BREATH:  return "BREATH";
        case PresetId::DRIFT:   return "DRIFT";
        default:                return "VOICE";
    }
}

const char* toString(KnobMode mode) noexcept {
    return (mode == KnobMode::PARAM) ? "PARAM" : "XY";
}

const char* toString(MasterParamId param) noexcept {
    return getMasterParamDef(param).key;
}

const char* toString(NoiseType type) noexcept {
    switch (type) {
        case NoiseType::Pink:  return "pink";
        case NoiseType::Brown: return "brown";
        default:               return "white";
    }
}

const char* toString(LfoTarget tgt) noexcept {
    switch (tgt) {
        case LfoTarget::Pitch: return "pitch";
        case LfoTarget::Pan:   return "pan";
        default:               return "cutoff";
    }
}

bool parseVoiceFxId(std::string_view s, VoiceFxId& out) noexcept {
    for (size_t i = 0; i < kNumVoiceFx; ++i) {
        if (s == kVoiceDefs[i].name) {
            out = static_cast<VoiceFxId>(i);
            return true;
        }
    }
    return false;
}

bool parseDroneFxId(std::string_view s, DroneFxId& out) noexcept {
    for (size_t i = 0; i < kNumDroneFx; ++i) {
        if (s == kDroneDefs[i].name) {
            out = static_cast<DroneFxId>(i);
            return true;
        }
    }
    return false;
}

bool parseBioSource(std::string_view s, BioSource& out) noexcept {
    if (s.empty() || s == "none" || s == "null") { out = BioSource::None; return true; }
    if (s == "breath") { out = BioSource::Breath; return true; }
    if (s == "heart")  { out = BioSource::Heart;  return true; }
    if (s == "eyex" || s == "eyeX") { out = BioSource::EyeX; return true; }
    if (s == "eyey" || s == "eyeY") { out = BioSource::EyeY; return true; }
    return false;
}

bool parsePresetId(std::string_view s, PresetId& out) noexcept {
    for (size_t i = 0; i < kNumPresets; ++i) {
        const auto p = static_cast<PresetId>(i);
        if (s == toString(p)) {
            out = p;
            return true;
        }
    }
    return false;
}

bool parseMasterParamId(std::string_view s, MasterParamId& out) noexcept {
    for (size_t i = 0; i < kNumMasterParams; ++i) {
        if (s == kMasterDefs[i].key) {
            out = static_cast<MasterParamId>(i);
            return true;
        }
    }
    return false;
}

bool parseNoiseType(std::string_view s, NoiseType& out) noexcept {
    if (s == "white") { out = NoiseType::White; return true; }
    if (s == "pink")  { out = NoiseType::Pink;  return true; }
    if (s == "brown") { out = NoiseType::Brown; return true; }
    return false;
}

bool parseLfoTarget(std::string_view s, LfoTarget& out) noexcept {
    if (s == "cutoff") { out = LfoTarget::Cutoff; return true; }
    if (s == "pitch")  { out = LfoTarget::Pitch;  return true; }
    if (s == "pan")    { out = LfoTarget::Pan;    return true; }
    return false;
}

uint16_t parseModTarget(std::string_view targetStr) noexcept {
    // Expected formats:
    //   "master:<param>"
    //   "voice:<FX>:x" | "voice:<FX>:y" | "voice:<FX>:<knobKey>"
    //   "drone:<FX>:x" | "drone:<FX>:y" | "drone:<FX>:<knobKey>"
    const size_t c1 = targetStr.find(':');
    if (c1 == std::string_view::npos) return kInvalidModTarget;

    const std::string_view domain = targetStr.substr(0, c1);
    const std::string_view rest = targetStr.substr(c1 + 1);

    if (domain == "master") {
        MasterParamId mp{};
        if (parseMasterParamId(rest, mp)) {
            return masterModIndex(mp);
        }
        return kInvalidModTarget;
    }

    const size_t c2 = rest.find(':');
    if (c2 == std::string_view::npos) return kInvalidModTarget;
    const std::string_view fxStr = rest.substr(0, c2);
    const std::string_view paramStr = rest.substr(c2 + 1);

    if (domain == "voice") {
        VoiceFxId fx{};
        if (!parseVoiceFxId(fxStr, fx)) return kInvalidModTarget;
        if (paramStr == "x") return voiceXyModIndex(fx, 0);
        if (paramStr == "y") return voiceXyModIndex(fx, 1);
        const FxDef& def = getVoiceFxDef(fx);
        for (uint8_t k = 0; k < def.numKnobs; ++k) {
            if (paramStr == def.knobs[k].key) {
                return voiceKnobModIndex(fx, k);
            }
        }
        return kInvalidModTarget;
    }

    if (domain == "drone") {
        DroneFxId fx{};
        if (!parseDroneFxId(fxStr, fx)) return kInvalidModTarget;
        if (paramStr == "x") return droneXyModIndex(fx, 0);
        if (paramStr == "y") return droneXyModIndex(fx, 1);
        const FxDef& def = getDroneFxDef(fx);
        for (uint8_t k = 0; k < def.numKnobs; ++k) {
            if (paramStr == def.knobs[k].key) {
                return droneKnobModIndex(fx, k);
            }
        }
        return kInvalidModTarget;
    }

    return kInvalidModTarget;
}

size_t formatModTarget(uint16_t idx, char* buf, size_t bufCap) noexcept {
    if (!buf || bufCap == 0 || idx >= kNumModTargets) {
        if (buf && bufCap > 0) buf[0] = '\0';
        return 0;
    }

    int written = 0;
    if (idx < kVoiceKnobModBase) {
        const size_t rel = idx - kVoiceXyModBase;
        const auto fx = static_cast<VoiceFxId>(rel / 2);
        const char axis = (rel % 2 == 0) ? 'x' : 'y';
        written = std::snprintf(buf, bufCap, "voice:%s:%c", toString(fx), axis);
    } else if (idx < kDroneXyModBase) {
        const size_t rel = idx - kVoiceKnobModBase;
        const auto fx = static_cast<VoiceFxId>(rel / 2);
        const size_t k = rel % 2;
        const FxDef& def = getVoiceFxDef(fx);
        if (k >= def.numKnobs) {
            buf[0] = '\0';
            return 0;
        }
        written = std::snprintf(buf, bufCap, "voice:%s:%s", toString(fx), def.knobs[k].key);
    } else if (idx < kDroneKnobModBase) {
        const size_t rel = idx - kDroneXyModBase;
        const auto fx = static_cast<DroneFxId>(rel / 2);
        const char axis = (rel % 2 == 0) ? 'x' : 'y';
        written = std::snprintf(buf, bufCap, "drone:%s:%c", toString(fx), axis);
    } else if (idx < kMasterModBase) {
        const size_t rel = idx - kDroneKnobModBase;
        const auto fx = static_cast<DroneFxId>(rel / 2);
        const size_t k = rel % 2;
        const FxDef& def = getDroneFxDef(fx);
        if (k >= def.numKnobs) {
            buf[0] = '\0';
            return 0;
        }
        written = std::snprintf(buf, bufCap, "drone:%s:%s", toString(fx), def.knobs[k].key);
    } else {
        const size_t rel = idx - kMasterModBase;
        const auto mp = static_cast<MasterParamId>(rel);
        written = std::snprintf(buf, bufCap, "master:%s", toString(mp));
    }

    if (written < 0) {
        buf[0] = '\0';
        return 0;
    }
    return (static_cast<size_t>(written) < bufCap) ? static_cast<size_t>(written) : (bufCap - 1);
}

} // namespace biomus
