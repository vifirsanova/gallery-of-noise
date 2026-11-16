#pragma once

#include "core/Engine.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace biomus {

// ============================================================================
// MidiController (Spec Sections 2, 4.5, 6.2)
// Wraps RtMidi (ALSA backend) + exposes deterministic MIDI byte parser for tests.
// ============================================================================
class MidiController {
public:
    explicit MidiController(Engine& engine);
    ~MidiController();

    // Opens Akai hardware port (if present) and/or virtual ALSA port "VoiceBoard MIDI In"
    bool start(const std::string& preferredPortSubstring = "Akai");
    void stop();

    bool isRunning() const noexcept { return running_.load(); }
    std::string connectedPortName() const { return portName_; }

    // Can be called directly in integration tests or from RtMidi callback
    void handleMidiBytes(const uint8_t* data, size_t length) noexcept;

private:
    Engine& engine_;
    std::atomic<bool> running_{false};
    std::atomic<int> heldPadStep_{-1};
    std::string portName_;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace biomus
