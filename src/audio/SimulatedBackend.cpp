#include "audio/AudioBackend.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <vector>

namespace biomus {

namespace {

struct StereoFrame16 {
    int16_t l;
    int16_t r;
};

class SimulatedAudioBackend final : public IAudioBackend {
public:
    ~SimulatedAudioBackend() override { stop(); }

    bool start(Engine& engine, const AudioConfig& cfg) override {
        if (running_.load()) return true;
        engine_ = &engine;
        cfg_ = cfg;
        if (cfg_.sampleRate < 8000) cfg_.sampleRate = 48000;
        if (cfg_.periodFrames < 32) cfg_.periodFrames = 128;

        engine_->prepare(static_cast<double>(cfg_.sampleRate), static_cast<int>(cfg_.periodFrames));
        running_.store(true);
        worker_ = std::thread([this]() { threadLoop(); });
        return true;
    }

    void stop() override {
        if (!running_.exchange(false)) return;
        if (worker_.joinable()) worker_.join();
    }

    bool isRunning() const noexcept override { return running_.load(); }
    const char* backendName() const noexcept override { return "Simulated-RT-48kHz"; }
    uint64_t xrunCount() const noexcept override { return 0; }

    void setMonitorEnabled(bool enabled) noexcept override {
        monitorEnabled_.store(enabled, std::memory_order_relaxed);
    }

    size_t popMonitorStereo(int16_t* dstInterleaved, size_t maxFrames) noexcept override {
        size_t n = 0;
        StereoFrame16 frame{};
        while (n < maxFrames && monitorRing_.pop(frame)) {
            dstInterleaved[n * 2]     = frame.l;
            dstInterleaved[n * 2 + 1] = frame.r;
            ++n;
        }
        return n;
    }

private:
    void threadLoop() {
        using clock = std::chrono::steady_clock;
        const size_t frames = cfg_.periodFrames;
        std::vector<float> inMono(frames, 0.0f);
        std::vector<float> outStereo(frames * 2, 0.0f);

        const auto periodNs = std::chrono::nanoseconds(
            (static_cast<uint64_t>(frames) * 1000000000ULL) / cfg_.sampleRate);
        auto nextWake = clock::now();

        while (running_.load(std::memory_order_relaxed)) {
            engine_->processStereo(inMono.data(), outStereo.data(), static_cast<int>(frames));

            if (monitorEnabled_.load(std::memory_order_relaxed)) {
                for (size_t i = 0; i < frames; ++i) {
                    const float fl = std::clamp(outStereo[i * 2], -1.0f, 1.0f);
                    const float fr = std::clamp(outStereo[i * 2 + 1], -1.0f, 1.0f);
                    StereoFrame16 sf{
                        static_cast<int16_t>(std::lround(fl * 32767.0f)),
                        static_cast<int16_t>(std::lround(fr * 32767.0f))
                    };
                    monitorRing_.push(sf);
                }
            }

            nextWake += periodNs;
            const auto now = clock::now();
            if (nextWake > now) {
                std::this_thread::sleep_until(nextWake);
            } else if (now - nextWake > std::chrono::milliseconds(50)) {
                nextWake = now; // resync if OS paused thread
            }
        }
    }

    Engine* engine_ = nullptr;
    AudioConfig cfg_{};
    std::atomic<bool> running_{false};
    std::atomic<bool> monitorEnabled_{false};
    std::thread worker_;
    SpscRingBuffer<StereoFrame16, 32768> monitorRing_;
};

} // namespace

std::unique_ptr<IAudioBackend> createSimulatedAudioBackend() {
    return std::make_unique<SimulatedAudioBackend>();
}

std::unique_ptr<IAudioBackend> createAutoAudioBackend(Engine& engine, const AudioConfig& cfg) {
    if (!cfg.preferSimulated) {
        auto alsa = createAlsaAudioBackend();
        if (alsa && alsa->start(engine, cfg)) {
            return alsa;
        }
    }
    auto sim = createSimulatedAudioBackend();
    sim->start(engine, cfg);
    return sim;
}

} // namespace biomus
