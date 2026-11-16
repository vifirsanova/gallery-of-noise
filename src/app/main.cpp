#include "audio/AudioBackend.hpp"
#include "app/MidiController.hpp"
#include "app/OledDisplay.hpp"
#include "core/Engine.hpp"
#include "ui/Canvas.hpp"
#include "ui/DisplayBackend.hpp"
#include "ui/InstrumentUi.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {
std::atomic<bool> gQuit{false};

void handleSignal(int) {
    gQuit.store(true);
}
} // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    biomus::AudioConfig audioCfg;
    biomus::ui::DisplayOptions dispOpts;
    std::string i2cDev = "/dev/i2c-1";
    std::string exportScreensDir;
    bool preferAlsa = false;
    bool demoMode = false;
    bool headless = false;
    double maxRunSeconds = 0.0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--alsa") {
            preferAlsa = true;
        } else if (arg == "--device" && i + 1 < argc) {
            audioCfg.device = argv[++i];
            preferAlsa = true;
        } else if (arg == "--sr" && i + 1 < argc) {
            audioCfg.sampleRate = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--period" && i + 1 < argc) {
            audioCfg.periodFrames = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--fb" && i + 1 < argc) {
            dispOpts.fbDevice = argv[++i];
        } else if (arg == "--scale" && i + 1 < argc) {
            dispOpts.windowScale = std::stoi(argv[++i]);
        } else if (arg == "--fullscreen") {
            dispOpts.fullscreen = true;
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--demo") {
            demoMode = true;
        } else if (arg == "--export-screens" && i + 1 < argc) {
            exportScreensDir = argv[++i];
        } else if (arg == "--duration" && i + 1 < argc) {
            maxRunSeconds = std::stod(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Voice Board — Bio Modulation + Noise (Autonomous Embedded Instrument)\n"
                      << "Usage: app [options]\n"
                      << "  --alsa                Prefer hardware ALSA full-duplex backend\n"
                      << "  --device <name>       ALSA device name (default: default)\n"
                      << "  --sr <hz>             Sample rate (default: 48000)\n"
                      << "  --period <frames>     Buffer period size (default: 128)\n"
                      << "  --fb <path>           Direct Linux framebuffer (e.g. /dev/fb0 for SPI TFT)\n"
                      << "  --scale <1..4>        Desktop SDL2 window scale (default: 2 -> 960x640)\n"
                      << "  --fullscreen          Fullscreen KMSDRM / desktop kiosk mode\n"
                      << "  --headless            Disable TFT/SDL2 UI (run audio + MIDI + I2C OLED only)\n"
                      << "  --demo                Enable synthetic voice + DRIFT preset + sequencer\n"
                      << "  --export-screens <d>  Render all 5 native 480x320 UI pages to PNG and exit\n"
                      << "  --duration <sec>      Exit automatically after <sec> seconds\n";
            return 0;
        }
    }

    biomus::Engine engine;
    engine.prepare(static_cast<double>(audioCfg.sampleRate), audioCfg.periodFrames * 4);

    if (demoMode || !exportScreensDir.empty()) {
        engine.enqueueCommand({biomus::CommandType::ApplyPreset,
                               static_cast<int32_t>(biomus::PresetId::DRIFT), 0, 0, 0});
        engine.enqueueCommand({biomus::CommandType::SetFileOn, 1, 0, 0, 0});
        for (int s : {0, 4, 8, 12}) {
            engine.enqueueCommand({biomus::CommandType::NoiseSetStepTrigger, 0, s, 1.0f, 0});
        }
        for (int s : {2, 6, 10, 14}) {
            engine.enqueueCommand({biomus::CommandType::NoiseSetStepTrigger, 1, s, 1.0f, 0});
        }
        for (int s : {3, 11}) {
            engine.enqueueCommand({biomus::CommandType::NoiseSetStepTrigger, 2, s, 1.0f, 0});
        }
        engine.enqueueCommand({biomus::CommandType::NoiseSetVoiceParam, (0 << 8) | 4,
                               static_cast<int32_t>(biomus::NoiseLockField::Cutoff), 4800.0f, 0});
        engine.enqueueCommand({biomus::CommandType::NoiseSetVoiceParam, (1 << 8) | 10,
                               static_cast<int32_t>(biomus::NoiseLockField::Resonance), 0.72f, 0});
        engine.enqueueCommand({biomus::CommandType::NoiseTogglePlay, 0, 0, 0, 0});
    }

    // Offline screenshot export mode for documentation / CI / hardware screen preview
    if (!exportScreensDir.empty()) {
        std::filesystem::create_directories(exportScreensDir);
        biomus::ui::InstrumentUi ui(engine);
        biomus::ui::Canvas canvas(biomus::ui::InstrumentUi::kLogicalW,
                                  biomus::ui::InstrumentUi::kLogicalH);

        // Warm up DSP & rolling bio-oscilloscopes (~4 seconds of simulated audio)
        std::vector<float> inMono(256, 0.0f), outStereo(512, 0.0f);
        biomus::ui::SystemStatusInfo sysInfo;
        sysInfo.audioBackend = "ALSA-Duplex";
        sysInfo.sampleRate = audioCfg.sampleRate;
        sysInfo.periodFrames = audioCfg.periodFrames;
        sysInfo.xruns = 0;
        sysInfo.midiPort = "Akai LPD8 MIDI 1";
        sysInfo.oledHardware = true;
        sysInfo.displayDriver = "KMSDRM/SPI-480x320";

        for (int blk = 0; blk < 750; ++blk) {
            engine.processStereo(inMono.data(), outStereo.data(), 256);
            if (blk % 4 == 0) {
                ui.render(canvas, sysInfo);
            }
        }

        const char* names[5] = {
            "page1_perform.png",
            "page2_voice_fx.png",
            "page3_drone_master.png",
            "page4_noise_seq.png",
            "page5_bio_sys.png"
        };
        for (int p = 0; p < 5; ++p) {
            ui.setPage(static_cast<biomus::ui::UiPage>(p));
            ui.render(canvas, sysInfo);
            const std::string outPath = exportScreensDir + "/" + names[p];
            if (!canvas.savePng(outPath)) {
                std::cerr << "[UI] Failed to save " << outPath << "\n";
                return 1;
            }
            std::cout << "[UI] Exported " << outPath << " (480x320)\n";
        }

        ui.setPage(biomus::ui::UiPage::Perform);
        ui.setHelpVisible(true);
        ui.render(canvas, sysInfo);
        canvas.savePng(exportScreensDir + "/overlay_help.png");
        std::cout << "[UI] Exported " << exportScreensDir << "/overlay_help.png (480x320)\n";
        return 0;
    }

    // 1. Start Audio Backend (Hardware ALSA full-duplex -> fallback to 48 kHz RT Simulator)
    audioCfg.preferSimulated = !preferAlsa;
    std::unique_ptr<biomus::IAudioBackend> audio = biomus::createAutoAudioBackend(engine, audioCfg);

    // 2. Start MIDI Input (Akai hardware or virtual ALSA sequencer port)
    biomus::MidiController midi(engine);
    midi.start();

    // 3. Start Auxiliary 128x64 I2C OLED Display (15 Hz)
    biomus::OledDisplay oled(engine);
    oled.start(i2cDev);

    // 4. Start Primary Small-Screen Native Display (SDL2 KMSDRM/Window or /dev/fb0 SPI TFT)
    std::unique_ptr<biomus::ui::IDisplayBackend> display;
    if (!headless) {
        display = biomus::ui::createAutoDisplayBackend(dispOpts);
        if (!display) {
            std::cout << "[UI] No SDL2 display or /dev/fb0 detected; continuing in headless OLED/MIDI mode.\n";
        }
    }

    biomus::ui::InstrumentUi ui(engine);
    biomus::ui::Canvas canvas(biomus::ui::InstrumentUi::kLogicalW,
                              biomus::ui::InstrumentUi::kLogicalH);

    biomus::ui::SystemStatusInfo sysInfo;
    sysInfo.audioBackend = audio ? audio->backendName() : "None";
    sysInfo.sampleRate = audioCfg.sampleRate;
    sysInfo.periodFrames = audioCfg.periodFrames;
    sysInfo.midiPort = midi.connectedPortName();
    sysInfo.oledHardware = oled.hasHardware();
    sysInfo.displayDriver = display ? display->driverName() : "Headless";

    std::cout << "============================================================\n"
              << "  VOICE BOARD — BIO MODULATION + NOISE (Autonomous Native)  \n"
              << "============================================================\n"
              << "  Audio Backend : " << sysInfo.audioBackend
              << " (" << audioCfg.sampleRate << " Hz, " << audioCfg.periodFrames << " frames)\n"
              << "  MIDI Port     : " << sysInfo.midiPort << "\n"
              << "  Primary UI    : " << sysInfo.displayDriver << " [480x320 @ 30 FPS]\n"
              << "  Aux 128x64    : " << (oled.hasHardware() ? ("I2C " + i2cDev) : "1-bit Framebuffer (15 Hz)") << "\n"
              << "  Press 1..5 to switch screens, I/? for controls, Esc/Ctrl+C to quit.\n"
              << "============================================================\n"
              << std::flush;

    // 30 FPS (33.3 ms) native UI loop on main thread
    const auto tStart = std::chrono::steady_clock::now();
    constexpr auto kFramePeriod = std::chrono::milliseconds(33);
    auto nextTick = tStart;

    while (!gQuit.load(std::memory_order_relaxed)) {
        if (display) {
            if (!display->pollEvents(ui)) {
                break;
            }
            sysInfo.xruns = audio->xrunCount();
            ui.render(canvas, sysInfo);
            display->present(canvas);
        }

        if (maxRunSeconds > 0.0) {
            const double elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - tStart).count();
            if (elapsed >= maxRunSeconds) break;
        }

        nextTick += kFramePeriod;
        std::this_thread::sleep_until(nextTick);
    }

    std::cout << "\n[System] Shutting down cleanly (ALSA xruns: " << audio->xrunCount() << ")...\n";
    if (display) display->shutdown();
    oled.stop();
    midi.stop();
    audio->stop();
    return 0;
}
