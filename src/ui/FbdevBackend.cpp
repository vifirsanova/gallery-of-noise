#include "ui/DisplayBackend.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>

#ifdef __linux__
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <unistd.h>
#endif

namespace biomus::ui {

namespace {

class FbdevBackend final : public IDisplayBackend {
public:
    explicit FbdevBackend(std::string devPath)
        : devPath_(devPath.empty() ? "/dev/fb0" : std::move(devPath)) {}

    ~FbdevBackend() override { shutdown(); }

    bool init() override {
#ifdef __linux__
        fbFd_ = ::open(devPath_.c_str(), O_RDWR);
        if (fbFd_ < 0) return false;

        fb_fix_screeninfo finfo{};
        fb_var_screeninfo vinfo{};
        if (::ioctl(fbFd_, FBIOGET_FSCREENINFO, &finfo) < 0 ||
            ::ioctl(fbFd_, FBIOGET_VSCREENINFO, &vinfo) < 0) {
            ::close(fbFd_);
            fbFd_ = -1;
            return false;
        }

        fbWidth_  = static_cast<int>(vinfo.xres);
        fbHeight_ = static_cast<int>(vinfo.yres);
        fbBpp_    = static_cast<int>(vinfo.bits_per_pixel);
        fbStride_ = static_cast<int>(finfo.line_length);
        mapBytes_ = finfo.smem_len;

        if (fbWidth_ <= 0 || fbHeight_ <= 0 || (fbBpp_ != 16 && fbBpp_ != 32) || mapBytes_ == 0) {
            ::close(fbFd_);
            fbFd_ = -1;
            return false;
        }

        fbMem_ = static_cast<uint8_t*>(::mmap(nullptr, mapBytes_, PROT_READ | PROT_WRITE, MAP_SHARED, fbFd_, 0));
        if (fbMem_ == MAP_FAILED) {
            fbMem_ = nullptr;
            ::close(fbFd_);
            fbFd_ = -1;
            return false;
        }

        std::memset(fbMem_, 0, mapBytes_);

        // Put console stdin into non-blocking raw mode if attached to a TTY
        if (::isatty(STDIN_FILENO)) {
            if (::tcgetattr(STDIN_FILENO, &origTerm_) == 0) {
                hasRawTerm_ = true;
                termios raw = origTerm_;
                raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
                raw.c_cc[VMIN] = 0;
                raw.c_cc[VTIME] = 0;
                ::tcsetattr(STDIN_FILENO, TCSANOW, &raw);
                const int flags = ::fcntl(STDIN_FILENO, F_GETFL, 0);
                ::fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
            }
        }
        return true;
#else
        return false;
#endif
    }

    void shutdown() override {
#ifdef __linux__
        if (hasRawTerm_) {
            ::tcsetattr(STDIN_FILENO, TCSANOW, &origTerm_);
            hasRawTerm_ = false;
        }
        if (fbMem_ && mapBytes_ > 0) {
            ::munmap(fbMem_, mapBytes_);
            fbMem_ = nullptr;
        }
        if (fbFd_ >= 0) {
            ::close(fbFd_);
            fbFd_ = -1;
        }
#endif
    }

    bool pollEvents(InstrumentUi& ui) override {
#ifdef __linux__
        if (!hasRawTerm_) return true;
        uint8_t buf[8];
        while (true) {
            const ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) break;
            if (n == 1) {
                const char ch = static_cast<char>(buf[0]);
                if (ch == 27) { // Bare ESC: close help or exit
                    if (ui.helpVisible()) ui.setHelpVisible(false);
                    else return false;
                } else if (ch == '\r' || ch == '\n') {
                    ui.handleKey("Enter");
                } else if (ch == '\t') {
                    ui.handleKey("Tab");
                } else if (ch >= 32 && ch <= 126) {
                    const KeyModFlags mods = (ch >= 'A' && ch <= 'Z') ? KeyModFlags::Shift : KeyModFlags::None;
                    ui.handleKey(std::string(1, ch), mods);
                }
            } else if (n >= 2 && buf[0] == 27 && buf[1] >= '1' && buf[1] <= '4') {
                // Alt+1..4 escape sequence
                ui.handleKey(std::string(1, static_cast<char>(buf[1])), KeyModFlags::Alt);
            }
        }
#else
        (void)ui;
#endif
        return true;
    }

    void present(const Canvas& canvas) override {
#ifdef __linux__
        if (!fbMem_) return;
        const int srcW = canvas.width();
        const int srcH = canvas.height();
        const uint32_t* src = canvas.data();

        for (int dy = 0; dy < fbHeight_; ++dy) {
            const int sy = (dy * srcH) / fbHeight_;
            const uint32_t* srcRow = src + static_cast<size_t>(sy * srcW);
            uint8_t* dstLine = fbMem_ + static_cast<size_t>(dy * fbStride_);

            if (fbBpp_ == 16) {
                auto* dst16 = reinterpret_cast<uint16_t*>(dstLine);
                for (int dx = 0; dx < fbWidth_; ++dx) {
                    const int sx = (dx * srcW) / fbWidth_;
                    dst16[dx] = Canvas::argbToRgb565(srcRow[sx]);
                }
            } else if (fbBpp_ == 32) {
                auto* dst32 = reinterpret_cast<uint32_t*>(dstLine);
                for (int dx = 0; dx < fbWidth_; ++dx) {
                    const int sx = (dx * srcW) / fbWidth_;
                    dst32[dx] = srcRow[sx];
                }
            }
        }
#else
        (void)canvas;
#endif
    }

    std::string driverName() const override {
        return "fbdev(" + devPath_ + " " + std::to_string(fbBpp_) + "bpp)";
    }

private:
    std::string devPath_;
    int fbFd_ = -1;
    uint8_t* fbMem_ = nullptr;
    size_t mapBytes_ = 0;
    int fbWidth_ = 0;
    int fbHeight_ = 0;
    int fbBpp_ = 0;
    int fbStride_ = 0;
#ifdef __linux__
    termios origTerm_{};
    bool hasRawTerm_ = false;
#endif
};

} // namespace

std::unique_ptr<IDisplayBackend> createFbdevBackend(const std::string& fbDevice) {
    auto b = std::make_unique<FbdevBackend>(fbDevice);
    if (b->init()) return b;
    return nullptr;
}

} // namespace biomus::ui
