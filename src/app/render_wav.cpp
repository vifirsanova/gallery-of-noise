#include "core/Engine.hpp"
#include "core/WavFile.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    std::string presetName = "ALL";
    std::string outDir = "renders";
    double durationSec = 4.0;
    uint32_t sampleRate = 48000;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--preset" && i + 1 < argc) presetName = argv[++i];
        else if (arg == "--out-dir" && i + 1 < argc) outDir = argv[++i];
        else if (arg == "--seconds" && i + 1 < argc) durationSec = std::atof(argv[++i]);
    }

    std::filesystem::create_directories(outDir);

    auto renderOne = [&](biomus::PresetId pid, const std::string& filename) -> bool {
        biomus::Engine engine;
        engine.prepare(static_cast<double>(sampleRate), 128);

        auto& st = engine.mutableState();
        biomus::applyPreset(st, pid);
        st.fileOn = true; // synthetic vocal phrase into Voice chain
        st.droneOn = (pid != biomus::PresetId::CLEAN);
        st.noise.playing = (pid == biomus::PresetId::NOISE ||
                            pid == biomus::PresetId::DRIFT ||
                            pid == biomus::PresetId::WALL);
        engine.syncSmoothersToState();

        const size_t totalFrames = static_cast<size_t>(durationSec * static_cast<double>(sampleRate));
        std::vector<float> inZero(128, 0.0f);
        std::vector<float> stereoOut(totalFrames * 2, 0.0f);

        size_t pos = 0;
        while (pos < totalFrames) {
            const size_t block = std::min<size_t>(128, totalFrames - pos);
            engine.processStereo(inZero.data(), stereoOut.data() + pos * 2, static_cast<int>(block));
            pos += block;
        }

        const std::string fullPath = outDir + "/" + filename;
        if (!biomus::writeWav24(fullPath, stereoOut.data(), totalFrames, sampleRate, 2)) {
            std::cerr << "Failed to write " << fullPath << "\n";
            return false;
        }
        std::cout << "[WAV] Rendered " << fullPath << " (" << durationSec << "s, 48kHz 24-bit stereo)\n";
        return true;
    };

    if (presetName == "ALL") {
        for (size_t i = 0; i < biomus::kNumPresets; ++i) {
            const auto pid = static_cast<biomus::PresetId>(i);
            std::string name = biomus::toString(pid);
            for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (!renderOne(pid, "preset_" + name + ".wav")) return 1;
        }
        return 0;
    }

    biomus::PresetId pid{};
    if (!biomus::parsePresetId(presetName, pid)) {
        std::cerr << "Unknown preset: " << presetName << "\n";
        return 1;
    }
    std::string name = biomus::toString(pid);
    for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return renderOne(pid, "preset_" + name + ".wav") ? 0 : 1;
}
