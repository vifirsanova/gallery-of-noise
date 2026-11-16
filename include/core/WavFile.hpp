#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace biomus {

struct WavData {
    uint32_t sampleRate = 48000;
    uint16_t channels = 1;
    std::vector<float> samples; // interleaved [-1..+1]
};

// Writes 24-bit PCM WAV (matching Spec 2: 48 kHz, 24-bit) or 16-bit PCM WAV
bool writeWav24(const std::string& filePath,
                const float* interleavedSamples,
                size_t numFrames,
                uint32_t sampleRate = 48000,
                uint16_t channels = 1);

bool writeWavFloat32(const std::string& filePath,
                     const float* interleavedSamples,
                     size_t numFrames,
                     uint32_t sampleRate = 48000,
                     uint16_t channels = 1);

// Reads 16-bit, 24-bit, or 32-bit float PCM WAV into float [-1..+1]
bool readWav(const std::string& filePath, WavData& out);

} // namespace biomus
