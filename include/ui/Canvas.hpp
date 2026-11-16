#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace biomus::ui {

struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;

    constexpr uint32_t toARGB() const noexcept {
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(r) << 16) |
               (static_cast<uint32_t>(g) << 8)  |
               static_cast<uint32_t>(b);
    }

    constexpr uint16_t toRGB565() const noexcept {
        return static_cast<uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
    }

    static constexpr Color rgb(uint8_t rr, uint8_t gg, uint8_t bb, uint8_t aa = 255) noexcept {
        return {rr, gg, bb, aa};
    }

    Color withAlpha(uint8_t aa) const noexcept { return {r, g, b, aa}; }
    Color dimmed(float factor) const noexcept {
        return {
            static_cast<uint8_t>(r * factor),
            static_cast<uint8_t>(g * factor),
            static_cast<uint8_t>(b * factor),
            a
        };
    }
};

// Signature hardware palette matching Voice Board dark cyber-modular theme
namespace palette {
constexpr Color kBg       = Color::rgb(10, 10, 12);
constexpr Color kPanel    = Color::rgb(17, 18, 22);
constexpr Color kPanelAlt = Color::rgb(12, 13, 16);
constexpr Color kBorder   = Color::rgb(36, 38, 46);
constexpr Color kGrid     = Color::rgb(26, 28, 34);
constexpr Color kText     = Color::rgb(228, 232, 240);
constexpr Color kDim      = Color::rgb(108, 114, 128);
constexpr Color kMuted    = Color::rgb(62, 66, 76);

constexpr Color kViolet   = Color::rgb(180, 68, 255);
constexpr Color kCyan     = Color::rgb(0, 238, 255);
constexpr Color kAmber    = Color::rgb(255, 174, 0);
constexpr Color kGreen    = Color::rgb(72, 245, 96);
constexpr Color kNoise    = Color::rgb(136, 255, 136);
constexpr Color kRed      = Color::rgb(255, 68, 68);

constexpr Color kBreath   = Color::rgb(68, 255, 221);
constexpr Color kHeart    = Color::rgb(255, 68, 170);
constexpr Color kEye      = Color::rgb(255, 221, 68);
} // namespace palette

// ============================================================================
// Software ARGB8888 Hardware Canvas (Default 480x320 — 3.5" RPi TFT native)
// Zero external font or graphics dependencies.
// ============================================================================
class Canvas {
public:
    Canvas(int width = 480, int height = 320);

    int width() const noexcept { return width_; }
    int height() const noexcept { return height_; }
    int strideBytes() const noexcept { return width_ * static_cast<int>(sizeof(uint32_t)); }
    const uint32_t* pixels() const noexcept { return pixels_.data(); }
    uint32_t* pixels() noexcept { return pixels_.data(); }
    const uint32_t* data() const noexcept { return pixels_.data(); }
    uint32_t* data() noexcept { return pixels_.data(); }

    static constexpr uint16_t argbToRgb565(uint32_t argb) noexcept {
        const uint32_t r = (argb >> 16) & 0xFFu;
        const uint32_t g = (argb >> 8)  & 0xFFu;
        const uint32_t b =  argb        & 0xFFu;
        return static_cast<uint16_t>(((r & 0xF8u) << 8) | ((g & 0xFCu) << 3) | (b >> 3));
    }

    void clear(Color c = palette::kBg) noexcept;
    void putPixel(int x, int y, Color c) noexcept;

    void fillRect(int x, int y, int w, int h, Color c) noexcept;
    void drawRect(int x, int y, int w, int h, Color c) noexcept;
    void drawPanel(int x, int y, int w, int h, Color topAccent, Color bg = palette::kPanel) noexcept;

    void drawHLine(int x, int y, int len, Color c) noexcept;
    void drawVLine(int x, int y, int len, Color c) noexcept;
    void drawLine(int x0, int y0, int x1, int y1, Color c) noexcept;

    void fillCircle(int cx, int cy, int radius, Color c) noexcept;
    void drawCircle(int cx, int cy, int radius, Color c) noexcept;

    // Draws a rotary encoder arc from startDeg to endDeg (degrees clockwise from 12 o'clock;
    // e.g. -135 .. +135 for standard 270-degree hardware synth potentiometer)
    void drawRotaryArc(int cx, int cy, int radius, int thickness,
                       float startDeg, float endDeg, Color c) noexcept;

    // Built-in crisp 5x7 pixel font at integer scale (1 = 5x7, 2 = 10x14, 3 = 15x21)
    void drawText(int x, int y, const char* text, Color c, int scale = 1) noexcept;
    void drawTextCentered(int x, int y, int w, const char* text, Color c, int scale = 1) noexcept;
    void drawTextRight(int rightX, int y, const char* text, Color c, int scale = 1) noexcept;
    static int textWidth(const char* text, int scale = 1) noexcept;

    // Exports canvas to lossless 24-bit RGB PNG file (pure C++17, zero external libs)
    bool savePng(const std::string& path) const;

private:
    int width_ = 480;
    int height_ = 320;
    std::vector<uint32_t> pixels_;
};

} // namespace biomus::ui
