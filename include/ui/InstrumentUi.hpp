#pragma once

#include "core/Engine.hpp"
#include "ui/Canvas.hpp"
#include <array>
#include <cstdint>
#include <string>

namespace biomus::ui {

enum class UiPage : uint8_t {
    Perform = 0,   // Dual Voice/Drone XY vector pads + 16-step transport + Bio/Master
    VoiceChain,    // 11-block Voice chain + 4 large hardware rotary encoders + Bio Mod
    DroneMaster,   // 7-block Drone chain + 4 Master rotary encoders + Bio Mod
    NoiseSeq,      // Full 4x16 polyrhythmic step matrix + Parameter Locks + Synth strip
    BioSystem,     // 3 live scrolling Bio oscilloscopes + 2D Gaze pad + ALSA/MIDI health
    Count
};

struct SystemStatusInfo {
    std::string audioBackend = "ALSA-Duplex";
    uint32_t sampleRate = 48000;
    uint32_t periodFrames = 128;
    uint64_t xruns = 0;
    std::string midiPort = "Akai / Virtual";
    bool oledHardware = false;
    std::string displayDriver = "SDL2";
};

enum class KeyModFlags : uint8_t {
    None  = 0,
    Shift = 1 << 0,
    Ctrl  = 1 << 1,
    Alt   = 1 << 2
};

inline KeyModFlags operator|(KeyModFlags a, KeyModFlags b) noexcept {
    return static_cast<KeyModFlags>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
}
inline bool hasFlag(KeyModFlags f, KeyModFlags bit) noexcept {
    return (static_cast<uint8_t>(f) & static_cast<uint8_t>(bit)) != 0;
}

// ============================================================================
// InstrumentUi — Autonomous Hardware Screen Controller & Renderer (480x320)
// Designed for 3.5"–5" Raspberry Pi TFT/DSI screens and Arch Linux SDL2 dev.
// ============================================================================
class InstrumentUi {
public:
    static constexpr int kLogicalW = 480;
    static constexpr int kLogicalH = 320;
    static constexpr size_t kScopeLen = 200;

    explicit InstrumentUi(Engine& engine);

    void setPage(UiPage page) noexcept { page_ = page; }
    UiPage page() const noexcept { return page_; }

    void setHelpVisible(bool visible) noexcept { showHelp_ = visible; }
    bool helpVisible() const noexcept { return showHelp_; }

    // Pushes latest bio telemetry into scrolling oscilloscope history & renders 480x320 frame
    void render(Canvas& canvas, const SystemStatusInfo& sysInfo);

    // Keyboard input (ASCII char or special key token like "F1".."F7", "Tab", "ArrowUp", "ArrowDown", "Enter")
    bool handleKey(const std::string& keyName, KeyModFlags mods = KeyModFlags::None);

    // Touch / Mouse input in logical [0..480) x [0..320) coordinates
    void handlePointerDown(int lx, int ly);
    void handlePointerMove(int lx, int ly);
    void handlePointerUp(int lx, int ly);

private:
    void pushBioHistory(const BioState& bio) noexcept;

    void renderTopBar(Canvas& c, const FullState& s);
    void renderBottomMeter(Canvas& c, const FullState& s);
    void renderPagePerform(Canvas& c, const FullState& s);
    void renderPageVoice(Canvas& c, const FullState& s);
    void renderPageDroneMaster(Canvas& c, const FullState& s);
    void renderPageNoiseSeq(Canvas& c, const FullState& s);
    void renderPageBioSys(Canvas& c, const FullState& s, const SystemStatusInfo& sys);
    void renderHelpOverlay(Canvas& c);

    // Reusable hardware widget painters
    void drawEncoderCard(Canvas& c, int x, int y, int w, int h,
                         const char* title, float baseVal, float minVal, float maxVal,
                         uint16_t modTarget, const FullState& s, Color accent,
                         bool activeFrame = false, bool isFreq = false);

    void drawModStrip(Canvas& c, int x, int y, int w, uint16_t modTarget, const FullState& s);

    enum class DragKind : uint8_t {
        None = 0,
        VoicePadXY,
        DronePadXY,
        VoiceEncoder0,
        VoiceEncoder1,
        VoiceEncoder2,
        VoiceEncoder3,
        VoiceModDepth0,
        VoiceModDepth1,
        VoiceModDepth2,
        VoiceModDepth3,
        DroneEncoderX,
        DroneEncoderY,
        DroneEncoderK0,
        MasterEncoder0,
        MasterEncoder1,
        MasterEncoder2,
        MasterEncoder3,
        MasterModDepth0,
        MasterModDepth1,
        MasterModDepth2,
        MasterModDepth3,
        NoiseFader
    };

    void applyDragAt(int lx, int ly);

    Engine& engine_;
    UiPage page_ = UiPage::Perform;
    bool showHelp_ = false;

    // Active drag state for touch/mouse gestures
    DragKind dragKind_ = DragKind::None;
    int dragStartX_ = 0;
    int dragStartY_ = 0;
    int dragRectX_ = 0;
    int dragRectY_ = 0;
    int dragRectW_ = 1;
    int dragRectH_ = 1;
    float dragStartVal_ = 0.0f;
    NoiseLockField dragNoiseField_ = NoiseLockField::Cutoff;
    float dragMin_ = 0.0f;
    float dragMax_ = 1.0f;

    // Rolling oscilloscope ring buffers for Page 5 (BIO & SYS)
    std::array<float, kScopeLen> breathHist_{};
    std::array<float, kScopeLen> heartHist_{};
    std::array<float, kScopeLen> eyeXHist_{};
    std::array<float, kScopeLen> eyeYHist_{};
    size_t scopeHead_ = 0;
    bool scopeInitialized_ = false;
};

} // namespace biomus::ui
