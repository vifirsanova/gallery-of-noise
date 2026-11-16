#include "ui/DisplayBackend.hpp"

#ifdef BIOMUS_HAS_SDL2
#include <SDL2/SDL.h>
#endif

#include <algorithm>
#include <cmath>
#include <string>

namespace biomus::ui {

#ifdef BIOMUS_HAS_SDL2
namespace {

class Sdl2Backend final : public IDisplayBackend {
public:
    explicit Sdl2Backend(const DisplayOptions& opts) : opts_(opts) {}
    ~Sdl2Backend() override { shutdown(); }

    bool init() override {
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // Crisp nearest-neighbor pixel art scaling
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
            return false;
        }
        sdlInited_ = true;

        const int scale = std::clamp(opts_.windowScale, 1, 4);
        const int winW = InstrumentUi::kLogicalW * scale;
        const int winH = InstrumentUi::kLogicalH * scale;
        uint32_t winFlags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE;
        if (opts_.fullscreen) {
            winFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        }

        window_ = SDL_CreateWindow(
            "VOICE BOARD // BIO MODULATION + NOISE [480x320 Embedded UI]",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            winW, winH, winFlags);
        if (!window_) {
            shutdown();
            return false;
        }

        // Try hardware renderer first, then seamless software renderer fallback
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer_) {
            renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        }
        if (!renderer_) {
            shutdown();
            return false;
        }

        SDL_RenderSetLogicalSize(renderer_, InstrumentUi::kLogicalW, InstrumentUi::kLogicalH);

        texture_ = SDL_CreateTexture(
            renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
            InstrumentUi::kLogicalW, InstrumentUi::kLogicalH);
        if (!texture_) {
            shutdown();
            return false;
        }

        const char* vidDrv = SDL_GetCurrentVideoDriver();
        driverLabel_ = std::string("SDL2(") + (vidDrv ? vidDrv : "auto") + ")";
        return true;
    }

    void shutdown() override {
        if (texture_)  { SDL_DestroyTexture(texture_);   texture_ = nullptr; }
        if (renderer_) { SDL_DestroyRenderer(renderer_); renderer_ = nullptr; }
        if (window_)   { SDL_DestroyWindow(window_);     window_ = nullptr; }
        if (sdlInited_) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
            sdlInited_ = false;
        }
    }

    bool pollEvents(InstrumentUi& ui) override {
        SDL_Event ev{};
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
                case SDL_QUIT:
                    return false;

                case SDL_KEYDOWN: {
                    const SDL_Keycode sym = ev.key.keysym.sym;
                    const uint16_t mod = ev.key.keysym.mod;

                    if (sym == SDLK_ESCAPE) {
                        if (ui.helpVisible()) {
                            ui.setHelpVisible(false);
                            break;
                        }
                        return false;
                    }

                    KeyModFlags flags = KeyModFlags::None;
                    if (mod & KMOD_SHIFT) flags = flags | KeyModFlags::Shift;
                    if (mod & KMOD_CTRL)  flags = flags | KeyModFlags::Ctrl;
                    if (mod & KMOD_ALT)   flags = flags | KeyModFlags::Alt;

                    std::string keyName;
                    if (sym >= SDLK_F1 && sym <= SDLK_F7) {
                        keyName = "F" + std::to_string(1 + (sym - SDLK_F1));
                    } else if (sym == SDLK_TAB) {
                        keyName = "Tab";
                    } else if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) {
                        keyName = "Enter";
                    } else if (sym == SDLK_UP) {
                        keyName = "ArrowUp";
                    } else if (sym == SDLK_DOWN) {
                        keyName = "ArrowDown";
                    } else if (sym == SDLK_SPACE) {
                        keyName = " ";
                    } else if (sym >= 32 && sym <= 126) {
                        char c = static_cast<char>(sym);
                        if ((mod & KMOD_SHIFT) && c >= 'a' && c <= 'z') {
                            c = static_cast<char>(c - 'a' + 'A');
                        } else if ((mod & KMOD_SHIFT) && c == '/') {
                            c = '?';
                        }
                        keyName.assign(1, c);
                    }

                    if (!keyName.empty()) {
                        ui.handleKey(keyName, flags);
                    }
                    break;
                }

                case SDL_MOUSEBUTTONDOWN:
                    if (ev.button.button == SDL_BUTTON_LEFT) {
                        mouseDown_ = true;
                        ui.handlePointerDown(ev.button.x, ev.button.y);
                    }
                    break;

                case SDL_MOUSEMOTION:
                    if (mouseDown_) {
                        ui.handlePointerMove(ev.motion.x, ev.motion.y);
                    }
                    break;

                case SDL_MOUSEBUTTONUP:
                    if (ev.button.button == SDL_BUTTON_LEFT) {
                        mouseDown_ = false;
                        ui.handlePointerUp(ev.button.x, ev.button.y);
                    }
                    break;

                case SDL_FINGERDOWN: {
                    const int lx = static_cast<int>(std::lround(ev.tfinger.x * InstrumentUi::kLogicalW));
                    const int ly = static_cast<int>(std::lround(ev.tfinger.y * InstrumentUi::kLogicalH));
                    ui.handlePointerDown(lx, ly);
                    break;
                }
                case SDL_FINGERMOTION: {
                    const int lx = static_cast<int>(std::lround(ev.tfinger.x * InstrumentUi::kLogicalW));
                    const int ly = static_cast<int>(std::lround(ev.tfinger.y * InstrumentUi::kLogicalH));
                    ui.handlePointerMove(lx, ly);
                    break;
                }
                case SDL_FINGERUP: {
                    const int lx = static_cast<int>(std::lround(ev.tfinger.x * InstrumentUi::kLogicalW));
                    const int ly = static_cast<int>(std::lround(ev.tfinger.y * InstrumentUi::kLogicalH));
                    ui.handlePointerUp(lx, ly);
                    break;
                }

                default:
                    break;
            }
        }
        return true;
    }

    void present(const Canvas& canvas) override {
        if (!renderer_ || !texture_) return;
        SDL_UpdateTexture(texture_, nullptr, canvas.data(), canvas.strideBytes());
        SDL_SetRenderDrawColor(renderer_, 10, 10, 12, 255);
        SDL_RenderClear(renderer_);
        SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
        SDL_RenderPresent(renderer_);
    }

    std::string driverName() const override { return driverLabel_; }

private:
    DisplayOptions opts_;
    bool sdlInited_ = false;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    bool mouseDown_ = false;
    std::string driverLabel_ = "SDL2";
};

} // namespace
#endif // BIOMUS_HAS_SDL2

std::unique_ptr<IDisplayBackend> createSdl2Backend(const DisplayOptions& opts) {
#ifdef BIOMUS_HAS_SDL2
    auto b = std::make_unique<Sdl2Backend>(opts);
    if (b->init()) return b;
#else
    (void)opts;
#endif
    return nullptr;
}

std::unique_ptr<IDisplayBackend> createAutoDisplayBackend(const DisplayOptions& opts) {
    if (!opts.fbDevice.empty()) {
        if (auto fb = createFbdevBackend(opts.fbDevice)) return fb;
    }
    if (auto sdl = createSdl2Backend(opts)) {
        return sdl;
    }
    if (auto fb = createFbdevBackend("/dev/fb0")) {
        return fb;
    }
    return nullptr;
}

} // namespace biomus::ui
