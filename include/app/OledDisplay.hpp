#pragma once

#include "core/Engine.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

namespace biomus {

// ============================================================================
// OledDisplay (Spec Sections 2, 4.5, 7.3)
// 128x64 SSD1306 I2C OLED renderer running in dedicated 15 Hz thread.
// ============================================================================
class OledDisplay {
public:
    static constexpr int kWidth = 128;
    static constexpr int kHeight = 64;
    static constexpr size_t kBufferBytes = (kWidth * kHeight) / 8; // 1024 bytes

    explicit OledDisplay(Engine& engine);
    ~OledDisplay();

    bool start(const std::string& i2cDevice = "/dev/i2c-1", uint8_t i2cAddr = 0x3C, int refreshHz = 15);
    void stop();

    bool hasHardware() const noexcept { return i2cFd_ >= 0; }
    uint64_t frameCount() const noexcept { return framesRendered_.load(std::memory_order_relaxed); }

    // Renders a single frame from state into an SSD1306 page-major 1024-byte buffer
    static void renderFrame(const FullState& state, std::array<uint8_t, kBufferBytes>& fb) noexcept;

    std::array<uint8_t, kBufferBytes> latestFramebuffer() const;

private:
    bool initI2cHardware(const std::string& dev, uint8_t addr);
    void flushToI2c(const std::array<uint8_t, kBufferBytes>& fb);
    void displayLoop(int refreshHz);

    Engine& engine_;
    int i2cFd_ = -1;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> framesRendered_{0};
    std::thread worker_;
    mutable std::mutex fbMutex_;
    std::array<uint8_t, kBufferBytes> fb_{};
};

} // namespace biomus
