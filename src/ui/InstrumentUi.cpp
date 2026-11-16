#include "ui/InstrumentUi.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace biomus::ui {

namespace {

inline float clampf(float v, float lo, float hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

inline bool hitRect(int px, int py, int rx, int ry, int rw, int rh) noexcept {
    return px >= rx && px < (rx + rw) && py >= ry && py < (ry + rh);
}

Color bioColor(BioSource src) noexcept {
    switch (src) {
        case BioSource::Breath: return palette::kBreath;
        case BioSource::Heart:  return palette::kHeart;
        case BioSource::EyeX:
        case BioSource::EyeY:   return palette::kEye;
        default:                return palette::kMuted;
    }
}

const char* bioShortLabel(BioSource src) noexcept {
    switch (src) {
        case BioSource::Breath: return "BRTH";
        case BioSource::Heart:  return "HART";
        case BioSource::EyeX:   return "EYEX";
        case BioSource::EyeY:   return "EYEY";
        default:                return "----";
    }
}

const char* voiceFxShort(VoiceFxId id) noexcept {
    switch (id) {
        case VoiceFxId::PAN:      return "PAN";
        case VoiceFxId::HPF:      return "HPF";
        case VoiceFxId::COMP:     return "CMP";
        case VoiceFxId::FOLD:     return "FLD";
        case VoiceFxId::BITCRUSH: return "BIT";
        case VoiceFxId::RINGMOD:  return "RNG";
        case VoiceFxId::CHORUS:   return "CHR";
        case VoiceFxId::REVERB:   return "REV";
        case VoiceFxId::SHIMMER:  return "SHM";
        case VoiceFxId::DELAY:    return "DLY";
        case VoiceFxId::LPF:      return "LPF";
        default:                  return "FX";
    }
}

const char* droneFxShort(DroneFxId id) noexcept {
    switch (id) {
        case DroneFxId::LPF:      return "LPF";
        case DroneFxId::DRIVE:    return "DRV";
        case DroneFxId::FOLD:     return "FLD";
        case DroneFxId::BITCRUSH: return "BIT";
        case DroneFxId::RINGMOD:  return "RNG";
        case DroneFxId::CHORUS:   return "CHR";
        case DroneFxId::REVERB:   return "REV";
        default:                  return "FX";
    }
}

void drawLedIndicator(Canvas& c, int x, int y, const char* lbl, bool on, Color onCol) noexcept {
    c.fillCircle(x + 5, y + 5, 4, on ? onCol : Color::rgb(28, 30, 36));
    c.drawCircle(x + 5, y + 5, 4, on ? onCol : palette::kBorder);
    c.drawText(x + 2, y + 13, lbl, on ? palette::kText : palette::kDim, 1);
}

} // namespace

InstrumentUi::InstrumentUi(Engine& engine) : engine_(engine) {
    breathHist_.fill(0.5f);
    heartHist_.fill(0.0f);
    eyeXHist_.fill(0.5f);
    eyeYHist_.fill(0.5f);
}

void InstrumentUi::pushBioHistory(const BioState& bio) noexcept {
    if (!scopeInitialized_) {
        breathHist_.fill(bio.breath);
        heartHist_.fill(bio.heart);
        eyeXHist_.fill(bio.eyeX);
        eyeYHist_.fill(bio.eyeY);
        scopeInitialized_ = true;
    }
    breathHist_[scopeHead_] = clampf(bio.breath, 0.0f, 1.0f);
    heartHist_[scopeHead_]  = clampf(bio.heart,  0.0f, 1.0f);
    eyeXHist_[scopeHead_]   = clampf(bio.eyeX,   0.0f, 1.0f);
    eyeYHist_[scopeHead_]   = clampf(bio.eyeY,   0.0f, 1.0f);
    scopeHead_ = (scopeHead_ + 1) % kScopeLen;
}

void InstrumentUi::render(Canvas& canvas, const SystemStatusInfo& sysInfo) {
    const FullState s = engine_.readSnapshot();
    pushBioHistory(s.bio);

    canvas.clear(palette::kBg);
    renderTopBar(canvas, s);

    switch (page_) {
        case UiPage::Perform:     renderPagePerform(canvas, s); break;
        case UiPage::VoiceChain:  renderPageVoice(canvas, s); break;
        case UiPage::DroneMaster: renderPageDroneMaster(canvas, s); break;
        case UiPage::NoiseSeq:    renderPageNoiseSeq(canvas, s); break;
        case UiPage::BioSystem:   renderPageBioSys(canvas, s, sysInfo); break;
        default:                  renderPagePerform(canvas, s); break;
    }

    renderBottomMeter(canvas, s);

    if (showHelp_) {
        renderHelpOverlay(canvas);
    }
}

// ============================================================================
// Persistent Top Hardware Header (y = 0..32)
// ============================================================================
void InstrumentUi::renderTopBar(Canvas& c, const FullState& s) {
    // 1. Preset Selector Box (x = 4..106, y = 3..29)
    c.fillRect(4, 3, 102, 26, palette::kPanel);
    c.drawRect(4, 3, 102, 26, palette::kBorder);
    c.fillCircle(12, 16, 3, palette::kAmber);
    c.drawText(20, 9, toString(s.preset), palette::kAmber, 2);

    // 2. Five Hardware Page Tabs (x = 110..349, 5 * 47px = 235px)
    constexpr const char* kTabLabels[5] = {"1:PERF", "2:VOX", "3:DRN", "4:SEQ", "5:BIO"};
    constexpr Color kTabColors[5] = {
        palette::kAmber, palette::kViolet, palette::kCyan, palette::kNoise, palette::kBreath
    };
    for (int i = 0; i < 5; ++i) {
        const int tx = 110 + i * 48;
        const bool act = (static_cast<int>(page_) == i);
        c.fillRect(tx, 3, 46, 26, act ? Color::rgb(26, 28, 36) : palette::kPanelAlt);
        c.drawRect(tx, 3, 46, 26, act ? kTabColors[i] : palette::kBorder);
        if (act) {
            c.fillRect(tx + 1, 3, 44, 2, kTabColors[i]);
        }
        c.drawTextCentered(tx, 13, 46, kTabLabels[i], act ? kTabColors[i] : palette::kDim, 1);
    }

    // 3. Hardware Status LEDs + [?] Help Button (x = 352..476)
    c.fillRect(352, 3, 102, 26, palette::kPanel);
    c.drawRect(352, 3, 102, 26, palette::kBorder);
    drawLedIndicator(c, 357, 6, "M", s.mic,     palette::kGreen);
    drawLedIndicator(c, 376, 6, "D", s.droneOn, palette::kCyan);
    drawLedIndicator(c, 395, 6, "F", s.fileOn,  palette::kViolet);
    drawLedIndicator(c, 414, 6, "B", s.bypass,  palette::kAmber);
    drawLedIndicator(c, 433, 6, "!", s.clip,    palette::kRed);

    c.fillRect(456, 3, 20, 26, showHelp_ ? palette::kCyan : palette::kPanel);
    c.drawRect(456, 3, 20, 26, showHelp_ ? palette::kCyan : palette::kBorder);
    c.drawTextCentered(456, 12, 20, "?", showHelp_ ? palette::kBg : palette::kText, 1);
}

// ============================================================================
// Persistent Bottom Master Meter & Mode Strip (y = 294..317)
// ============================================================================
void InstrumentUi::renderBottomMeter(Canvas& c, const FullState& s) {
    c.fillRect(4, 294, 472, 23, palette::kPanel);
    c.drawRect(4, 294, 472, 23, palette::kBorder);

    char info[48];
    std::snprintf(info, sizeof(info), "KNOB:%s  BIO:%s",
                  toString(s.knobMode), bioShortLabel(s.activeBio));
    c.drawText(10, 302, info, palette::kDim, 1);

    // 12-Segment Stereo Hardware VU Meter (x = 180..468)
    c.drawText(152, 302, "OUT", palette::kAmber, 1);
    const int lit = static_cast<int>(std::lround(clampf(s.masterLevel, 0.0f, 1.0f) * 12.0f));
    for (int i = 0; i < 12; ++i) {
        const int sx = 178 + i * 24;
        const bool isOn = (i < lit);
        Color baseC = (i < 8) ? Color::rgb(20, 46, 24)
                    : (i < 11) ? Color::rgb(48, 38, 16)
                               : Color::rgb(52, 20, 20);
        Color brightC = (i < 8) ? palette::kGreen
                      : (i < 11) ? palette::kAmber
                                 : palette::kRed;
        c.fillRect(sx, 299, 21, 13, isOn ? brightC : baseC);
        c.drawRect(sx, 299, 21, 13, isOn ? brightC : palette::kBorder);
    }
}

// ============================================================================
// Reusable Modulation Badge + Depth Bar & Hardware Rotary Encoder Card
// ============================================================================
void InstrumentUi::drawModStrip(Canvas& c, int x, int y, int w,
                                uint16_t modTarget, const FullState& s) {
    const ModEntry me = s.modulation.get(modTarget);
    const Color mc = bioColor(me.source);
    const bool active = (me.source != BioSource::None);

    // Source pill (width 36)
    c.fillRect(x, y, 36, 13, Color::rgb(12, 13, 16));
    c.drawRect(x, y, 36, 13, active ? mc : palette::kBorder);
    c.drawTextCentered(x, y + 3, 36, bioShortLabel(me.source), active ? mc : palette::kMuted, 1);

    // Depth bar
    const int bx = x + 39;
    const int bw = std::max(12, w - 39);
    c.fillRect(bx, y + 3, bw, 7, Color::rgb(10, 11, 14));
    c.drawRect(bx, y + 3, bw, 7, palette::kBorder);
    if (active && me.depth > 0.0f) {
        const int fw = static_cast<int>(std::lround(clampf(me.depth, 0.0f, 1.0f) * static_cast<float>(bw - 2)));
        c.fillRect(bx + 1, y + 4, fw, 5, mc);
    }
}

void InstrumentUi::drawEncoderCard(Canvas& c, int x, int y, int w, int h,
                                   const char* title, float baseVal, float minVal, float maxVal,
                                   uint16_t modTarget, const FullState& s, Color accent,
                                   bool activeFrame, bool isFreq) {
    c.fillRect(x, y, w, h, palette::kPanelAlt);
    c.drawRect(x, y, w, h, activeFrame ? accent : palette::kBorder);
    if (activeFrame) {
        c.fillRect(x + 1, y + 1, w - 2, 2, accent);
    }

    c.drawTextCentered(x, y + 6, w, title, activeFrame ? accent : palette::kDim, 1);

    const float span = std::max(1e-6f, maxVal - minVal);
    const float normBase = clampf((baseVal - minVal) / span, 0.0f, 1.0f);
    const ModEntry me = s.modulation.get(modTarget);
    const float liveVal = effectiveValue(baseVal, minVal, maxVal, me.source, me.depth, s.bio);
    const float normLive = clampf((liveVal - minVal) / span, 0.0f, 1.0f);

    const int cx = x + w / 2;
    const int radius = std::min(w / 2 - 14, (h - 58) / 2);
    const int cy = y + 20 + radius + 4;

    if (radius >= 12) {
        // Track background (-135 .. +135 deg)
        c.drawRotaryArc(cx, cy, radius, 4, -135.0f, 135.0f, Color::rgb(30, 33, 42));
        // Base value arc
        const float endDeg = -135.0f + 270.0f * normBase;
        if (normBase > 0.005f) {
            c.drawRotaryArc(cx, cy, radius, 4, -135.0f, endDeg, accent);
        }
        // Live bio-modulated excursion arc & bright indicator dot
        if (me.source != BioSource::None && me.depth > 0.001f) {
            const float liveDeg = -135.0f + 270.0f * normLive;
            c.drawRotaryArc(cx, cy, radius - 5, 2, std::min(endDeg, liveDeg),
                            std::max(endDeg, liveDeg), bioColor(me.source));
        }
        // Pointer line from center
        const float ptrRad = ((-135.0f + 270.0f * normLive) - 90.0f) * (3.14159265f / 180.0f);
        const int px = cx + static_cast<int>(std::lround(std::cos(ptrRad) * static_cast<float>(radius - 3)));
        const int py = cy + static_cast<int>(std::lround(std::sin(ptrRad) * static_cast<float>(radius - 3)));
        c.drawLine(cx, cy, px, py, palette::kText);
        c.fillCircle(cx, cy, 3, accent);
    }

    char vbuf[24];
    if (isFreq || maxVal >= 500.0f) {
        std::snprintf(vbuf, sizeof(vbuf), "%d", static_cast<int>(std::lround(liveVal)));
    } else if (maxVal - minVal < 0.25f) {
        std::snprintf(vbuf, sizeof(vbuf), "%.3f", static_cast<double>(liveVal));
    } else {
        std::snprintf(vbuf, sizeof(vbuf), "%.2f", static_cast<double>(liveVal));
    }
    c.drawTextCentered(x, y + h - 33, w, vbuf, palette::kText, 1);

    if (modTarget != kInvalidModTarget) {
        drawModStrip(c, x + 5, y + h - 18, w - 10, modTarget, s);
    }
}

// ============================================================================
// Page 1: PERFORM (Stage XY Vector Pads + 16-Step Ribbon + Quick Bio/Master)
// ============================================================================
void InstrumentUi::renderPagePerform(Canvas& c, const FullState& s) {
    auto drawXyCard = [&](int x, int y, int w, int h, bool isVoice) {
        const Color col = isVoice ? palette::kViolet : palette::kCyan;
        c.drawPanel(x, y, w, h, col);

        const char* chainTitle = isVoice ? "VOICE" : "DRONE";
        const char* fxName = isVoice ? toString(s.voice.active) : toString(s.drone.active);
        const bool fxOn = isVoice ? s.voice.enabled[static_cast<size_t>(s.voice.active)]
                                  : s.drone.enabled[static_cast<size_t>(s.drone.active)];

        c.drawText(x + 8, y + 7, chainTitle, col, 1);
        c.drawText(x + 48, y + 7, fxName, palette::kText, 1);

        c.fillRect(x + w - 34, y + 5, 28, 12, fxOn ? Color::rgb(18, 42, 22) : palette::kPanelAlt);
        c.drawRect(x + w - 34, y + 5, 28, 12, fxOn ? palette::kGreen : palette::kBorder);
        c.drawTextCentered(x + w - 34, y + 8, 28, fxOn ? "ON" : "OFF",
                           fxOn ? palette::kGreen : palette::kDim, 1);

        // 2D XY Vector Pad
        const int px = x + 8;
        const int py = y + 21;
        const int pw = 132;
        const int ph = 132;
        c.fillRect(px, py, pw, ph, Color::rgb(6, 7, 10));
        c.drawRect(px, py, pw, ph, palette::kBorder);
        // Crosshairs & quarter grid
        c.drawHLine(px + 1, py + ph / 2, pw - 2, palette::kGrid);
        c.drawVLine(px + pw / 2, py + 1, ph - 2, palette::kGrid);

        XY baseXY = isVoice ? s.voice.xy[static_cast<size_t>(s.voice.active)]
                            : s.drone.xy[static_cast<size_t>(s.drone.active)];
        const uint16_t mxIdx = isVoice ? voiceXyModIndex(s.voice.active, 0)
                                       : droneXyModIndex(s.drone.active, 0);
        const uint16_t myIdx = isVoice ? voiceXyModIndex(s.voice.active, 1)
                                       : droneXyModIndex(s.drone.active, 1);
        const ModEntry mx = s.modulation.get(mxIdx);
        const ModEntry my = s.modulation.get(myIdx);

        const float liveX = effectiveValue(baseXY.x, 0.0f, 1.0f, mx.source, mx.depth, s.bio);
        const float liveY = effectiveValue(baseXY.y, 0.0f, 1.0f, my.source, my.depth, s.bio);

        const int bx = px + 4 + static_cast<int>(std::lround(baseXY.x * static_cast<float>(pw - 9)));
        const int by = py + ph - 5 - static_cast<int>(std::lround(baseXY.y * static_cast<float>(ph - 9)));
        const int lx = px + 4 + static_cast<int>(std::lround(liveX * static_cast<float>(pw - 9)));
        const int ly = py + ph - 5 - static_cast<int>(std::lround(liveY * static_cast<float>(ph - 9)));

        if (bx != lx || by != ly) {
            c.drawLine(bx, by, lx, ly, col.dimmed(0.7f));
            c.drawCircle(bx, by, 5, col.dimmed(0.65f));
        }
        c.drawHLine(px + 1, ly, pw - 2, col.withAlpha(42));
        c.drawVLine(lx, py + 1, ph - 2, col.withAlpha(42));
        c.fillCircle(lx, ly, 4, col);
        c.drawCircle(lx, ly, 6, palette::kText);

        // Right-side readouts inside XY card
        const int rx = px + pw + 6;
        char buf[16];
        c.drawText(rx, py + 4, "X", palette::kDim, 1);
        std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(liveX));
        c.drawText(rx, py + 15, buf, col, 1);
        c.drawText(rx, py + 27, bioShortLabel(mx.source), bioColor(mx.source), 1);

        c.drawText(rx, py + 52, "Y", palette::kDim, 1);
        std::snprintf(buf, sizeof(buf), "%.2f", static_cast<double>(liveY));
        c.drawText(rx, py + 63, buf, col, 1);
        c.drawText(rx, py + 75, bioShortLabel(my.source), bioColor(my.source), 1);

        // Cycle prev/next buttons [<] [>]
        c.fillRect(rx, py + 104, 21, 26, palette::kPanelAlt);
        c.drawRect(rx, py + 104, 21, 26, palette::kBorder);
        c.drawTextCentered(rx, py + 113, 21, "<", palette::kText, 1);

        c.fillRect(rx + 24, py + 104, 21, 26, palette::kPanelAlt);
        c.drawRect(rx + 24, py + 104, 21, 26, palette::kBorder);
        c.drawTextCentered(rx + 24, py + 113, 21, ">", palette::kText, 1);

        // Mini FX slot pills along bottom of card
        const int count = isVoice ? static_cast<int>(kNumVoiceFx) : static_cast<int>(kNumDroneFx);
        const int pillW = (w - 14) / count;
        for (int i = 0; i < count; ++i) {
            const int sx = x + 7 + i * pillW;
            bool isAct = false, isEn = false;
            if (isVoice) {
                const VoiceFxId fid = s.voice.order[static_cast<size_t>(i)];
                isAct = (fid == s.voice.active);
                isEn = s.voice.enabled[static_cast<size_t>(fid)];
            } else {
                const DroneFxId fid = s.drone.order[static_cast<size_t>(i)];
                isAct = (fid == s.drone.active);
                isEn = s.drone.enabled[static_cast<size_t>(fid)];
            }
            c.fillRect(sx, y + h - 18, pillW - 2, 11,
                       isAct ? col : (isEn ? Color::rgb(34, 38, 48) : Color::rgb(14, 15, 18)));
        }
    };

    drawXyCard(4, 34, 196, 178, true);
    drawXyCard(204, 34, 196, 178, false);

    // Far-right Bio + Master Quick Column (x = 404..476, y = 34..212)
    c.drawPanel(404, 34, 72, 178, palette::kAmber);
    c.drawTextCentered(404, 41, 72, "BIO/MST", palette::kAmber, 1);

    auto drawMiniHBar = [&](int py, const char* lbl, float norm, Color col) {
        c.drawText(410, py, lbl, palette::kDim, 1);
        c.fillRect(436, py, 34, 7, palette::kPanelAlt);
        c.drawRect(436, py, 34, 7, palette::kBorder);
        const int fw = static_cast<int>(std::lround(clampf(norm, 0.0f, 1.0f) * 32.0f));
        if (fw > 0) c.fillRect(437, py + 1, fw, 5, col);
    };
    drawMiniHBar(56,  "BR",  s.bio.breath, palette::kBreath);
    drawMiniHBar(70,  "HT",  s.bio.heart,  palette::kHeart);
    drawMiniHBar(84,  "EX",  s.bio.eyeX,   palette::kEye);
    drawMiniHBar(98,  "EY",  s.bio.eyeY,   palette::kEye);

    c.drawHLine(409, 112, 62, palette::kBorder);
    drawMiniHBar(120, "VO",  s.master.voiceOut / 3.0f, palette::kAmber);
    drawMiniHBar(136, "DR",  s.master.droneVol / 1.5f, palette::kAmber);
    drawMiniHBar(152, "FB",  s.master.fb / 0.85f,      palette::kAmber);
    drawMiniHBar(168, "LP",  (s.master.lpf - 500.0f) / 17500.0f, palette::kAmber);

    char bpmBuf[16];
    std::snprintf(bpmBuf, sizeof(bpmBuf), "%d BPM", s.bio.bpm);
    c.drawTextCentered(404, 192, 72, bpmBuf, palette::kHeart, 1);

    // Lower 16-Step Noise Sequencer Ribbon (x = 4..476, y = 216..290)
    c.drawPanel(4, 216, 472, 74, palette::kNoise);
    const int trIdx = std::clamp(s.noise.activeTrack, 0, 3);
    const NoiseTrack& tr = s.noise.tracks[static_cast<size_t>(trIdx)];
    char nHead[64];
    std::snprintf(nHead, sizeof(nHead), "NOISE SEQ  T%d:%s  %d BPM",
                  trIdx + 1, toString(tr.voice.type), static_cast<int>(std::lround(s.noise.tempo)));
    c.drawText(12, 223, nHead, palette::kNoise, 1);

    // Play/Stop button (x = 398..468, y = 220..236)
    c.fillRect(398, 220, 70, 16, s.noise.playing ? Color::rgb(24, 52, 24) : palette::kPanelAlt);
    c.drawRect(398, 220, 70, 16, s.noise.playing ? palette::kNoise : palette::kBorder);
    c.drawTextCentered(398, 224, 70, s.noise.playing ? "STOP [SPC]" : "PLAY [SPC]",
                       s.noise.playing ? palette::kNoise : palette::kText, 1);

    // 16 Tactile Step Pads (x = 10..466, y = 242..282)
    for (int i = 0; i < 16; ++i) {
        const int sx = 10 + i * 29;
        const Step& st = tr.steps[static_cast<size_t>(i)];
        const bool isCur = s.noise.playing && (tr.currentStep == i);
        const bool isSel = (s.noise.selectedStep == i);

        c.fillRect(sx, 242, 26, 40, st.trigger ? Color::rgb(28, 48, 28) : palette::kPanelAlt);
        c.drawRect(sx, 242, 26, 40,
                   isCur ? palette::kAmber : (isSel ? palette::kText : (st.trigger ? palette::kNoise : palette::kBorder)));

        char snum[4];
        std::snprintf(snum, sizeof(snum), "%d", i + 1);
        c.drawText(sx + 3, 246, snum, isCur ? palette::kAmber : palette::kDim, 1);

        if (st.trigger) {
            c.fillCircle(sx + 13, 266, 4, palette::kNoise);
        }
        if (st.locks.mask != 0) {
            c.fillRect(sx + 20, 275, 4, 4, palette::kAmber);
        }
    }
}

// ============================================================================
// Page 2: VOICE FX (11-Slot Reorderable Chain + 4 Hardware Rotary Encoders)
// ============================================================================
void InstrumentUi::renderPageVoice(Canvas& c, const FullState& s) {
    c.drawPanel(4, 34, 472, 54, palette::kViolet);

    const VoiceFxId actFx = s.voice.active;
    const bool actOn = s.voice.enabled[static_cast<size_t>(actFx)];
    char head[64];
    std::snprintf(head, sizeof(head), "VOICE CHAIN :: %s", toString(actFx));
    c.drawText(12, 41, head, palette::kViolet, 1);

    // Move [<] [>] and [ON/OFF] buttons
    c.fillRect(306, 38, 48, 15, palette::kPanelAlt);
    c.drawRect(306, 38, 48, 15, palette::kBorder);
    c.drawTextCentered(306, 42, 48, "<< MOV", palette::kText, 1);

    c.fillRect(358, 38, 48, 15, palette::kPanelAlt);
    c.drawRect(358, 38, 48, 15, palette::kBorder);
    c.drawTextCentered(358, 42, 48, "MOV >>", palette::kText, 1);

    c.fillRect(412, 38, 56, 15, actOn ? Color::rgb(20, 48, 24) : palette::kPanelAlt);
    c.drawRect(412, 38, 56, 15, actOn ? palette::kGreen : palette::kBorder);
    c.drawTextCentered(412, 42, 56, actOn ? "FX: ON" : "FX: OFF", actOn ? palette::kGreen : palette::kDim, 1);

    // 11 FX Blocks (x = 10..470, each 41px wide)
    for (size_t i = 0; i < kNumVoiceFx; ++i) {
        const VoiceFxId fid = s.voice.order[i];
        const bool isAct = (fid == actFx);
        const bool isEn = s.voice.enabled[static_cast<size_t>(fid)];
        const int bx = 9 + static_cast<int>(i) * 42;
        c.fillRect(bx, 58, 40, 24, isAct ? palette::kViolet : palette::kPanelAlt);
        c.drawRect(bx, 58, 40, 24, isAct ? palette::kText : (isEn ? palette::kViolet.dimmed(0.55f) : palette::kBorder));
        c.drawTextCentered(bx, 66, 40, voiceFxShort(fid),
                           isAct ? palette::kBg : (isEn ? palette::kText : palette::kMuted), 1);
    }

    // 4 Large Hardware Rotary Encoders (y = 92..290)
    const XY xy = s.voice.xy[static_cast<size_t>(actFx)];
    const FxDef& def = getVoiceFxDef(actFx);

    drawEncoderCard(c, 4,   92, 115, 198, "AXIS X [Q/W]", xy.x, 0.0f, 1.0f,
                    voiceXyModIndex(actFx, 0), s, palette::kViolet, s.knobMode == KnobMode::XY);
    drawEncoderCard(c, 123, 92, 115, 198, "AXIS Y [A/S]", xy.y, 0.0f, 1.0f,
                    voiceXyModIndex(actFx, 1), s, palette::kViolet, s.knobMode == KnobMode::XY);

    if (def.numKnobs > 0) {
        const auto& k0 = def.knobs[0];
        drawEncoderCard(c, 242, 92, 115, 198, k0.label,
                        s.voice.knobs[static_cast<size_t>(actFx)][0], k0.minVal, k0.maxVal,
                        voiceKnobModIndex(actFx, 0), s, palette::kCyan, s.knobMode == KnobMode::PARAM);
    } else {
        c.drawPanel(242, 92, 115, 198, palette::kBorder, palette::kPanelAlt);
        c.drawTextCentered(242, 184, 115, "NO PARAM 1", palette::kMuted, 1);
    }

    if (def.numKnobs > 1) {
        const auto& k1 = def.knobs[1];
        drawEncoderCard(c, 361, 92, 115, 198, k1.label,
                        s.voice.knobs[static_cast<size_t>(actFx)][1], k1.minVal, k1.maxVal,
                        voiceKnobModIndex(actFx, 1), s, palette::kCyan, s.knobMode == KnobMode::PARAM);
    } else {
        c.drawPanel(361, 92, 115, 198, palette::kBorder, palette::kPanelAlt);
        c.drawTextCentered(361, 184, 115, "NO PARAM 2", palette::kMuted, 1);
    }
}

// ============================================================================
// Page 3: DRONE + MASTER (Drone 7-Slot Chain + 4 Master Encoders)
// ============================================================================
void InstrumentUi::renderPageDroneMaster(Canvas& c, const FullState& s) {
    // Top Half: Drone Chain + Encoders (y = 34..158)
    c.drawPanel(4, 34, 472, 124, palette::kCyan);
    const DroneFxId dFx = s.drone.active;
    const bool dOn = s.drone.enabled[static_cast<size_t>(dFx)];

    char dHead[48];
    std::snprintf(dHead, sizeof(dHead), "DRONE 55Hz :: %s", toString(dFx));
    c.drawText(12, 41, dHead, palette::kCyan, 1);

    c.fillRect(184, 38, 48, 15, dOn ? Color::rgb(18, 46, 24) : palette::kPanelAlt);
    c.drawRect(184, 38, 48, 15, dOn ? palette::kGreen : palette::kBorder);
    c.drawTextCentered(184, 42, 48, dOn ? "FX:ON" : "FX:OFF", dOn ? palette::kGreen : palette::kDim, 1);

    // 7 Drone FX blocks in 2 rows on left (x = 10..234, y = 58..150)
    for (size_t i = 0; i < kNumDroneFx; ++i) {
        const DroneFxId fid = s.drone.order[i];
        const bool isAct = (fid == dFx);
        const bool isEn = s.drone.enabled[static_cast<size_t>(fid)];
        const int col = static_cast<int>(i % 4);
        const int row = static_cast<int>(i / 4);
        const int bx = 10 + col * 56;
        const int by = 60 + row * 34;
        c.fillRect(bx, by, 52, 28, isAct ? palette::kCyan : palette::kPanelAlt);
        c.drawRect(bx, by, 52, 28, isAct ? palette::kText : (isEn ? palette::kCyan.dimmed(0.55f) : palette::kBorder));
        c.drawTextCentered(bx, by + 10, 52, droneFxShort(fid),
                           isAct ? palette::kBg : (isEn ? palette::kText : palette::kMuted), 1);
    }

    // Drone X & Y compact encoders on right (x = 242..470)
    const XY dxy = s.drone.xy[static_cast<size_t>(dFx)];
    drawEncoderCard(c, 242, 40, 112, 114, "DRN X [Z/X]", dxy.x, 0.0f, 1.0f,
                    droneXyModIndex(dFx, 0), s, palette::kCyan, true);
    drawEncoderCard(c, 358, 40, 112, 114, "DRN Y [C/V]", dxy.y, 0.0f, 1.0f,
                    droneXyModIndex(dFx, 1), s, palette::kCyan, true);

    // Bottom Half: 4 Master Encoders (y = 162..290)
    c.drawPanel(4, 162, 472, 128, palette::kAmber);
    c.drawText(12, 168, "MASTER BUS & FEEDBACK MATRIX [R/T SELECT, F/G ADJUST]", palette::kAmber, 1);

    const float mVals[4] = {s.master.voiceOut, s.master.droneVol, s.master.fb, s.master.lpf};
    const char* mTitles[4] = {"VOICE OUT", "DRONE VOL", "FEEDBACK", "MASTER LPF"};
    for (int i = 0; i < 4; ++i) {
        const auto mp = static_cast<MasterParamId>(i);
        const KnobDef& kd = getMasterParamDef(mp);
        drawEncoderCard(c, 9 + i * 116, 180, 112, 106,
                        mTitles[i], mVals[i], kd.minVal, kd.maxVal,
                        masterModIndex(mp), s, palette::kAmber,
                        s.master.selectedParam == i, i == 3);
    }
}

// ============================================================================
// Page 4: NOISE SEQ (4x16 Polyrhythmic Matrix + Synth Voice + Parameter Locks)
// ============================================================================
void InstrumentUi::renderPageNoiseSeq(Canvas& c, const FullState& s) {
    c.drawPanel(4, 34, 472, 132, palette::kNoise);

    const int actTr = std::clamp(s.noise.activeTrack, 0, 3);
    const int selSt = std::clamp(s.noise.selectedStep, 0, 15);
    const int locksCnt = s.noise.tracks[static_cast<size_t>(actTr)].steps[static_cast<size_t>(selSt)].locks.count();

    char head[80];
    std::snprintf(head, sizeof(head), "4x16 NOISE MATRIX   STEP:%d/16   LOCKS:%d", selSt + 1, locksCnt);
    c.drawText(12, 41, head, palette::kNoise, 1);

    // Clear Locks button (x = 312..388) + Play/Stop button (x = 394..468)
    c.fillRect(312, 38, 76, 15, palette::kPanelAlt);
    c.drawRect(312, 38, 76, 15, locksCnt > 0 ? palette::kAmber : palette::kBorder);
    c.drawTextCentered(312, 42, 76, "CLR LOCKS", locksCnt > 0 ? palette::kAmber : palette::kDim, 1);

    c.fillRect(394, 38, 74, 15, s.noise.playing ? Color::rgb(24, 52, 24) : palette::kPanelAlt);
    c.drawRect(394, 38, 74, 15, s.noise.playing ? palette::kNoise : palette::kBorder);
    c.drawTextCentered(394, 42, 74, s.noise.playing ? "STOP [SPC]" : "PLAY [SPC]",
                       s.noise.playing ? palette::kNoise : palette::kText, 1);

    // 4 Tracks x 16 Steps Matrix (y = 58..160)
    for (int t = 0; t < 4; ++t) {
        const int ry = 58 + t * 25;
        const bool isTrAct = (t == actTr);
        c.fillRect(10, ry, 42, 22, isTrAct ? Color::rgb(24, 44, 24) : palette::kPanelAlt);
        c.drawRect(10, ry, 42, 22, isTrAct ? palette::kNoise : palette::kBorder);
        char tlbl[12];
        std::snprintf(tlbl, sizeof(tlbl), "TRK %d", t + 1);
        c.drawTextCentered(10, ry + 7, 42, tlbl, isTrAct ? palette::kNoise : palette::kDim, 1);

        const NoiseTrack& tr = s.noise.tracks[static_cast<size_t>(t)];
        for (int st = 0; st < 16; ++st) {
            const int sx = 56 + st * 26;
            const Step& step = tr.steps[static_cast<size_t>(st)];
            const bool isPlayhead = s.noise.playing && (tr.currentStep == st);
            const bool isSelected = isTrAct && (selSt == st);

            c.fillRect(sx, ry, 24, 22, step.trigger ? Color::rgb(30, 56, 30) : palette::kPanelAlt);
            c.drawRect(sx, ry, 24, 22,
                       isPlayhead ? palette::kAmber
                                  : (isSelected ? palette::kText
                                                : (step.trigger ? palette::kNoise : palette::kBorder)));
            if (step.trigger) {
                c.fillCircle(sx + 12, ry + 11, 3, palette::kNoise);
            }
            if (step.locks.mask != 0) {
                c.fillRect(sx + 18, ry + 16, 4, 4, palette::kAmber);
            }
        }
    }

    // Bottom Synth Voice Parameters for Active Track (y = 170..290)
    c.drawPanel(4, 170, 472, 120, palette::kNoise);
    const NoiseVoiceParams& v = s.noise.tracks[static_cast<size_t>(actTr)].voice;

    auto drawParamBar = [&](int px, int py, int pw, const char* lbl, float val, float lo, float hi,
                            const char* customStr = nullptr) {
        c.fillRect(px, py, pw, 22, palette::kPanelAlt);
        c.drawRect(px, py, pw, 22, palette::kBorder);
        c.drawText(px + 4, py + 3, lbl, palette::kDim, 1);

        char vstr[16];
        if (customStr) {
            std::snprintf(vstr, sizeof(vstr), "%s", customStr);
        } else if (hi >= 500.0f) {
            std::snprintf(vstr, sizeof(vstr), "%d", static_cast<int>(std::lround(val)));
        } else {
            std::snprintf(vstr, sizeof(vstr), "%.2f", static_cast<double>(val));
        }
        c.drawTextRight(px + pw - 4, py + 3, vstr, palette::kNoise, 1);

        if (!customStr) {
            const float norm = clampf((val - lo) / std::max(1e-6f, hi - lo), 0.0f, 1.0f);
            c.fillRect(px + 4, py + 14, pw - 8, 5, Color::rgb(16, 20, 16));
            const int fw = static_cast<int>(std::lround(norm * static_cast<float>(pw - 8)));
            if (fw > 0) c.fillRect(px + 4, py + 14, fw, 5, palette::kNoise);
        }
    };

    // 5 Columns across x = 10..470 (each 90px wide)
    c.drawText(10,  176, "SOURCE",   palette::kNoise, 1);
    c.drawText(103, 176, "ENVELOPE", palette::kNoise, 1);
    c.drawText(196, 176, "LFO 1",    palette::kNoise, 1);
    c.drawText(289, 176, "LFO 2",    palette::kNoise, 1);
    c.drawText(382, 176, "OUTPUT",   palette::kNoise, 1);

    // Col 0: Source
    drawParamBar(10, 188, 88, "TYPE", 0, 0, 1, toString(v.type));
    drawParamBar(10, 214, 88, "CUT",  v.cutoff, 20.0f, 12000.0f);
    drawParamBar(10, 240, 88, "RES",  v.resonance, 0.0f, 0.95f);

    // Col 1: Envelope
    drawParamBar(103, 188, 88, "ATK",  v.ampAttack, 0.001f, 1.0f);
    drawParamBar(103, 214, 88, "DEC",  v.ampDecay, 0.01f, 2.0f);
    drawParamBar(103, 240, 88, "MENV", v.modEnvAmount, 0.0f, 1.0f);

    // Col 2: LFO 1
    drawParamBar(196, 188, 88, "RATE", v.lfo1.rate, 0.05f, 20.0f);
    drawParamBar(196, 214, 88, "DPTH", v.lfo1.depth, 0.0f, 1.0f);
    drawParamBar(196, 240, 88, "TGT",  0, 0, 1, toString(v.lfo1.target));

    // Col 3: LFO 2
    drawParamBar(289, 188, 88, "RATE", v.lfo2.rate, 0.05f, 20.0f);
    drawParamBar(289, 214, 88, "DPTH", v.lfo2.depth, 0.0f, 1.0f);
    drawParamBar(289, 240, 88, "TGT",  0, 0, 1, toString(v.lfo2.target));

    // Col 4: Output + Tempo
    drawParamBar(382, 188, 88, "PAN",  v.pan, -1.0f, 1.0f);
    drawParamBar(382, 214, 88, "GAIN", v.gain, 0.0f, 1.0f);
    drawParamBar(382, 240, 88, "BPM",  s.noise.tempo, 40.0f, 240.0f);

    c.drawText(10, 272, "DRAG ANY PARAM TO EDIT TRACK VOICE + LOCK SELECTED STEP", palette::kDim, 1);
}

// ============================================================================
// Page 5: BIO & SYS (3 Scrolling Oscilloscopes + 2D Gaze Pad + ALSA/MIDI Health)
// ============================================================================
void InstrumentUi::renderPageBioSys(Canvas& c, const FullState& s, const SystemStatusInfo& sys) {
    auto drawScopeBox = [&](int x, int y, int w, int h, const char* lbl, float curVal,
                            const std::array<float, kScopeLen>& hist, Color col, const char* extraRight = nullptr) {
        c.drawPanel(x, y, w, h, col);
        c.drawText(x + 8, y + 6, lbl, col, 1);

        char vbuf[32];
        if (extraRight) std::snprintf(vbuf, sizeof(vbuf), "%s", extraRight);
        else            std::snprintf(vbuf, sizeof(vbuf), "%.2f", static_cast<double>(curVal));
        c.drawTextRight(x + w - 8, y + 6, vbuf, palette::kText, 1);

        const int sx = x + 6;
        const int sy = y + 18;
        const int sw = w - 12;
        const int sh = h - 24;
        c.fillRect(sx, sy, sw, sh, Color::rgb(6, 7, 10));
        c.drawRect(sx, sy, sw, sh, palette::kBorder);
        c.drawHLine(sx + 1, sy + sh / 2, sw - 2, palette::kGrid);

        int prevX = sx + 1;
        int prevY = sy + sh / 2;
        for (size_t i = 0; i < kScopeLen; ++i) {
            const size_t idx = (scopeHead_ + i) % kScopeLen;
            const float v = clampf(hist[idx], 0.0f, 1.0f);
            const int px = sx + 1 + static_cast<int>((i * static_cast<size_t>(sw - 3)) / (kScopeLen - 1));
            const int py = sy + sh - 2 - static_cast<int>(std::lround(v * static_cast<float>(sh - 4)));
            if (i > 0) {
                c.drawLine(prevX, prevY, px, py, col);
            }
            prevX = px;
            prevY = py;
        }
    };

    drawScopeBox(4, 34, 316, 82, "BREATH SENSOR [ALT+1]", s.bio.breath, breathHist_, palette::kBreath);

    char bpmStr[24];
    std::snprintf(bpmStr, sizeof(bpmStr), "%d BPM (%.2f)", s.bio.bpm, static_cast<double>(s.bio.heart));
    drawScopeBox(4, 120, 316, 82, "HEART PULSE [ALT+2]", s.bio.heart, heartHist_, palette::kHeart, bpmStr);

    drawScopeBox(4, 206, 316, 84, "EYE GAZE X [ALT+3]", s.bio.eyeX, eyeXHist_, palette::kEye);

    // Top-right: 2D Pupil Gaze Target Pad (x = 324..476, y = 34..156)
    c.drawPanel(324, 34, 152, 122, palette::kEye);
    c.drawText(332, 40, "EYE 2D TRACKER", palette::kEye, 1);
    const int gx = 332, gy = 54, gw = 136, gh = 94;
    c.fillRect(gx, gy, gw, gh, Color::rgb(6, 7, 10));
    c.drawRect(gx, gy, gw, gh, palette::kBorder);
    c.drawHLine(gx + 1, gy + gh / 2, gw - 2, palette::kGrid);
    c.drawVLine(gx + gw / 2, gy + 1, gh - 2, palette::kGrid);
    const int ex = gx + 4 + static_cast<int>(std::lround(s.bio.eyeX * static_cast<float>(gw - 9)));
    const int ey = gy + gh - 5 - static_cast<int>(std::lround(s.bio.eyeY * static_cast<float>(gh - 9)));
    c.fillCircle(ex, ey, 4, palette::kEye);
    c.drawCircle(ex, ey, 7, palette::kText);

    // Bottom-right: Stage System & Audio Health (x = 324..476, y = 160..290)
    c.drawPanel(324, 160, 152, 130, palette::kGreen);
    c.drawText(332, 167, "STAGE TELEMETRY", palette::kGreen, 1);

    char l1[32], l2[32], l3[32], l4[32];
    std::snprintf(l1, sizeof(l1), "AUD: %.16s", sys.audioBackend.c_str());
    const float latMs = (sys.sampleRate > 0)
                            ? (2.0f * 1000.0f * static_cast<float>(sys.periodFrames) / static_cast<float>(sys.sampleRate))
                            : 0.0f;
    std::snprintf(l2, sizeof(l2), "%uHz/%uf (~%.1fms)", sys.sampleRate, sys.periodFrames, static_cast<double>(latMs));
    std::snprintf(l3, sizeof(l3), "XRUNS: %llu", static_cast<unsigned long long>(sys.xruns));
    std::snprintf(l4, sizeof(l4), "DISP: %.15s", sys.displayDriver.c_str());

    c.drawText(332, 184, l1, palette::kText, 1);
    c.drawText(332, 198, l2, palette::kDim, 1);
    c.drawText(332, 212, l3, (sys.xruns == 0) ? palette::kGreen : palette::kRed, 1);
    c.drawText(332, 226, l4, palette::kCyan, 1);

    std::string midiShort = "MIDI: " + sys.midiPort;
    if (midiShort.size() > 22) midiShort.resize(22);
    c.drawText(332, 240, midiShort.c_str(), palette::kDim, 1);
    c.drawText(332, 254, sys.oledHardware ? "OLED: I2C 0x3C OK" : "OLED: RAM BUFFER", palette::kDim, 1);
    c.drawText(332, 272, "CORE: RT LOCK-FREE", palette::kAmber, 1);
}

// ============================================================================
// Modal Hardware Cheat-Sheet Overlay ([I] / [?])
// ============================================================================
void InstrumentUi::renderHelpOverlay(Canvas& c) {
    c.fillRect(0, 0, kLogicalW, kLogicalH, Color::rgb(0, 0, 0, 218));
    c.drawPanel(12, 24, 456, 272, palette::kCyan, Color::rgb(14, 16, 20));

    c.drawText(24, 34, "VOICE BOARD :: HARDWARE & KEYBOARD CONTROLS", palette::kCyan, 1);
    c.drawTextRight(456, 34, "[I / ?] CLOSE", palette::kAmber, 1);
    c.drawHLine(24, 47, 432, palette::kBorder);

    const char* lines[] = {
        "PAGES       : 1..5 switch screen (PERFORM / VOICE / DRONE / SEQ / BIO)",
        "PRESETS     : F1..F7 or Akai Pads 1..7 (VOICE, WALL, AMBIENT, DRIFT)",
        "VOICE CHAIN : E/D select FX | Ctrl+E/D move | Q/W & A/S XY or Params",
        "DRONE CHAIN : Y/H select FX | Shift+Y/H ON/OFF | Z/X & C/V Drone XY",
        "MASTER BUS  : R/T select param | F/G -/+ value | Akai Knobs 7 & 8",
        "BIO MOD     : Shift+Key cycle src | Alt+1..4 src | Alt+Up/Down depth",
        "NOISE SEQ   : Space / Shift+G Play | ,/. step | [/] track | Enter trig",
        "SOURCES     : M Mic | N Drone | L/P/O Synth Voice | B Master Bypass",
        "TOUCH / PAD : Tap tabs/steps/LEDs, drag XY pads & rotary encoders"
    };
    for (int i = 0; i < 9; ++i) {
        c.drawText(24, 58 + i * 24, lines[i], (i % 2 == 0) ? palette::kText : palette::kDim, 1);
    }
}

// ============================================================================
// Keyboard Input Handler (Full Spec Section 6.1 + Screen Navigation 1..5)
// ============================================================================
bool InstrumentUi::handleKey(const std::string& key, KeyModFlags mods) {
    if (key.empty()) return false;
    const bool shift = hasFlag(mods, KeyModFlags::Shift);
    const bool ctrl  = hasFlag(mods, KeyModFlags::Ctrl);
    const bool alt   = hasFlag(mods, KeyModFlags::Alt);

    // Presets F1..F7
    if (key.size() == 2 && key[0] == 'F' && key[1] >= '1' && key[1] <= '7') {
        engine_.enqueueCommand({CommandType::ApplyPreset, key[1] - '1', 0, 0, 0});
        return true;
    }

    // Help overlay
    if (key == "i" || key == "I" || key == "?") {
        showHelp_ = !showHelp_;
        return true;
    }
    if (key == "Escape" && showHelp_) {
        showHelp_ = false;
        return true;
    }

    // Alt+1..4 Bio source, Alt+Up/Down Mod depth; bare 1..5 switches Hardware Screen!
    if (key.size() == 1 && key[0] >= '1' && key[0] <= '5') {
        const int digit = key[0] - '1';
        if (alt && digit < 4) {
            engine_.enqueueCommand({CommandType::SetActiveBio, digit + 1, 0, 0, 0});
            return true;
        }
        if (!alt && !ctrl && !shift) {
            page_ = static_cast<UiPage>(digit);
            return true;
        }
    }
    if (alt && key == "ArrowUp") {
        engine_.enqueueCommand({CommandType::AdjustModDepth, 0, 0, +0.05f, 0});
        return true;
    }
    if (alt && key == "ArrowDown") {
        engine_.enqueueCommand({CommandType::AdjustModDepth, 0, 0, -0.05f, 0});
        return true;
    }

    const FullState s = engine_.readSnapshot();
    constexpr float kStep = 0.05f;

    if (key == "Tab") {
        engine_.enqueueCommand({CommandType::ToggleKnobMode, 0, 0, 0, 0});
        return true;
    }

    // Single-character shortcuts
    if (key.size() == 1) {
        const char ch = key[0];
        const char lo = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));

        if (lo == 'e') {
            if (shift) engine_.enqueueCommand({CommandType::CycleModSource, voiceXyModIndex(s.voice.active, 0), 0, 0, 0});
            else if (ctrl) engine_.enqueueCommand({CommandType::MoveVoiceFx, -1, 0, 0, 0});
            else engine_.enqueueCommand({CommandType::CycleVoiceFx, -1, 0, 0, 0});
            return true;
        }
        if (lo == 'd') {
            if (shift) engine_.enqueueCommand({CommandType::CycleModSource, voiceXyModIndex(s.voice.active, 1), 0, 0, 0});
            else if (ctrl) engine_.enqueueCommand({CommandType::MoveVoiceFx, +1, 0, 0, 0});
            else engine_.enqueueCommand({CommandType::CycleVoiceFx, +1, 0, 0, 0});
            return true;
        }
        if (lo == 'q' || lo == 'w') {
            const float dir = (lo == 'q') ? -kStep : +kStep;
            if (shift) {
                if (getVoiceFxDef(s.voice.active).numKnobs > 0)
                    engine_.enqueueCommand({CommandType::CycleModSource, voiceKnobModIndex(s.voice.active, 0), 0, 0, 0});
            } else if (s.knobMode == KnobMode::XY) {
                engine_.enqueueCommand({CommandType::AdjustVoiceXY, 0, 0, dir, 0.0f});
            } else {
                engine_.enqueueCommand({CommandType::AdjustVoiceKnob, 0, 0, dir, 0.0f});
            }
            return true;
        }
        if (lo == 'a' || lo == 's') {
            const float dir = (lo == 'a') ? -kStep : +kStep;
            if (shift) {
                if (getVoiceFxDef(s.voice.active).numKnobs > 1)
                    engine_.enqueueCommand({CommandType::CycleModSource, voiceKnobModIndex(s.voice.active, 1), 0, 0, 0});
            } else if (s.knobMode == KnobMode::XY) {
                engine_.enqueueCommand({CommandType::AdjustVoiceXY, 0, 0, 0.0f, dir});
            } else {
                engine_.enqueueCommand({CommandType::AdjustVoiceKnob, 1, 0, dir, 0.0f});
            }
            return true;
        }

        // Drone chain
        if (lo == 'y' || lo == 'h') {
            const int dir = (lo == 'y') ? -1 : +1;
            if (shift) engine_.enqueueCommand({CommandType::ToggleDroneFx, 0, 0, 0, 0});
            else if (ctrl) engine_.enqueueCommand({CommandType::MoveDroneFx, dir, 0, 0, 0});
            else engine_.enqueueCommand({CommandType::CycleDroneFx, dir, 0, 0, 0});
            return true;
        }
        if (lo == 'z' || lo == 'x') {
            const float dir = (lo == 'z') ? -kStep : +kStep;
            if (shift) {
                const int ax = (lo == 'z') ? 0 : 1;
                engine_.enqueueCommand({CommandType::CycleModSource, droneXyModIndex(s.drone.active, ax), 0, 0, 0});
            } else {
                engine_.enqueueCommand({CommandType::AdjustDroneXY, 0, 0, dir, 0.0f});
            }
            return true;
        }
        if (lo == 'c' || lo == 'v') {
            const float dir = (lo == 'c') ? -kStep : +kStep;
            if (shift) {
                if (getDroneFxDef(s.drone.active).numKnobs > 0)
                    engine_.enqueueCommand({CommandType::CycleModSource, droneKnobModIndex(s.drone.active, 0), 0, 0, 0});
            } else {
                engine_.enqueueCommand({CommandType::AdjustDroneXY, 0, 0, 0.0f, dir});
            }
            return true;
        }

        // Master + Noise Play
        if (lo == 'r') { engine_.enqueueCommand({CommandType::SelectMasterParam, -1, 0, 0, 0}); return true; }
        if (lo == 't') { engine_.enqueueCommand({CommandType::SelectMasterParam, +1, 0, 0, 0}); return true; }
        if (lo == 'f') {
            if (shift) {
                const auto mp = static_cast<MasterParamId>(std::clamp(s.master.selectedParam, 0, 3));
                engine_.enqueueCommand({CommandType::CycleModSource, masterModIndex(mp), 0, 0, 0});
            } else {
                engine_.enqueueCommand({CommandType::AdjustMasterParam, 0, 0, -kStep, 0});
            }
            return true;
        }
        if (ch == 'g' && !shift) {
            engine_.enqueueCommand({CommandType::AdjustMasterParam, 0, 0, +kStep, 0});
            return true;
        }
        if (ch == 'G' || ch == ' ') {
            engine_.enqueueCommand({CommandType::NoiseTogglePlay, 0, 0, 0, 0});
            return true;
        }

        // Sources & Bypass
        if (lo == 'm') { engine_.enqueueCommand({CommandType::ToggleMic, 0, 0, 0, 0}); return true; }
        if (lo == 'n') { engine_.enqueueCommand({CommandType::ToggleDrone, 0, 0, 0, 0}); return true; }
        if (lo == 'l') { engine_.enqueueCommand({CommandType::SetFileOn, 1, 0, 0, 0}); return true; }
        if (lo == 'p') { engine_.enqueueCommand({CommandType::SetFileOn, s.fileOn ? 0 : 1, 0, 0, 0}); return true; }
        if (lo == 'o') { engine_.enqueueCommand({CommandType::SetFileOn, 0, 0, 0, 0}); return true; }
        if (lo == 'b') { engine_.enqueueCommand({CommandType::ToggleBypass, 0, 0, 0, 0}); return true; }

        // Sequencer step/track navigation
        if (ch == ',') { engine_.enqueueCommand({CommandType::NoiseSelectStep, -1, 1, 0, 0}); return true; }
        if (ch == '.') { engine_.enqueueCommand({CommandType::NoiseSelectStep, +1, 1, 0, 0}); return true; }
        if (ch == '[') { engine_.enqueueCommand({CommandType::NoiseSelectTrack, -1, 1, 0, 0}); return true; }
        if (ch == ']') { engine_.enqueueCommand({CommandType::NoiseSelectTrack, +1, 1, 0, 0}); return true; }
    }

    if (key == "Enter" || key == "Return") {
        engine_.enqueueCommand({CommandType::NoiseToggleStep, s.noise.activeTrack, s.noise.selectedStep, 0, 0});
        return true;
    }

    return false;
}

// ============================================================================
// Touch / Mouse Pointer Handlers (480x320 Logical Hardware Coordinates)
// ============================================================================
void InstrumentUi::handlePointerDown(int lx, int ly) {
    dragKind_ = DragKind::None;
    dragStartX_ = lx;
    dragStartY_ = ly;

    if (showHelp_) {
        showHelp_ = false;
        return;
    }

    const FullState s = engine_.readSnapshot();

    // 1. Top Header hits (y = 0..32)
    if (ly < 32) {
        if (hitRect(lx, ly, 4, 3, 102, 26)) {
            const int next = (static_cast<int>(s.preset) + 1) % static_cast<int>(kNumPresets);
            engine_.enqueueCommand({CommandType::ApplyPreset, next, 0, 0, 0});
            return;
        }
        for (int i = 0; i < 5; ++i) {
            if (hitRect(lx, ly, 110 + i * 48, 3, 46, 26)) {
                page_ = static_cast<UiPage>(i);
                return;
            }
        }
        if (hitRect(lx, ly, 355, 3, 19, 26)) { engine_.enqueueCommand({CommandType::ToggleMic, 0, 0, 0, 0}); return; }
        if (hitRect(lx, ly, 374, 3, 19, 26)) { engine_.enqueueCommand({CommandType::ToggleDrone, 0, 0, 0, 0}); return; }
        if (hitRect(lx, ly, 393, 3, 19, 26)) { engine_.enqueueCommand({CommandType::SetFileOn, s.fileOn ? 0 : 1, 0, 0, 0}); return; }
        if (hitRect(lx, ly, 412, 3, 19, 26)) { engine_.enqueueCommand({CommandType::ToggleBypass, 0, 0, 0, 0}); return; }
        if (hitRect(lx, ly, 456, 3, 20, 26)) { showHelp_ = !showHelp_; return; }
        return;
    }

    // Bottom bar tap on KNOB mode toggles XY/PARAM
    if (ly >= 294) {
        if (lx < 150) engine_.enqueueCommand({CommandType::ToggleKnobMode, 0, 0, 0, 0});
        return;
    }

    // Page-specific touch zones
    if (page_ == UiPage::Perform) {
        // Voice XY Pad (x = 12..144, y = 55..187)
        if (hitRect(lx, ly, 12, 55, 132, 132)) {
            dragKind_ = DragKind::VoicePadXY;
            dragRectX_ = 12; dragRectY_ = 55; dragRectW_ = 132; dragRectH_ = 132;
            applyDragAt(lx, ly);
            return;
        }
        // Voice ON/OFF & < >
        if (hitRect(lx, ly, 166, 39, 28, 12)) { engine_.enqueueCommand({CommandType::ToggleVoiceFx, 0, 0, 0, 0}); return; }
        if (hitRect(lx, ly, 150, 159, 21, 26)) { engine_.enqueueCommand({CommandType::CycleVoiceFx, -1, 0, 0, 0}); return; }
        if (hitRect(lx, ly, 174, 159, 21, 26)) { engine_.enqueueCommand({CommandType::CycleVoiceFx, +1, 0, 0, 0}); return; }

        // Drone XY Pad (x = 212..344, y = 55..187)
        if (hitRect(lx, ly, 212, 55, 132, 132)) {
            dragKind_ = DragKind::DronePadXY;
            dragRectX_ = 212; dragRectY_ = 55; dragRectW_ = 132; dragRectH_ = 132;
            applyDragAt(lx, ly);
            return;
        }
        if (hitRect(lx, ly, 366, 39, 28, 12)) { engine_.enqueueCommand({CommandType::ToggleDroneFx, 0, 0, 0, 0}); return; }
        if (hitRect(lx, ly, 350, 159, 21, 26)) { engine_.enqueueCommand({CommandType::CycleDroneFx, -1, 0, 0, 0}); return; }
        if (hitRect(lx, ly, 374, 159, 21, 26)) { engine_.enqueueCommand({CommandType::CycleDroneFx, +1, 0, 0, 0}); return; }

        // Sequencer Ribbon Play/Stop & 16 Step Pads
        if (hitRect(lx, ly, 398, 220, 70, 16)) {
            engine_.enqueueCommand({CommandType::NoiseTogglePlay, 0, 0, 0, 0});
            return;
        }
        for (int i = 0; i < 16; ++i) {
            if (hitRect(lx, ly, 10 + i * 29, 242, 26, 40)) {
                engine_.enqueueCommand({CommandType::NoiseToggleStep, s.noise.activeTrack, i, 0, 0});
                return;
            }
        }
        return;
    }

    if (page_ == UiPage::VoiceChain) {
        if (hitRect(lx, ly, 306, 38, 48, 15)) { engine_.enqueueCommand({CommandType::MoveVoiceFx, -1, 0, 0, 0}); return; }
        if (hitRect(lx, ly, 358, 38, 48, 15)) { engine_.enqueueCommand({CommandType::MoveVoiceFx, +1, 0, 0, 0}); return; }
        if (hitRect(lx, ly, 412, 38, 56, 15)) { engine_.enqueueCommand({CommandType::ToggleVoiceFx, 0, 0, 0, 0}); return; }

        for (size_t i = 0; i < kNumVoiceFx; ++i) {
            if (hitRect(lx, ly, 9 + static_cast<int>(i) * 42, 58, 40, 24)) {
                engine_.enqueueCommand({CommandType::SelectVoiceFx, static_cast<int32_t>(s.voice.order[i]), 0, 0, 0});
                return;
            }
        }

        const VoiceFxId act = s.voice.active;
        const FxDef& def = getVoiceFxDef(act);
        const int cardX[4] = {4, 123, 242, 361};
        const uint16_t targets[4] = {
            voiceXyModIndex(act, 0),
            voiceXyModIndex(act, 1),
            (def.numKnobs > 0) ? voiceKnobModIndex(act, 0) : kInvalidModTarget,
            (def.numKnobs > 1) ? voiceKnobModIndex(act, 1) : kInvalidModTarget
        };
        const float curVals[4] = {
            s.voice.xy[static_cast<size_t>(act)].x,
            s.voice.xy[static_cast<size_t>(act)].y,
            (def.numKnobs > 0) ? s.voice.knobs[static_cast<size_t>(act)][0] : 0.0f,
            (def.numKnobs > 1) ? s.voice.knobs[static_cast<size_t>(act)][1] : 0.0f
        };

        for (int cIdx = 0; cIdx < 4; ++cIdx) {
            if (targets[cIdx] == kInvalidModTarget) continue;
            // Mod source pill (x + 5, y = 272, w = 36, h = 13)
            if (hitRect(lx, ly, cardX[cIdx] + 5, 272, 36, 15)) {
                engine_.enqueueCommand({CommandType::CycleModSource, targets[cIdx], 0, 0, 0});
                return;
            }
            // Mod depth bar (x + 44, y = 272, w = 66, h = 15)
            if (hitRect(lx, ly, cardX[cIdx] + 44, 272, 66, 15)) {
                dragKind_ = static_cast<DragKind>(static_cast<int>(DragKind::VoiceModDepth0) + cIdx);
                dragRectX_ = cardX[cIdx] + 44; dragRectW_ = 66;
                applyDragAt(lx, ly);
                return;
            }
            // Rotary dial body
            if (hitRect(lx, ly, cardX[cIdx], 92, 115, 176)) {
                dragKind_ = static_cast<DragKind>(static_cast<int>(DragKind::VoiceEncoder0) + cIdx);
                dragStartVal_ = curVals[cIdx];
                return;
            }
        }
        return;
    }

    if (page_ == UiPage::DroneMaster) {
        if (hitRect(lx, ly, 184, 38, 48, 15)) { engine_.enqueueCommand({CommandType::ToggleDroneFx, 0, 0, 0, 0}); return; }
        for (size_t i = 0; i < kNumDroneFx; ++i) {
            const int bx = 10 + static_cast<int>(i % 4) * 56;
            const int by = 60 + static_cast<int>(i / 4) * 34;
            if (hitRect(lx, ly, bx, by, 52, 28)) {
                engine_.enqueueCommand({CommandType::SelectDroneFx, static_cast<int32_t>(s.drone.order[i]), 0, 0, 0});
                return;
            }
        }

        const DroneFxId dFx = s.drone.active;
        if (hitRect(lx, ly, 247, 136, 36, 15)) {
            engine_.enqueueCommand({CommandType::CycleModSource, droneXyModIndex(dFx, 0), 0, 0, 0});
            return;
        }
        if (hitRect(lx, ly, 363, 136, 36, 15)) {
            engine_.enqueueCommand({CommandType::CycleModSource, droneXyModIndex(dFx, 1), 0, 0, 0});
            return;
        }
        if (hitRect(lx, ly, 242, 40, 112, 94)) {
            dragKind_ = DragKind::DroneEncoderX;
            dragStartVal_ = s.drone.xy[static_cast<size_t>(dFx)].x;
            return;
        }
        if (hitRect(lx, ly, 358, 40, 112, 94)) {
            dragKind_ = DragKind::DroneEncoderY;
            dragStartVal_ = s.drone.xy[static_cast<size_t>(dFx)].y;
            return;
        }

        // 4 Master Encoders
        const float mVals[4] = {s.master.voiceOut, s.master.droneVol, s.master.fb, s.master.lpf};
        for (int i = 0; i < 4; ++i) {
            const int mx = 9 + i * 116;
            const auto mp = static_cast<MasterParamId>(i);
            if (hitRect(lx, ly, mx + 5, 268, 36, 15)) {
                engine_.enqueueCommand({CommandType::CycleModSource, masterModIndex(mp), 0, 0, 0});
                return;
            }
            if (hitRect(lx, ly, mx + 44, 268, 63, 15)) {
                dragKind_ = static_cast<DragKind>(static_cast<int>(DragKind::MasterModDepth0) + i);
                dragRectX_ = mx + 44; dragRectW_ = 63;
                applyDragAt(lx, ly);
                return;
            }
            if (hitRect(lx, ly, mx, 180, 112, 86)) {
                const int diff = i - s.master.selectedParam;
                if (diff != 0) engine_.enqueueCommand({CommandType::SelectMasterParam, diff, 0, 0, 0});
                dragKind_ = static_cast<DragKind>(static_cast<int>(DragKind::MasterEncoder0) + i);
                dragStartVal_ = mVals[i];
                return;
            }
        }
        return;
    }

    if (page_ == UiPage::NoiseSeq) {
        if (hitRect(lx, ly, 312, 38, 76, 15)) {
            engine_.enqueueCommand({CommandType::NoiseClearStepLocks, s.noise.activeTrack, s.noise.selectedStep, 0, 0});
            return;
        }
        if (hitRect(lx, ly, 394, 38, 74, 15)) {
            engine_.enqueueCommand({CommandType::NoiseTogglePlay, 0, 0, 0, 0});
            return;
        }

        // 4x16 Matrix taps
        for (int t = 0; t < 4; ++t) {
            const int ry = 58 + t * 25;
            if (hitRect(lx, ly, 10, ry, 42, 22)) {
                engine_.enqueueCommand({CommandType::NoiseSelectTrack, t, 0, 0, 0});
                return;
            }
            for (int st = 0; st < 16; ++st) {
                if (hitRect(lx, ly, 56 + st * 26, ry, 24, 22)) {
                    if (t != s.noise.activeTrack) {
                        engine_.enqueueCommand({CommandType::NoiseSelectTrack, t, 0, 0, 0});
                    }
                    engine_.enqueueCommand({CommandType::NoiseToggleStep, t, st, 0, 0});
                    return;
                }
            }
        }

        // Synth Parameter Bars (5 cols x 3 rows)
        const int actTr = std::clamp(s.noise.activeTrack, 0, 3);
        const NoiseVoiceParams& v = s.noise.tracks[static_cast<size_t>(actTr)].voice;
        const int32_t packed = ((actTr & 0xFF) << 8) | (std::clamp(s.noise.selectedStep, 0, 15) & 0xFF);

        // Discrete click toggles (Type, LFO1 Target, LFO2 Target)
        if (hitRect(lx, ly, 10, 188, 88, 22)) {
            const int nextT = (static_cast<int>(v.type) + 1) % 3;
            engine_.enqueueCommand({CommandType::NoiseSetVoiceParam, packed,
                                    static_cast<int32_t>(NoiseLockField::Type), static_cast<float>(nextT), 0});
            return;
        }
        if (hitRect(lx, ly, 196, 240, 88, 22)) {
            const int nextL = (static_cast<int>(v.lfo1.target) + 1) % 3;
            engine_.enqueueCommand({CommandType::NoiseSetVoiceParam, packed,
                                    static_cast<int32_t>(NoiseLockField::Lfo1Target), static_cast<float>(nextL), 0});
            return;
        }
        if (hitRect(lx, ly, 289, 240, 88, 22)) {
            const int nextL = (static_cast<int>(v.lfo2.target) + 1) % 3;
            engine_.enqueueCommand({CommandType::NoiseSetVoiceParam, packed,
                                    static_cast<int32_t>(NoiseLockField::Lfo2Target), static_cast<float>(nextL), 0});
            return;
        }

        struct FaderHit { int x, y; NoiseLockField field; float lo, hi; };
        const FaderHit faders[] = {
            {10,  214, NoiseLockField::Cutoff,       20.0f,  12000.0f},
            {10,  240, NoiseLockField::Resonance,    0.0f,   0.95f},
            {103, 188, NoiseLockField::AmpAttack,    0.001f, 1.0f},
            {103, 214, NoiseLockField::AmpDecay,     0.01f,  2.0f},
            {103, 240, NoiseLockField::ModEnvAmount, 0.0f,   1.0f},
            {196, 188, NoiseLockField::Lfo1Rate,     0.05f,  20.0f},
            {196, 214, NoiseLockField::Lfo1Depth,    0.0f,   1.0f},
            {289, 188, NoiseLockField::Lfo2Rate,     0.05f,  20.0f},
            {289, 214, NoiseLockField::Lfo2Depth,    0.0f,   1.0f},
            {382, 188, NoiseLockField::Pan,         -1.0f,   1.0f},
            {382, 214, NoiseLockField::Gain,         0.0f,   1.0f}
        };
        for (const auto& fh : faders) {
            if (hitRect(lx, ly, fh.x, fh.y, 88, 22)) {
                dragKind_ = DragKind::NoiseFader;
                dragRectX_ = fh.x + 4;
                dragRectW_ = 80;
                dragNoiseField_ = fh.field;
                dragMin_ = fh.lo;
                dragMax_ = fh.hi;
                applyDragAt(lx, ly);
                return;
            }
        }
        return;
    }
}

void InstrumentUi::handlePointerMove(int lx, int ly) {
    if (dragKind_ != DragKind::None) {
        applyDragAt(lx, ly);
    }
}

void InstrumentUi::handlePointerUp(int /*lx*/, int /*ly*/) {
    dragKind_ = DragKind::None;
}

void InstrumentUi::applyDragAt(int lx, int ly) {
    const FullState s = engine_.readSnapshot();
    const float deltaNorm = static_cast<float>((lx - dragStartX_) - (ly - dragStartY_)) / 140.0f;

    switch (dragKind_) {
        case DragKind::VoicePadXY: {
            const float x = clampf(static_cast<float>(lx - dragRectX_) / static_cast<float>(dragRectW_), 0.0f, 1.0f);
            const float y = clampf(1.0f - static_cast<float>(ly - dragRectY_) / static_cast<float>(dragRectH_), 0.0f, 1.0f);
            engine_.enqueueCommand({CommandType::SetVoiceXY, static_cast<int32_t>(s.voice.active), 0, x, y});
            break;
        }
        case DragKind::DronePadXY: {
            const float x = clampf(static_cast<float>(lx - dragRectX_) / static_cast<float>(dragRectW_), 0.0f, 1.0f);
            const float y = clampf(1.0f - static_cast<float>(ly - dragRectY_) / static_cast<float>(dragRectH_), 0.0f, 1.0f);
            engine_.enqueueCommand({CommandType::SetDroneXY, static_cast<int32_t>(s.drone.active), 0, x, y});
            break;
        }
        case DragKind::VoiceEncoder0:
        case DragKind::VoiceEncoder1: {
            const VoiceFxId fx = s.voice.active;
            XY cur = s.voice.xy[static_cast<size_t>(fx)];
            if (dragKind_ == DragKind::VoiceEncoder0) cur.x = clampf(dragStartVal_ + deltaNorm, 0.0f, 1.0f);
            else                                      cur.y = clampf(dragStartVal_ + deltaNorm, 0.0f, 1.0f);
            engine_.enqueueCommand({CommandType::SetVoiceXY, static_cast<int32_t>(fx), 0, cur.x, cur.y});
            break;
        }
        case DragKind::VoiceEncoder2:
        case DragKind::VoiceEncoder3: {
            const VoiceFxId fx = s.voice.active;
            const int kIdx = (dragKind_ == DragKind::VoiceEncoder2) ? 0 : 1;
            const FxDef& def = getVoiceFxDef(fx);
            if (kIdx < def.numKnobs) {
                const float val = clampf(dragStartVal_ + deltaNorm * (def.knobs[kIdx].maxVal - def.knobs[kIdx].minVal),
                                         def.knobs[kIdx].minVal, def.knobs[kIdx].maxVal);
                engine_.enqueueCommand({CommandType::SetVoiceKnob, static_cast<int32_t>(fx), kIdx, val, 0});
            }
            break;
        }
        case DragKind::VoiceModDepth0:
        case DragKind::VoiceModDepth1:
        case DragKind::VoiceModDepth2:
        case DragKind::VoiceModDepth3: {
            const int idx = static_cast<int>(dragKind_) - static_cast<int>(DragKind::VoiceModDepth0);
            const VoiceFxId fx = s.voice.active;
            const uint16_t tid = (idx < 2) ? voiceXyModIndex(fx, idx) : voiceKnobModIndex(fx, idx - 2);
            const float ratio = clampf(static_cast<float>(lx - dragRectX_) / static_cast<float>(dragRectW_), 0.0f, 1.0f);
            ModEntry cur = s.modulation.get(tid);
            const BioSource src = (cur.source != BioSource::None) ? cur.source : s.activeBio;
            engine_.enqueueCommand({CommandType::SetModulation, tid, static_cast<int32_t>(src), ratio, 0});
            break;
        }
        case DragKind::DroneEncoderX:
        case DragKind::DroneEncoderY: {
            const DroneFxId fx = s.drone.active;
            XY cur = s.drone.xy[static_cast<size_t>(fx)];
            if (dragKind_ == DragKind::DroneEncoderX) cur.x = clampf(dragStartVal_ + deltaNorm, 0.0f, 1.0f);
            else                                      cur.y = clampf(dragStartVal_ + deltaNorm, 0.0f, 1.0f);
            engine_.enqueueCommand({CommandType::SetDroneXY, static_cast<int32_t>(fx), 0, cur.x, cur.y});
            break;
        }
        case DragKind::MasterEncoder0:
        case DragKind::MasterEncoder1:
        case DragKind::MasterEncoder2:
        case DragKind::MasterEncoder3: {
            const int mIdx = static_cast<int>(dragKind_) - static_cast<int>(DragKind::MasterEncoder0);
            const auto mp = static_cast<MasterParamId>(mIdx);
            const KnobDef& kd = getMasterParamDef(mp);
            const float val = clampf(dragStartVal_ + deltaNorm * (kd.maxVal - kd.minVal), kd.minVal, kd.maxVal);
            engine_.enqueueCommand({CommandType::SetMasterParam, mIdx, 0, val, 0});
            break;
        }
        case DragKind::MasterModDepth0:
        case DragKind::MasterModDepth1:
        case DragKind::MasterModDepth2:
        case DragKind::MasterModDepth3: {
            const int mIdx = static_cast<int>(dragKind_) - static_cast<int>(DragKind::MasterModDepth0);
            const uint16_t tid = masterModIndex(static_cast<MasterParamId>(mIdx));
            const float ratio = clampf(static_cast<float>(lx - dragRectX_) / static_cast<float>(dragRectW_), 0.0f, 1.0f);
            ModEntry cur = s.modulation.get(tid);
            const BioSource src = (cur.source != BioSource::None) ? cur.source : s.activeBio;
            engine_.enqueueCommand({CommandType::SetModulation, tid, static_cast<int32_t>(src), ratio, 0});
            break;
        }
        case DragKind::NoiseFader: {
            const float ratio = clampf(static_cast<float>(lx - dragRectX_) / static_cast<float>(dragRectW_), 0.0f, 1.0f);
            const float val = dragMin_ + ratio * (dragMax_ - dragMin_);
            const int32_t packed = ((s.noise.activeTrack & 0xFF) << 8) | (s.noise.selectedStep & 0xFF);
            engine_.enqueueCommand({CommandType::NoiseSetVoiceParam, packed,
                                    static_cast<int32_t>(dragNoiseField_), val, 0});
            break;
        }
        default: break;
    }
}

} // namespace biomus::ui
