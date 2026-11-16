#include "app/OledDisplay.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>

#ifdef __linux__
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

namespace biomus {

namespace {

inline void setPixel(std::array<uint8_t, OledDisplay::kBufferBytes>& fb, int x, int y, bool on = true) noexcept {
    if (x < 0 || x >= OledDisplay::kWidth || y < 0 || y >= OledDisplay::kHeight) return;
    const size_t idx = static_cast<size_t>((y / 8) * OledDisplay::kWidth + x);
    const uint8_t mask = static_cast<uint8_t>(1u << (y & 7));
    if (on) fb[idx] |= mask;
    else    fb[idx] &= static_cast<uint8_t>(~mask);
}

inline void drawRect(std::array<uint8_t, OledDisplay::kBufferBytes>& fb,
                     int x0, int y0, int w, int h, bool fill) noexcept {
    for (int dy = 0; dy < h; ++dy) {
        for (int dx = 0; dx < w; ++dx) {
            if (fill || dy == 0 || dy == h - 1 || dx == 0 || dx == w - 1) {
                setPixel(fb, x0 + dx, y0 + dy, true);
            }
        }
    }
}

inline void drawBar(std::array<uint8_t, OledDisplay::kBufferBytes>& fb,
                    int x0, int y0, int w, int h, float norm) noexcept {
    drawRect(fb, x0, y0, w, h, false);
    const float c = (norm < 0.0f) ? 0.0f : ((norm > 1.0f) ? 1.0f : norm);
    const int fillW = static_cast<int>(c * static_cast<float>(w - 2) + 0.5f);
    if (fillW > 0 && h > 2) {
        drawRect(fb, x0 + 1, y0 + 1, fillW, h - 2, true);
    }
}

// Compact 5x7 ASCII glyphs for digits, uppercase, punctuation (0x20..0x5F)
constexpr uint8_t kFont5x7[64][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // ' '
    {0x00,0x00,0x5F,0x00,0x00}, // '!'
    {0x00,0x07,0x00,0x07,0x00}, // '"'
    {0x14,0x7F,0x14,0x7F,0x14}, // '#'
    {0x24,0x2A,0x7F,0x2A,0x12}, // '$'
    {0x23,0x13,0x08,0x64,0x62}, // '%'
    {0x36,0x49,0x55,0x22,0x50}, // '&'
    {0x00,0x05,0x03,0x00,0x00}, // '\''
    {0x00,0x1C,0x22,0x41,0x00}, // '('
    {0x00,0x41,0x22,0x1C,0x00}, // ')'
    {0x14,0x08,0x3E,0x08,0x14}, // '*'
    {0x08,0x08,0x3E,0x08,0x08}, // '+'
    {0x00,0x50,0x30,0x00,0x00}, // ','
    {0x08,0x08,0x08,0x08,0x08}, // '-'
    {0x00,0x60,0x60,0x00,0x00}, // '.'
    {0x20,0x10,0x08,0x04,0x02}, // '/'
    {0x3E,0x51,0x49,0x45,0x3E}, // '0'
    {0x00,0x42,0x7F,0x40,0x00}, // '1'
    {0x42,0x61,0x51,0x49,0x46}, // '2'
    {0x21,0x41,0x45,0x4B,0x31}, // '3'
    {0x18,0x14,0x12,0x7F,0x10}, // '4'
    {0x27,0x45,0x45,0x45,0x39}, // '5'
    {0x3C,0x4A,0x49,0x49,0x30}, // '6'
    {0x01,0x71,0x09,0x05,0x03}, // '7'
    {0x36,0x49,0x49,0x49,0x36}, // '8'
    {0x06,0x49,0x49,0x29,0x1E}, // '9'
    {0x00,0x36,0x36,0x00,0x00}, // ':'
    {0x00,0x56,0x36,0x00,0x00}, // ';'
    {0x08,0x14,0x22,0x41,0x00}, // '<'
    {0x14,0x14,0x14,0x14,0x14}, // '='
    {0x00,0x41,0x22,0x14,0x08}, // '>'
    {0x02,0x01,0x51,0x09,0x06}, // '?'
    {0x32,0x49,0x79,0x41,0x3E}, // '@'
    {0x7E,0x11,0x11,0x11,0x7E}, // 'A'
    {0x7F,0x49,0x49,0x49,0x36}, // 'B'
    {0x3E,0x41,0x41,0x41,0x22}, // 'C'
    {0x7F,0x41,0x41,0x22,0x1C}, // 'D'
    {0x7F,0x49,0x49,0x49,0x41}, // 'E'
    {0x7F,0x09,0x09,0x09,0x01}, // 'F'
    {0x3E,0x41,0x49,0x49,0x7A}, // 'G'
    {0x7F,0x08,0x08,0x08,0x7F}, // 'H'
    {0x00,0x41,0x7F,0x41,0x00}, // 'I'
    {0x20,0x40,0x41,0x3F,0x01}, // 'J'
    {0x7F,0x08,0x14,0x22,0x41}, // 'K'
    {0x7F,0x40,0x40,0x40,0x40}, // 'L'
    {0x7F,0x02,0x0C,0x02,0x7F}, // 'M'
    {0x7F,0x04,0x08,0x10,0x7F}, // 'N'
    {0x3E,0x41,0x41,0x41,0x3E}, // 'O'
    {0x7F,0x09,0x09,0x09,0x06}, // 'P'
    {0x3E,0x41,0x51,0x21,0x5E}, // 'Q'
    {0x7F,0x09,0x19,0x29,0x46}, // 'R'
    {0x46,0x49,0x49,0x49,0x31}, // 'S'
    {0x01,0x01,0x7F,0x01,0x01}, // 'T'
    {0x3F,0x40,0x40,0x40,0x3F}, // 'U'
    {0x1F,0x20,0x40,0x20,0x1F}, // 'V'
    {0x3F,0x40,0x38,0x40,0x3F}, // 'W'
    {0x63,0x14,0x08,0x14,0x63}, // 'X'
    {0x07,0x08,0x70,0x08,0x07}, // 'Y'
    {0x61,0x51,0x49,0x45,0x43}, // 'Z'
    {0x00,0x7F,0x41,0x41,0x00}, // '['
    {0x02,0x04,0x08,0x10,0x20}, // '\\'
    {0x00,0x41,0x41,0x7F,0x00}, // ']'
    {0x04,0x02,0x01,0x02,0x04}, // '^'
    {0x40,0x40,0x40,0x40,0x40}  // '_'
};

void drawText(std::array<uint8_t, OledDisplay::kBufferBytes>& fb, int x, int y, const char* str) noexcept {
    int cx = x;
    for (const char* p = str; *p; ++p) {
        char ch = *p;
        if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
        if (ch < 0x20 || ch > 0x5F) ch = '?';
        const uint8_t* glyph = kFont5x7[static_cast<int>(ch) - 0x20];
        for (int col = 0; col < 5; ++col) {
            const uint8_t bits = glyph[col];
            for (int row = 0; row < 7; ++row) {
                if (bits & (1u << row)) {
                    setPixel(fb, cx + col, y + row, true);
                }
            }
        }
        cx += 6;
        if (cx + 5 > OledDisplay::kWidth) break;
    }
}

} // namespace

OledDisplay::OledDisplay(Engine& engine) : engine_(engine) {}

OledDisplay::~OledDisplay() {
    stop();
}

bool OledDisplay::initI2cHardware(const std::string& dev, uint8_t addr) {
#ifdef __linux__
    const int fd = ::open(dev.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) return false;
    if (::ioctl(fd, I2C_SLAVE, addr) < 0) {
        ::close(fd);
        return false;
    }

    // Standard SSD1306 128x64 initialization sequence (Horizontal Addressing Mode)
    const uint8_t initSeq[] = {
        0x00, // Control byte: command stream
        0xAE, // Display OFF
        0xD5, 0x80, // Clock divide
        0xA8, 0x3F, // Multiplex 64
        0xD3, 0x00, // Offset 0
        0x40,       // Start line 0
        0x8D, 0x14, // Charge pump ON
        0x20, 0x00, // Horizontal addressing mode
        0xA1,       // Segment remap
        0xC8,       // COM scan dec
        0xDA, 0x12, // COM pins
        0x81, 0xCF, // Contrast
        0xD9, 0xF1, // Precharge
        0xDB, 0x40, // VCOMH
        0xA4,       // Resume to RAM content
        0xA6,       // Normal display
        0xAF        // Display ON
    };

    if (::write(fd, initSeq, sizeof(initSeq)) != static_cast<ssize_t>(sizeof(initSeq))) {
        ::close(fd);
        return false;
    }
    i2cFd_ = fd;
    return true;
#else
    (void)dev;
    (void)addr;
    return false;
#endif
}

void OledDisplay::flushToI2c(const std::array<uint8_t, kBufferBytes>& fb) {
#ifdef __linux__
    if (i2cFd_ < 0) return;
    const uint8_t addrCmd[] = {0x00, 0x21, 0x00, 0x7F, 0x22, 0x00, 0x07};
    if (::write(i2cFd_, addrCmd, sizeof(addrCmd)) < 0) return;

    uint8_t packet[1 + kBufferBytes];
    packet[0] = 0x40; // Data stream
    std::memcpy(packet + 1, fb.data(), kBufferBytes);
    (void)::write(i2cFd_, packet, sizeof(packet));
#else
    (void)fb;
#endif
}

void OledDisplay::renderFrame(const FullState& s, std::array<uint8_t, kBufferBytes>& fb) noexcept {
    fb.fill(0);

    // Line 0 (y=0..7): Preset + Flags + BPM
    char line0[32];
    std::snprintf(line0, sizeof(line0), "%-7s %c%c%c %3dB",
                  toString(s.preset),
                  s.mic ? 'M' : '-',
                  s.droneOn ? 'D' : '-',
                  s.bypass ? '!' : (s.knobMode == KnobMode::XY ? 'X' : 'P'),
                  s.bio.bpm);
    drawText(fb, 0, 0, line0);

    // Thin divider at y=9
    for (int x = 0; x < kWidth; ++x) setPixel(fb, x, 9, true);

    // Line 1 (y=12..19): Active Voice FX + XY bars
    const auto vIdx = static_cast<size_t>(s.voice.active);
    char lineV[24];
    std::snprintf(lineV, sizeof(lineV), "V:%-8s%s",
                  toString(s.voice.active),
                  s.voice.enabled[vIdx] ? "+" : "-");
    drawText(fb, 0, 12, lineV);
    drawBar(fb, 70, 12, 27, 7, s.voice.xy[vIdx].x);
    drawBar(fb, 100, 12, 27, 7, s.voice.xy[vIdx].y);

    // Line 2 (y=22..29): Active Drone FX + XY bars
    const auto dIdx = static_cast<size_t>(s.drone.active);
    char lineD[24];
    std::snprintf(lineD, sizeof(lineD), "D:%-8s%s",
                  toString(s.drone.active),
                  s.drone.enabled[dIdx] ? "+" : "-");
    drawText(fb, 0, 22, lineD);
    drawBar(fb, 70, 22, 27, 7, s.drone.xy[dIdx].x);
    drawBar(fb, 100, 22, 27, 7, s.drone.xy[dIdx].y);

    // Line 3 (y=33..40): 16-step Noise Sequencer strip (active track)
    const int trIdx = std::clamp(s.noise.activeTrack, 0, static_cast<int>(kNumNoiseTracks - 1));
    const NoiseTrack& tr = s.noise.tracks[static_cast<size_t>(trIdx)];
    char trLbl[16];
    std::snprintf(trLbl, sizeof(trLbl), "N%d", trIdx + 1);
    drawText(fb, 0, 33, trLbl);

    for (int st = 0; st < 16; ++st) {
        const int sx = 16 + st * 7;
        const bool trig = tr.steps[static_cast<size_t>(st)].trigger;
        const bool playhead = s.noise.playing && (tr.currentStep == st);
        drawRect(fb, sx, 33, 6, 7, trig);
        if (playhead) {
            setPixel(fb, sx + 2, 41, true);
            setPixel(fb, sx + 3, 41, true);
        }
    }

    // Line 4 (y=45..52): Bio bars (Breath, Heart, EyeX)
    drawText(fb, 0, 45, "B");
    drawBar(fb, 8, 45, 32, 6, s.bio.breath);
    drawText(fb, 44, 45, "H");
    drawBar(fb, 52, 45, 32, 6, s.bio.heart);
    drawText(fb, 88, 45, "E");
    drawBar(fb, 96, 45, 31, 6, s.bio.eyeX);

    // Line 5 (y=55..62): Master level VU meter + Clip indicator
    drawText(fb, 0, 55, "OUT");
    drawBar(fb, 22, 56, 96, 6, s.masterLevel);
    if (s.clip) {
        drawText(fb, 121, 55, "!");
    }
}

bool OledDisplay::start(const std::string& i2cDevice, uint8_t i2cAddr, int refreshHz) {
    if (running_.load()) return true;
    initI2cHardware(i2cDevice, i2cAddr);
    running_.store(true);
    const int hz = std::clamp(refreshHz, 5, 30);
    worker_ = std::thread([this, hz]() { displayLoop(hz); });
    return true;
}

void OledDisplay::stop() {
    if (!running_.exchange(false)) return;
    if (worker_.joinable()) worker_.join();
#ifdef __linux__
    if (i2cFd_ >= 0) {
        ::close(i2cFd_);
        i2cFd_ = -1;
    }
#endif
}

std::array<uint8_t, OledDisplay::kBufferBytes> OledDisplay::latestFramebuffer() const {
    std::lock_guard<std::mutex> lock(fbMutex_);
    return fb_;
}

void OledDisplay::displayLoop(int refreshHz) {
    const auto interval = std::chrono::milliseconds(1000 / refreshHz);
    std::array<uint8_t, kBufferBytes> localFb{};

    while (running_.load(std::memory_order_relaxed)) {
        const FullState snap = engine_.readSnapshot();
        renderFrame(snap, localFb);

        {
            std::lock_guard<std::mutex> lock(fbMutex_);
            fb_ = localFb;
        }
        flushToI2c(localFb);
        framesRendered_.fetch_add(1, std::memory_order_relaxed);
        std::this_thread::sleep_for(interval);
    }
}

} // namespace biomus
