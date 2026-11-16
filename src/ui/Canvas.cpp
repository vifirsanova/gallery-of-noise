#include "ui/Canvas.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>

namespace biomus::ui {

namespace {

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

// Full 5x7 ASCII glyph table (0x20..0x7E = 95 printable characters)
constexpr uint8_t kGlyphs95[95][5] = {
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
    {0x40,0x40,0x40,0x40,0x40}, // '_'
    {0x00,0x01,0x02,0x04,0x00}, // '`'
    {0x20,0x54,0x54,0x54,0x78}, // 'a'
    {0x7F,0x48,0x44,0x44,0x38}, // 'b'
    {0x38,0x44,0x44,0x44,0x20}, // 'c'
    {0x38,0x44,0x44,0x48,0x7F}, // 'd'
    {0x38,0x54,0x54,0x54,0x18}, // 'e'
    {0x08,0x7E,0x09,0x01,0x02}, // 'f'
    {0x0C,0x52,0x52,0x52,0x3E}, // 'g'
    {0x7F,0x08,0x04,0x04,0x78}, // 'h'
    {0x00,0x44,0x7D,0x40,0x00}, // 'i'
    {0x20,0x40,0x44,0x3D,0x00}, // 'j'
    {0x7F,0x10,0x28,0x44,0x00}, // 'k'
    {0x00,0x41,0x7F,0x40,0x00}, // 'l'
    {0x7C,0x04,0x18,0x04,0x78}, // 'm'
    {0x7C,0x08,0x04,0x04,0x78}, // 'n'
    {0x38,0x44,0x44,0x44,0x38}, // 'o'
    {0x7C,0x14,0x14,0x14,0x08}, // 'p'
    {0x08,0x14,0x14,0x18,0x7C}, // 'q'
    {0x7C,0x08,0x04,0x04,0x08}, // 'r'
    {0x48,0x54,0x54,0x54,0x20}, // 's'
    {0x04,0x3F,0x44,0x40,0x20}, // 't'
    {0x3C,0x40,0x40,0x20,0x7C}, // 'u'
    {0x1C,0x20,0x40,0x20,0x1C}, // 'v'
    {0x3C,0x40,0x30,0x40,0x3C}, // 'w'
    {0x44,0x28,0x10,0x28,0x44}, // 'x'
    {0x0C,0x50,0x50,0x50,0x3C}, // 'y'
    {0x44,0x64,0x54,0x4C,0x44}, // 'z'
    {0x00,0x08,0x36,0x41,0x00}, // '{'
    {0x00,0x00,0x7F,0x00,0x00}, // '|'
    {0x00,0x41,0x36,0x08,0x00}, // '}'
    {0x08,0x04,0x08,0x10,0x08}  // '~'
};

uint32_t crc32Png(const uint8_t* data, size_t len) noexcept {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return ~crc;
}

void writeU32BE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFFu));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    out.push_back(static_cast<uint8_t>(v & 0xFFu));
}

void appendPngChunk(std::vector<uint8_t>& png, const char type[4], const std::vector<uint8_t>& payload) {
    writeU32BE(png, static_cast<uint32_t>(payload.size()));
    const size_t typePos = png.size();
    png.insert(png.end(), reinterpret_cast<const uint8_t*>(type), reinterpret_cast<const uint8_t*>(type) + 4);
    png.insert(png.end(), payload.begin(), payload.end());
    const uint32_t crc = crc32Png(png.data() + typePos, 4 + payload.size());
    writeU32BE(png, crc);
}

} // namespace

Canvas::Canvas(int width, int height)
    : width_(std::max(16, width)),
      height_(std::max(16, height)),
      pixels_(static_cast<size_t>(width_ * height_), palette::kBg.toARGB()) {}

void Canvas::clear(Color c) noexcept {
    std::fill(pixels_.begin(), pixels_.end(), c.toARGB());
}

void Canvas::putPixel(int x, int y, Color c) noexcept {
    if (x < 0 || x >= width_ || y < 0 || y >= height_ || c.a == 0) return;
    uint32_t& dst = pixels_[static_cast<size_t>(y * width_ + x)];
    if (c.a == 255) {
        dst = c.toARGB();
        return;
    }
    const uint32_t a = c.a;
    const uint32_t ia = 255u - a;
    const uint8_t dr = static_cast<uint8_t>((dst >> 16) & 0xFFu);
    const uint8_t dg = static_cast<uint8_t>((dst >> 8) & 0xFFu);
    const uint8_t db = static_cast<uint8_t>(dst & 0xFFu);
    const uint8_t rr = static_cast<uint8_t>((c.r * a + dr * ia) / 255u);
    const uint8_t rg = static_cast<uint8_t>((c.g * a + dg * ia) / 255u);
    const uint8_t rb = static_cast<uint8_t>((c.b * a + db * ia) / 255u);
    dst = (0xFFu << 24) | (static_cast<uint32_t>(rr) << 16) | (static_cast<uint32_t>(rg) << 8) | rb;
}

void Canvas::fillRect(int x, int y, int w, int h, Color c) noexcept {
    if (w <= 0 || h <= 0 || c.a == 0) return;
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(width_, x + w);
    const int y1 = std::min(height_, y + h);
    if (x0 >= x1 || y0 >= y1) return;

    if (c.a == 255) {
        const uint32_t px = c.toARGB();
        for (int py = y0; py < y1; ++py) {
            uint32_t* row = pixels_.data() + static_cast<size_t>(py * width_);
            std::fill(row + x0, row + x1, px);
        }
    } else {
        for (int py = y0; py < y1; ++py) {
            for (int px = x0; px < x1; ++px) putPixel(px, py, c);
        }
    }
}

void Canvas::drawRect(int x, int y, int w, int h, Color c) noexcept {
    if (w <= 0 || h <= 0) return;
    drawHLine(x, y, w, c);
    drawHLine(x, y + h - 1, w, c);
    drawVLine(x, y, h, c);
    drawVLine(x + w - 1, y, h, c);
}

void Canvas::drawPanel(int x, int y, int w, int h, Color topAccent, Color bg) noexcept {
    fillRect(x, y, w, h, bg);
    drawRect(x, y, w, h, palette::kBorder);
    // Signature glowing 2px top hardware accent strip + subtle inner glow
    fillRect(x + 1, y, w - 2, 2, topAccent);
    fillRect(x + 1, y + 2, w - 2, 3, topAccent.withAlpha(32));
}

void Canvas::drawHLine(int x, int y, int len, Color c) noexcept {
    fillRect(x, y, len, 1, c);
}

void Canvas::drawVLine(int x, int y, int len, Color c) noexcept {
    fillRect(x, y, 1, len, c);
}

void Canvas::drawLine(int x0, int y0, int x1, int y1, Color c) noexcept {
    int dx = std::abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
    int dy = -std::abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;
    while (true) {
        putPixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        const int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Canvas::fillCircle(int cx, int cy, int radius, Color c) noexcept {
    if (radius <= 0) {
        putPixel(cx, cy, c);
        return;
    }
    const int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= r2) {
                putPixel(cx + dx, cy + dy, c);
            }
        }
    }
}

void Canvas::drawCircle(int cx, int cy, int radius, Color c) noexcept {
    if (radius <= 0) return;
    const float rIn = static_cast<float>(radius) - 0.7f;
    const float rOut = static_cast<float>(radius) + 0.7f;
    const int bound = radius + 1;
    for (int dy = -bound; dy <= bound; ++dy) {
        for (int dx = -bound; dx <= bound; ++dx) {
            const float d = std::sqrt(static_cast<float>(dx * dx + dy * dy));
            if (d >= rIn && d <= rOut) {
                putPixel(cx + dx, cy + dy, c);
            }
        }
    }
}

void Canvas::drawRotaryArc(int cx, int cy, int radius, int thickness,
                           float startDeg, float endDeg, Color c) noexcept {
    if (radius <= 1 || thickness <= 0) return;
    if (endDeg < startDeg) std::swap(startDeg, endDeg);
    const float span = endDeg - startDeg;
    if (span < 0.5f) return;

    // Step in fine angular increments for smooth solid hardware ring
    const float stepDeg = std::max(0.6f, 35.0f / static_cast<float>(radius));
    const int halfT = thickness / 2;
    for (float deg = startDeg; deg <= endDeg; deg += stepDeg) {
        // 0 deg = 12 o'clock (up), clockwise positive
        const float rad = (deg - 90.0f) * kDegToRad;
        const float cs = std::cos(rad);
        const float sn = std::sin(rad);
        const int px = static_cast<int>(std::lround(static_cast<float>(cx) + cs * static_cast<float>(radius)));
        const int py = static_cast<int>(std::lround(static_cast<float>(cy) + sn * static_cast<float>(radius)));
        fillCircle(px, py, std::max(1, halfT), c);
    }
}

int Canvas::textWidth(const char* text, int scale) noexcept {
    if (!text || !*text || scale <= 0) return 0;
    const int len = static_cast<int>(std::strlen(text));
    return (len * 6 - 1) * scale;
}

void Canvas::drawText(int x, int y, const char* text, Color c, int scale) noexcept {
    if (!text || scale <= 0) return;
    int cx = x;
    for (const char* p = text; *p; ++p) {
        int ch = static_cast<unsigned char>(*p);
        if (ch < 0x20 || ch > 0x7E) ch = '?';
        const uint8_t* glyph = kGlyphs95[ch - 0x20];
        for (int col = 0; col < 5; ++col) {
            const uint8_t bits = glyph[col];
            if (!bits) continue;
            for (int row = 0; row < 7; ++row) {
                if (bits & (1u << row)) {
                    if (scale == 1) {
                        putPixel(cx + col, y + row, c);
                    } else {
                        fillRect(cx + col * scale, y + row * scale, scale, scale, c);
                    }
                }
            }
        }
        cx += 6 * scale;
        if (cx >= width_) break;
    }
}

void Canvas::drawTextCentered(int x, int y, int w, const char* text, Color c, int scale) noexcept {
    const int tw = textWidth(text, scale);
    drawText(x + (w - tw) / 2, y, text, c, scale);
}

void Canvas::drawTextRight(int rightX, int y, const char* text, Color c, int scale) noexcept {
    const int tw = textWidth(text, scale);
    drawText(rightX - tw, y, text, c, scale);
}

bool Canvas::savePng(const std::string& path) const {
    // Build raw scanlines: 1 filter byte (0) + width * 3 RGB bytes per row
    const size_t rowBytes = 1 + static_cast<size_t>(width_) * 3;
    std::vector<uint8_t> raw(rowBytes * static_cast<size_t>(height_), 0);
    for (int y = 0; y < height_; ++y) {
        uint8_t* dst = raw.data() + static_cast<size_t>(y) * rowBytes;
        dst[0] = 0; // Filter type 0 (None)
        const uint32_t* src = pixels_.data() + static_cast<size_t>(y * width_);
        for (int x = 0; x < width_; ++x) {
            const uint32_t px = src[x];
            dst[1 + x * 3]     = static_cast<uint8_t>((px >> 16) & 0xFFu);
            dst[1 + x * 3 + 1] = static_cast<uint8_t>((px >> 8) & 0xFFu);
            dst[1 + x * 3 + 2] = static_cast<uint8_t>(px & 0xFFu);
        }
    }

    // Wrap raw scanlines in RFC 1950 zlib stream using stored (uncompressed) DEFLATE blocks
    std::vector<uint8_t> zstream;
    zstream.reserve(raw.size() + (raw.size() / 65535 + 2) * 5 + 6);
    zstream.push_back(0x78); // CMF (deflate, 32K window)
    zstream.push_back(0x01); // FLG (no preset dict, check bits)

    uint32_t s1 = 1, s2 = 0;
    size_t pos = 0;
    while (pos < raw.size()) {
        const size_t blk = std::min<size_t>(65535, raw.size() - pos);
        const bool isLast = (pos + blk == raw.size());
        zstream.push_back(isLast ? 0x01 : 0x00);
        const uint16_t len16 = static_cast<uint16_t>(blk);
        const uint16_t nlen16 = static_cast<uint16_t>(~len16);
        zstream.push_back(static_cast<uint8_t>(len16 & 0xFFu));
        zstream.push_back(static_cast<uint8_t>((len16 >> 8) & 0xFFu));
        zstream.push_back(static_cast<uint8_t>(nlen16 & 0xFFu));
        zstream.push_back(static_cast<uint8_t>((nlen16 >> 8) & 0xFFu));
        zstream.insert(zstream.end(), raw.begin() + static_cast<ptrdiff_t>(pos),
                       raw.begin() + static_cast<ptrdiff_t>(pos + blk));

        for (size_t i = 0; i < blk; ++i) {
            s1 = (s1 + raw[pos + i]) % 65521u;
            s2 = (s2 + s1) % 65521u;
        }
        pos += blk;
    }
    writeU32BE(zstream, (s2 << 16) | s1); // Adler-32

    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};

    std::vector<uint8_t> ihdr;
    writeU32BE(ihdr, static_cast<uint32_t>(width_));
    writeU32BE(ihdr, static_cast<uint32_t>(height_));
    ihdr.push_back(8); // bit depth 8
    ihdr.push_back(2); // color type 2 (Truecolor RGB)
    ihdr.push_back(0); // compression
    ihdr.push_back(0); // filter
    ihdr.push_back(0); // interlace
    appendPngChunk(png, "IHDR", ihdr);
    appendPngChunk(png, "IDAT", zstream);
    appendPngChunk(png, "IEND", {});

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return f.good();
}

} // namespace biomus::ui
