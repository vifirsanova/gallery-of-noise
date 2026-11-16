#include "TestHarness.hpp"

#include "app/MidiController.hpp"
#include "app/OledDisplay.hpp"
#include "audio/AudioBackend.hpp"
#include "core/Engine.hpp"
#include "ui/Canvas.hpp"
#include "ui/InstrumentUi.hpp"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

// ============================================================================
// 1. MIDI CC, Note Pad Preset, Program Change & Parameter-Lock Integration Test
// ============================================================================
TEST_CASE("MIDI Akai controller messages update Engine state and parameter locks", "[integration][midi]") {
    biomus::Engine engine;
    engine.prepare(48000.0, 128);
    biomus::MidiController midi(engine);

    std::vector<float> zeroIn(128, 0.0f);
    std::vector<float> dummyOut(128, 0.0f);

    // 1. Akai Pad 2 (Note 37 = C#1) -> Preset WALL (index 1)
    const uint8_t noteWall[3] = {0x90, 37, 100};
    midi.handleMidiBytes(noteWall, 3);
    engine.process(zeroIn.data(), dummyOut.data(), 128);
    REQUIRE(engine.readSnapshot().preset == biomus::PresetId::WALL);

    // 2. Knob 3 (CC 3 = 127) -> Voice active X = 1.0
    const uint8_t ccVoiceX[3] = {0xB0, 3, 127};
    midi.handleMidiBytes(ccVoiceX, 3);
    engine.process(zeroIn.data(), dummyOut.data(), 128);
    const auto activeVoiceFx = engine.readSnapshot().voice.active;
    REQUIRE(engine.readSnapshot().voice.xy[static_cast<size_t>(activeVoiceFx)].x == Approx(1.0f));

    // 3. Program Change 2 -> Noise Track 3 (index 2)
    const uint8_t pcTrack2[2] = {0xC0, 2};
    midi.handleMidiBytes(pcTrack2, 2);
    engine.process(zeroIn.data(), dummyOut.data(), 128);
    REQUIRE(engine.readSnapshot().noise.activeTrack == 2);

    // 4. Hold Step Pad (Note 46 -> step 2) + turn Knob 3 (CC 72 on Akai MPK bank) -> Cutoff Lock!
    const uint8_t padHold[3] = {0x90, 46, 100};
    const uint8_t knobLock[3] = {0xB0, 72, 64};
    const uint8_t padRel[3] = {0x80, 46, 0};
    midi.handleMidiBytes(padHold, 3);
    midi.handleMidiBytes(knobLock, 3);
    midi.handleMidiBytes(padRel, 3);
    engine.process(zeroIn.data(), dummyOut.data(), 128);

    const auto snap = engine.readSnapshot();
    REQUIRE(snap.noise.tracks[2].steps[2].locks.has(biomus::NoiseLockField::Cutoff));
    REQUIRE(snap.noise.tracks[2].steps[2].locks.values.cutoff > 5000.0f);
}

// ============================================================================
// 2. Native Small-Screen Canvas, RGB565 SPI TFT Conversion & PNG Export Test
// ============================================================================
TEST_CASE("Software Canvas renders hardware arcs, converts RGB565, and exports valid PNG", "[integration][ui]") {
    biomus::ui::Canvas canvas(480, 320);
    canvas.clear(biomus::ui::palette::kBg);

    // Pure R, G, B 16-bit RGB565 SPI TFT verification
    REQUIRE(biomus::ui::Canvas::argbToRgb565(0xFFFF0000u) == 0xF800u);
    REQUIRE(biomus::ui::Canvas::argbToRgb565(0xFF00FF00u) == 0x07E0u);
    REQUIRE(biomus::ui::Canvas::argbToRgb565(0xFF0000FFu) == 0x001Fu);

    canvas.drawPanel(10, 10, 200, 140, biomus::ui::palette::kViolet);
    canvas.drawRotaryArc(110, 80, 32, 4, -135.0f, 90.0f, biomus::ui::palette::kCyan);
    canvas.drawText(20, 20, "VOICE BOARD 480x320", biomus::ui::palette::kAmber, 2);

    const std::string tmpPng = "/tmp/biomus_canvas_test.png";
    REQUIRE(canvas.savePng(tmpPng));

    std::ifstream f(tmpPng, std::ios::binary);
    REQUIRE(f.is_open());
    uint8_t sig[8]{};
    f.read(reinterpret_cast<char*>(sig), 8);
    REQUIRE(sig[0] == 0x89);
    REQUIRE(sig[1] == 'P');
    REQUIRE(sig[2] == 'N');
    REQUIRE(sig[3] == 'G');
    f.close();
    std::remove(tmpPng.c_str());
}

// ============================================================================
// 3. Autonomous Embedded InstrumentUi: 5 Pages + Keyboard & Touch Dispatch
// ============================================================================
TEST_CASE("InstrumentUi renders all 5 small-screen pages and dispatches keyboard/touch", "[integration][ui]") {
    biomus::Engine engine;
    engine.prepare(48000.0, 128);

    biomus::ui::InstrumentUi ui(engine);
    biomus::ui::Canvas canvas(480, 320);
    biomus::ui::SystemStatusInfo sys;
    sys.audioBackend = "Simulated-48kHz";

    std::vector<float> zeroIn(128, 0.0f);
    std::vector<float> dummyOut(128, 0.0f);
    auto pumpEngine = [&]() {
        engine.process(zeroIn.data(), dummyOut.data(), 128);
    };

    // 1. Render all 5 distinct pages and ensure each produces rich non-background pixels
    std::vector<std::vector<uint32_t>> pageFrames(5);
    const uint32_t bgArgb = biomus::ui::palette::kBg.toARGB();
    for (int p = 0; p < 5; ++p) {
        ui.setPage(static_cast<biomus::ui::UiPage>(p));
        ui.render(canvas, sys);
        size_t nonBg = 0;
        for (int i = 0; i < canvas.width() * canvas.height(); ++i) {
            if (canvas.data()[i] != bgArgb) ++nonBg;
        }
        REQUIRE(nonBg > 8000);
        pageFrames[static_cast<size_t>(p)].assign(canvas.data(), canvas.data() + 480 * 320);
    }
    REQUIRE(pageFrames[0] != pageFrames[1]);
    REQUIRE(pageFrames[1] != pageFrames[3]);
    REQUIRE(pageFrames[3] != pageFrames[4]);

    // 2. Keyboard shortcuts (Section 6.1 + Hardware page switching)
    REQUIRE(ui.handleKey("2")); // Switch to Page 2: VoiceChain
    REQUIRE(ui.page() == biomus::ui::UiPage::VoiceChain);

    REQUIRE(ui.handleKey("F7")); // Apply preset DRIFT
    pumpEngine();
    REQUIRE(engine.readSnapshot().preset == biomus::PresetId::DRIFT);

    // Lowercase 'g' increments Master selected param by 5% of [0..3] range (1.00 -> 1.15), NOT Noise Play!
    REQUIRE(ui.handleKey("g"));
    pumpEngine();
    REQUIRE(engine.readSnapshot().master.voiceOut == Approx(1.15f));
    REQUIRE(engine.readSnapshot().noise.playing == false);

    // Uppercase 'G' (Shift+G) toggles Noise Sequencer Play!
    REQUIRE(ui.handleKey("G", biomus::ui::KeyModFlags::Shift));
    pumpEngine();
    REQUIRE(engine.readSnapshot().noise.playing == true);

    // Alt+2 selects Heart bio source
    REQUIRE(ui.handleKey("2", biomus::ui::KeyModFlags::Alt));
    pumpEngine();
    REQUIRE(engine.readSnapshot().activeBio == biomus::BioSource::Heart);

    // Tab switches KnobMode XY -> PARAM
    REQUIRE(ui.handleKey("Tab"));
    pumpEngine();
    REQUIRE(engine.readSnapshot().knobMode == biomus::KnobMode::PARAM);

    // 3. Touchscreen / Mouse Pointer Gestures in 480x320 Logical Space
    // Tap Top Tab 1 (x = 125, y = 14) -> Page Perform
    ui.handlePointerDown(125, 14);
    ui.handlePointerUp(125, 14);
    REQUIRE(ui.page() == biomus::ui::UiPage::Perform);

    // Drag Voice 2D XY Vector Pad on Perform page to top-right corner (x=144, y=55 -> X=1.0, Y=1.0)
    ui.handlePointerDown(78, 120);
    ui.handlePointerMove(144, 55);
    ui.handlePointerUp(144, 55);
    pumpEngine();
    const auto vAct = engine.readSnapshot().voice.active;
    REQUIRE(engine.readSnapshot().voice.xy[static_cast<size_t>(vAct)].x == Approx(1.0f));
    REQUIRE(engine.readSnapshot().voice.xy[static_cast<size_t>(vAct)].y == Approx(1.0f));

    // Tap Tab 4 (NOISE SEQ, x = 270, y = 14) and tap Track 2 Step 5 cell (x = 56 + 4*26 + 10, y = 58 + 25 + 10)
    ui.handlePointerDown(270, 14);
    ui.handlePointerUp(270, 14);
    REQUIRE(ui.page() == biomus::ui::UiPage::NoiseSeq);

    ui.handlePointerDown(56 + 4 * 26 + 10, 58 + 25 + 10);
    ui.handlePointerUp(56 + 4 * 26 + 10, 58 + 25 + 10);
    pumpEngine();
    REQUIRE(engine.readSnapshot().noise.activeTrack == 1);
    REQUIRE(engine.readSnapshot().noise.tracks[1].steps[4].trigger == true);
}

// ============================================================================
// 4. OLED 128x64 Framebuffer Rendering Test
// ============================================================================
TEST_CASE("OLED 128x64 SSD1306 framebuffer renders active state cleanly", "[integration][oled]") {
    biomus::FullState s = biomus::makeDefaultState();
    s.masterLevel = 0.75f;
    s.clip = true;
    s.noise.playing = true;

    std::array<uint8_t, biomus::OledDisplay::kBufferBytes> fbVoice{};
    biomus::OledDisplay::renderFrame(s, fbVoice);

    int litBytes = 0;
    for (uint8_t b : fbVoice) {
        if (b != 0) ++litBytes;
    }
    REQUIRE(litBytes > 80);

    biomus::applyPreset(s, biomus::PresetId::WALL);
    std::array<uint8_t, biomus::OledDisplay::kBufferBytes> fbWall{};
    biomus::OledDisplay::renderFrame(s, fbWall);
    REQUIRE(fbVoice != fbWall);
}

#ifndef BIOMUSIC_HAVE_CATCH2
int main() {
    for (const auto& t : minicatch::registry()) {
        t.fn();
    }
    return 0;
}
#endif
