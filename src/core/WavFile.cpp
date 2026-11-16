#include "core/WavFile.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace biomus {

namespace {

inline void writeU16LE(std::ofstream& f, uint16_t v) {
    const uint8_t b[2] = {static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF)};
    f.write(reinterpret_cast<const char*>(b), 2);
}

inline void writeU32LE(std::ofstream& f, uint32_t v) {
    const uint8_t b[4] = {
        static_cast<uint8_t>(v & 0xFF),
        static_cast<uint8_t>((v >> 8) & 0xFF),
        static_cast<uint8_t>((v >> 16) & 0xFF),
        static_cast<uint8_t>((v >> 24) & 0xFF)
    };
    f.write(reinterpret_cast<const char*>(b), 4);
}

inline uint16_t readU16LE(const uint8_t* p) {
    return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline uint32_t readU32LE(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) |
           (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

} // namespace

bool writeWav24(const std::string& filePath,
                const float* interleavedSamples,
                size_t numFrames,
                uint32_t sampleRate,
                uint16_t channels) {
    if (!interleavedSamples && numFrames > 0) return false;
    if (channels == 0) return false;

    std::ofstream f(filePath, std::ios::binary);
    if (!f.is_open()) return false;

    const uint16_t bitsPerSample = 24;
    const uint16_t blockAlign = channels * (bitsPerSample / 8);
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t dataSize = static_cast<uint32_t>(numFrames * blockAlign);
    const uint32_t riffSize = 36 + dataSize;

    f.write("RIFF", 4);
    writeU32LE(f, riffSize);
    f.write("WAVE", 4);

    f.write("fmt ", 4);
    writeU32LE(f, 16);
    writeU16LE(f, 1); // PCM
    writeU16LE(f, channels);
    writeU32LE(f, sampleRate);
    writeU32LE(f, byteRate);
    writeU16LE(f, blockAlign);
    writeU16LE(f, bitsPerSample);

    f.write("data", 4);
    writeU32LE(f, dataSize);

    const size_t totalSamples = numFrames * channels;
    std::vector<uint8_t> chunkBuf;
    chunkBuf.reserve(std::min<size_t>(totalSamples, 4096) * 3);

    for (size_t i = 0; i < totalSamples; ++i) {
        const float clamped = std::clamp(interleavedSamples[i], -1.0f, 1.0f);
        const int32_t pcm = static_cast<int32_t>(std::lround(clamped * 8388607.0f));
        chunkBuf.push_back(static_cast<uint8_t>(pcm & 0xFF));
        chunkBuf.push_back(static_cast<uint8_t>((pcm >> 8) & 0xFF));
        chunkBuf.push_back(static_cast<uint8_t>((pcm >> 16) & 0xFF));
        if (chunkBuf.size() >= 12288) {
            f.write(reinterpret_cast<const char*>(chunkBuf.data()), static_cast<std::streamsize>(chunkBuf.size()));
            chunkBuf.clear();
        }
    }
    if (!chunkBuf.empty()) {
        f.write(reinterpret_cast<const char*>(chunkBuf.data()), static_cast<std::streamsize>(chunkBuf.size()));
    }
    return f.good();
}

bool writeWavFloat32(const std::string& filePath,
                     const float* interleavedSamples,
                     size_t numFrames,
                     uint32_t sampleRate,
                     uint16_t channels) {
    if (!interleavedSamples && numFrames > 0) return false;
    if (channels == 0) return false;

    std::ofstream f(filePath, std::ios::binary);
    if (!f.is_open()) return false;

    const uint16_t bitsPerSample = 32;
    const uint16_t blockAlign = channels * 4;
    const uint32_t byteRate = sampleRate * blockAlign;
    const uint32_t dataSize = static_cast<uint32_t>(numFrames * blockAlign);
    const uint32_t riffSize = 36 + dataSize;

    f.write("RIFF", 4);
    writeU32LE(f, riffSize);
    f.write("WAVE", 4);

    f.write("fmt ", 4);
    writeU32LE(f, 16);
    writeU16LE(f, 3); // IEEE Float
    writeU16LE(f, channels);
    writeU32LE(f, sampleRate);
    writeU32LE(f, byteRate);
    writeU16LE(f, blockAlign);
    writeU16LE(f, bitsPerSample);

    f.write("data", 4);
    writeU32LE(f, dataSize);

    const size_t totalSamples = numFrames * channels;
    if (totalSamples > 0) {
        f.write(reinterpret_cast<const char*>(interleavedSamples),
                static_cast<std::streamsize>(totalSamples * sizeof(float)));
    }
    return f.good();
}

bool readWav(const std::string& filePath, WavData& out) {
    std::ifstream f(filePath, std::ios::binary | std::ios::ate);
    if (!f.is_open()) return false;

    const auto fileSize = f.tellg();
    if (fileSize < 44) return false;
    f.seekg(0, std::ios::beg);

    std::vector<uint8_t> raw(static_cast<size_t>(fileSize));
    if (!f.read(reinterpret_cast<char*>(raw.data()), fileSize)) return false;

    if (std::memcmp(raw.data(), "RIFF", 4) != 0 ||
        std::memcmp(raw.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    uint16_t audioFormat = 0;
    uint16_t channels = 0;
    uint32_t sampleRate = 0;
    uint16_t bitsPerSample = 0;
    const uint8_t* dataPtr = nullptr;
    uint32_t dataBytes = 0;

    size_t pos = 12;
    while (pos + 8 <= raw.size()) {
        const uint32_t chunkSize = readU32LE(raw.data() + pos + 4);
        const size_t bodyPos = pos + 8;
        if (bodyPos + chunkSize > raw.size()) break;

        if (std::memcmp(raw.data() + pos, "fmt ", 4) == 0 && chunkSize >= 16) {
            audioFormat   = readU16LE(raw.data() + bodyPos);
            channels      = readU16LE(raw.data() + bodyPos + 2);
            sampleRate    = readU32LE(raw.data() + bodyPos + 4);
            bitsPerSample = readU16LE(raw.data() + bodyPos + 14);
        } else if (std::memcmp(raw.data() + pos, "data", 4) == 0) {
            dataPtr = raw.data() + bodyPos;
            dataBytes = chunkSize;
            break;
        }
        pos = bodyPos + chunkSize + (chunkSize & 1u);
    }

    if (!dataPtr || channels == 0 || sampleRate == 0) return false;
    out.sampleRate = sampleRate;
    out.channels = channels;
    out.samples.clear();

    if (audioFormat == 1 && bitsPerSample == 16) {
        const size_t count = dataBytes / 2;
        out.samples.resize(count);
        for (size_t i = 0; i < count; ++i) {
            const int16_t s = static_cast<int16_t>(readU16LE(dataPtr + i * 2));
            out.samples[i] = static_cast<float>(s) * (1.0f / 32768.0f);
        }
        return true;
    }
    if (audioFormat == 1 && bitsPerSample == 24) {
        const size_t count = dataBytes / 3;
        out.samples.resize(count);
        for (size_t i = 0; i < count; ++i) {
            int32_t v = static_cast<int32_t>(dataPtr[i * 3]) |
                        (static_cast<int32_t>(dataPtr[i * 3 + 1]) << 8) |
                        (static_cast<int32_t>(dataPtr[i * 3 + 2]) << 16);
            if (v & 0x800000) v |= ~0xFFFFFF; // sign extend 24 -> 32
            out.samples[i] = static_cast<float>(v) * (1.0f / 8388607.0f);
        }
        return true;
    }
    if (audioFormat == 3 && bitsPerSample == 32) {
        const size_t count = dataBytes / 4;
        out.samples.resize(count);
        std::memcpy(out.samples.data(), dataPtr, count * 4);
        return true;
    }
    return false;
}

} // namespace biomus
