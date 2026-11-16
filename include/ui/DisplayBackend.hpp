#pragma once

#include "ui/Canvas.hpp"
#include "ui/InstrumentUi.hpp"
#include <memory>
#include <string>

namespace biomus::ui {

class IDisplayBackend {
public:
    virtual ~IDisplayBackend() = default;

    virtual bool init() = 0;
    virtual void shutdown() = 0;

    // Polls window/touch/keyboard events and dispatches to InstrumentUi.
    // Returns false if user requested exit (Escape at top-level or window close).
    virtual bool pollEvents(InstrumentUi& ui) = 0;

    // Presents a 480x320 Canvas onto physical screen / window
    virtual void present(const Canvas& canvas) = 0;

    virtual std::string driverName() const = 0;
};

struct DisplayOptions {
    std::string fbDevice;      // e.g. "/dev/fb0" or "/dev/fb1" (if non-empty, try fbdev first)
    int windowScale = 2;       // Desktop SDL2 scale (2 -> 960x640 window)
    bool fullscreen = false;   // True on RPi KMSDRM / kiosk mode
};

std::unique_ptr<IDisplayBackend> createSdl2Backend(const DisplayOptions& opts);
std::unique_ptr<IDisplayBackend> createFbdevBackend(const std::string& fbDevice);

// Auto-selects optimal backend:
// 1. Explicit --fb /dev/fbX -> FbdevBackend
// 2. SDL2 (X11/Wayland on Arch desktop, KMSDRM on Raspberry Pi OS Lite)
// 3. Fallback /dev/fb0 if present on headless Linux console
std::unique_ptr<IDisplayBackend> createAutoDisplayBackend(const DisplayOptions& opts);

} // namespace biomus::ui
