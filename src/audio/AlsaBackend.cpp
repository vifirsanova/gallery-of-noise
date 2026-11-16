#include "audio/AudioBackend.hpp"

#ifdef BIOMUSIC_HAVE_ALSA
#include <alsa/asoundlib.h>
#include <pthread.h>
#include <sched.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace biomus {

namespace {

struct StereoFrame16 {
    int16_t l;
    int16_t r;
};

class AlsaAudioBackend final : public IAudioBackend {
public:
    ~AlsaAudioBackend() override { stop(); }

    bool start(Engine& engine, const AudioConfig& cfg) override {
        if (running_.load()) return true;
        engine_ = &engine;
        cfg_ = cfg;

        if (!openPcm(playback_, cfg_.device.c_str(), SND_PCM_STREAM_PLAYBACK, playFormat_, playChannels_)) {
            closeHandles();
            return false;
        }
        // Capture is optional if user runs output-only card, though full duplex is standard (Spec 2)
        hasCapture_ = openPcm(capture_, cfg_.device.c_str(), SND_PCM_STREAM_CAPTURE, capFormat_, capChannels_);

        engine_->prepare(static_cast<double>(actualRate_), static_cast<int>(actualPeriod_));

        snd_pcm_prepare(playback_);
        if (hasCapture_) {
            snd_pcm_prepare(capture_);
            snd_pcm_start(capture_);
        }

        running_.store(true);
        worker_ = std::thread([this]() { audioThread(); });
        return true;
    }

    void stop() override {
        if (!running_.exchange(false)) return;
        if (worker_.joinable()) worker_.join();
        closeHandles();
    }

    bool isRunning() const noexcept override { return running_.load(); }
    const char* backendName() const noexcept override { return "ALSA-Duplex"; }
    uint64_t xrunCount() const noexcept override { return xruns_.load(std::memory_order_relaxed); }

    void setMonitorEnabled(bool enabled) noexcept override {
        monitorEnabled_.store(enabled, std::memory_order_relaxed);
    }

    size_t popMonitorStereo(int16_t* dstInterleaved, size_t maxFrames) noexcept override {
        size_t n = 0;
        StereoFrame16 f{};
        while (n < maxFrames && monitorRing_.pop(f)) {
            dstInterleaved[n * 2]     = f.l;
            dstInterleaved[n * 2 + 1] = f.r;
            ++n;
        }
        return n;
    }

private:
    bool openPcm(snd_pcm_t*& handle,
                 const char* dev,
                 snd_pcm_stream_t stream,
                 snd_pcm_format_t& chosenFmt,
                 unsigned int& chosenCh) {
        if (snd_pcm_open(&handle, dev, stream, 0) < 0) {
            handle = nullptr;
            return false;
        }

        snd_pcm_hw_params_t* hw = nullptr;
        snd_pcm_hw_params_alloca(&hw);
        if (snd_pcm_hw_params_any(handle, hw) < 0) return false;
        if (snd_pcm_hw_params_set_access(handle, hw, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) return false;

        // Spec 2: 24-bit preferred (S24_3LE or S32_LE container for USB class-compliant interfaces)
        const snd_pcm_format_t candidates[] = {
            SND_PCM_FORMAT_S24_3LE,
            SND_PCM_FORMAT_S32_LE,
            SND_PCM_FORMAT_FLOAT_LE,
            SND_PCM_FORMAT_S16_LE
        };
        bool fmtOk = false;
        for (snd_pcm_format_t fmt : candidates) {
            if (snd_pcm_hw_params_set_format(handle, hw, fmt) == 0) {
                chosenFmt = fmt;
                fmtOk = true;
                break;
            }
        }
        if (!fmtOk) return false;

        chosenCh = 2;
        if (snd_pcm_hw_params_set_channels(handle, hw, 2) < 0) {
            chosenCh = 1;
            if (snd_pcm_hw_params_set_channels(handle, hw, 1) < 0) return false;
        }

        unsigned int rate = cfg_.sampleRate;
        if (snd_pcm_hw_params_set_rate_near(handle, hw, &rate, nullptr) < 0) return false;
        actualRate_ = rate;

        snd_pcm_uframes_t period = cfg_.periodFrames;
        if (snd_pcm_hw_params_set_period_size_near(handle, hw, &period, nullptr) < 0) return false;
        actualPeriod_ = static_cast<uint32_t>(period);

        snd_pcm_uframes_t bufSize = period * std::max<uint32_t>(2, cfg_.periods);
        snd_pcm_hw_params_set_buffer_size_near(handle, hw, &bufSize);

        if (snd_pcm_hw_params(handle, hw) < 0) return false;
        return true;
    }

    void closeHandles() {
        if (playback_) {
            snd_pcm_close(playback_);
            playback_ = nullptr;
        }
        if (capture_) {
            snd_pcm_close(capture_);
            capture_ = nullptr;
        }
        hasCapture_ = false;
    }

    static size_t bytesPerSample(snd_pcm_format_t fmt) noexcept {
        switch (fmt) {
            case SND_PCM_FORMAT_S24_3LE: return 3;
            case SND_PCM_FORMAT_S32_LE:
            case SND_PCM_FORMAT_FLOAT_LE: return 4;
            case SND_PCM_FORMAT_S16_LE:
            default: return 2;
        }
    }

    static void decodeCaptureToMono(const uint8_t* raw,
                                    float* dstMono,
                                    size_t frames,
                                    snd_pcm_format_t fmt,
                                    unsigned int ch) noexcept {
        const size_t bps = bytesPerSample(fmt);
        const size_t frameBytes = bps * ch;
        for (size_t i = 0; i < frames; ++i) {
            const uint8_t* p = raw + i * frameBytes;
            float s = 0.0f;
            if (fmt == SND_PCM_FORMAT_S24_3LE) {
                int32_t v = static_cast<int32_t>(p[0]) |
                            (static_cast<int32_t>(p[1]) << 8) |
                            (static_cast<int32_t>(p[2]) << 16);
                if (v & 0x800000) v |= ~0xFFFFFF;
                s = static_cast<float>(v) * (1.0f / 8388607.0f);
            } else if (fmt == SND_PCM_FORMAT_S32_LE) {
                int32_t v = 0;
                std::memcpy(&v, p, 4);
                s = static_cast<float>(v) * (1.0f / 2147483648.0f);
            } else if (fmt == SND_PCM_FORMAT_FLOAT_LE) {
                std::memcpy(&s, p, 4);
            } else {
                int16_t v = 0;
                std::memcpy(&v, p, 2);
                s = static_cast<float>(v) * (1.0f / 32768.0f);
            }
            dstMono[i] = s;
        }
    }

    static void encodeStereoPlayback(const float* srcStereo,
                                     uint8_t* raw,
                                     size_t frames,
                                     snd_pcm_format_t fmt,
                                     unsigned int ch) noexcept {
        const size_t bps = bytesPerSample(fmt);
        for (size_t i = 0; i < frames; ++i) {
            const float samples[2] = {
                std::clamp(srcStereo[i * 2], -1.0f, 1.0f),
                std::clamp(srcStereo[i * 2 + 1], -1.0f, 1.0f)
            };
            for (unsigned int c = 0; c < ch; ++c) {
                const float s = (ch == 1) ? 0.5f * (samples[0] + samples[1]) : samples[c];
                uint8_t* dst = raw + (i * ch + c) * bps;
                if (fmt == SND_PCM_FORMAT_S24_3LE) {
                    const int32_t pcm = static_cast<int32_t>(std::lround(s * 8388607.0f));
                    dst[0] = static_cast<uint8_t>(pcm & 0xFF);
                    dst[1] = static_cast<uint8_t>((pcm >> 8) & 0xFF);
                    dst[2] = static_cast<uint8_t>((pcm >> 16) & 0xFF);
                } else if (fmt == SND_PCM_FORMAT_S32_LE) {
                    const int32_t pcm = static_cast<int32_t>(std::llround(static_cast<double>(s) * 2147483647.0));
                    std::memcpy(dst, &pcm, 4);
                } else if (fmt == SND_PCM_FORMAT_FLOAT_LE) {
                    std::memcpy(dst, &s, 4);
                } else {
                    const int16_t pcm = static_cast<int16_t>(std::lround(s * 32767.0f));
                    std::memcpy(dst, &pcm, 2);
                }
            }
        }
    }

    void audioThread() {
        // Attempt real-time SCHED_FIFO priority (silently ignored if unprivileged)
        sched_param sch{};
        sch.sched_priority = 70;
        pthread_setschedparam(pthread_self(), SCHED_FIFO, &sch);

        const size_t frames = actualPeriod_;
        std::vector<float> inMono(frames, 0.0f);
        std::vector<float> outStereo(frames * 2, 0.0f);
        std::vector<uint8_t> rawCap(frames * capChannels_ * bytesPerSample(capFormat_), 0);
        std::vector<uint8_t> rawPlay(frames * playChannels_ * bytesPerSample(playFormat_), 0);

        while (running_.load(std::memory_order_relaxed)) {
            if (hasCapture_) {
                snd_pcm_sframes_t r = snd_pcm_readi(capture_, rawCap.data(), frames);
                if (r < 0) {
                    xruns_.fetch_add(1, std::memory_order_relaxed);
                    snd_pcm_recover(capture_, static_cast<int>(r), 1);
                    std::fill(inMono.begin(), inMono.end(), 0.0f);
                } else {
                    decodeCaptureToMono(rawCap.data(), inMono.data(), static_cast<size_t>(r), capFormat_, capChannels_);
                    if (static_cast<size_t>(r) < frames) {
                        std::fill(inMono.begin() + r, inMono.end(), 0.0f);
                    }
                }
            }

            engine_->processStereo(inMono.data(), outStereo.data(), static_cast<int>(frames));

            if (monitorEnabled_.load(std::memory_order_relaxed)) {
                for (size_t i = 0; i < frames; ++i) {
                    monitorRing_.push({
                        static_cast<int16_t>(std::lround(std::clamp(outStereo[i * 2], -1.0f, 1.0f) * 32767.0f)),
                        static_cast<int16_t>(std::lround(std::clamp(outStereo[i * 2 + 1], -1.0f, 1.0f) * 32767.0f))
                    });
                }
            }

            encodeStereoPlayback(outStereo.data(), rawPlay.data(), frames, playFormat_, playChannels_);

            size_t written = 0;
            const size_t frameStride = playChannels_ * bytesPerSample(playFormat_);
            while (written < frames && running_.load(std::memory_order_relaxed)) {
                snd_pcm_sframes_t w = snd_pcm_writei(playback_,
                                                     rawPlay.data() + written * frameStride,
                                                     frames - written);
                if (w < 0) {
                    xruns_.fetch_add(1, std::memory_order_relaxed);
                    if (snd_pcm_recover(playback_, static_cast<int>(w), 1) < 0) {
                        break;
                    }
                } else {
                    written += static_cast<size_t>(w);
                }
            }
        }
    }

    Engine* engine_ = nullptr;
    AudioConfig cfg_{};
    snd_pcm_t* playback_ = nullptr;
    snd_pcm_t* capture_ = nullptr;
    bool hasCapture_ = false;

    snd_pcm_format_t playFormat_ = SND_PCM_FORMAT_S16_LE;
    snd_pcm_format_t capFormat_ = SND_PCM_FORMAT_S16_LE;
    unsigned int playChannels_ = 2;
    unsigned int capChannels_ = 1;
    uint32_t actualRate_ = 48000;
    uint32_t actualPeriod_ = 128;

    std::atomic<bool> running_{false};
    std::atomic<bool> monitorEnabled_{false};
    std::atomic<uint64_t> xruns_{0};
    std::thread worker_;
    SpscRingBuffer<StereoFrame16, 32768> monitorRing_;
};

} // namespace

std::unique_ptr<IAudioBackend> createAlsaAudioBackend() {
    return std::make_unique<AlsaAudioBackend>();
}

} // namespace biomus

#else

namespace biomus {
std::unique_ptr<IAudioBackend> createAlsaAudioBackend() { return nullptr; }
} // namespace biomus

#endif
