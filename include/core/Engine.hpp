#pragma once

#include "core/BioSignals.hpp"
#include "core/Commands.hpp"
#include "core/DroneSynth.hpp"
#include "core/Effects.hpp"
#include "core/NoiseSynth.hpp"
#include "core/RingBuffer.hpp"
#include "core/Smoother.hpp"
#include "core/State.hpp"

#include <array>
#include <memory>
#include <mutex>
#include <vector>

namespace biomus {

// ============================================================================
// Core Real-Time Audio Engine (Spec Sections 4.1, 4.4, 4.5)
// Zero dynamic allocations, zero blocking locks inside process() / processStereo().
// ============================================================================
class Engine {
public:
    Engine();
    ~Engine() = default;

    void prepare(double sampleRate, int maxBlockSize);
    void reset();

    // Spec 4.1 / 4.4 mono callback: processes `frames` from `in` to `out`
    void process(const float* in, float* out, int frames) noexcept;

    // Stereo output callback (mono voice input -> stereo interleaved [L, R, L, R...])
    void processStereo(const float* inMono, float* outStereoInterleaved, int frames) noexcept;

    // Thread-safe command enqueue from MIDI / WebSocket / GPIO threads
    bool enqueueCommand(const Command& cmd) noexcept;

    // Wait-free snapshot read for OLED (10-20 Hz) & WebSocket (30 Hz) threads
    FullState readSnapshot() const noexcept { return snapshot_.read(); }

    // Direct accessors for single-threaded offline tests / WAV renderer
    const FullState& state() const noexcept { return state_; }
    FullState& mutableState() noexcept { return state_; }
    void syncSmoothersToState() noexcept;

    // Enable/disable internal bio mockup generator (on by default per Stage 1/2)
    void setBioMockupEnabled(bool enabled) noexcept { bioMockupEnabled_ = enabled; }
    bool bioMockupEnabled() const noexcept { return bioMockupEnabled_; }

    double sampleRate() const noexcept { return sampleRate_; }
    int maxBlockSize() const noexcept { return maxBlockSize_; }

private:
    void drainCommands() noexcept;
    void updateSmoothedTargets(int frames) noexcept;
    void generateSyntheticFileVoice(float* dst, int frames) noexcept;

    double sampleRate_ = 48000.0;
    int maxBlockSize_ = 512;
    bool bioMockupEnabled_ = true;

    FullState state_;
    SeqLockSnapshot<FullState> snapshot_;

    // Multi-producer mutex ONLY on non-RT threads; RT thread pops lock-free
    std::mutex producerMutex_;
    SpscRingBuffer<Command, 1024> commandQueue_;

    // Voice Effects (11 instances indexed by VoiceFxId)
    PanEffect voicePan_;
    HpfEffect voiceHpf_;
    CompEffect voiceComp_;
    FoldEffect voiceFold_;
    BitcrushEffect voiceBitcrush_;
    RingModEffect voiceRingMod_;
    ChorusEffect voiceChorus_;
    ReverbEffect voiceReverb_;
    ShimmerEffect voiceShimmer_;
    DelayEffect voiceDelay_;
    LpfEffect voiceLpf_{false};
    std::array<Effect*, kNumVoiceFx> voiceFxPtrs_{};

    // Drone Effects (7 instances indexed by DroneFxId)
    LpfEffect droneLpf_{true};
    DriveEffect droneDrive_;
    FoldEffect droneFold_;
    BitcrushEffect droneBitcrush_;
    RingModEffect droneRingMod_;
    ChorusEffect droneChorus_;
    ReverbEffect droneReverb_;
    std::array<Effect*, kNumDroneFx> droneFxPtrs_{};

    // Generators
    BioSignalGenerator bioGen_;
    DroneGenerator droneGen_;
    NoiseSynthEngine noiseEngine_;

    // Master LPF & Feedback Delay line
    TptStateVariableFilter masterLpfL_;
    TptStateVariableFilter masterLpfR_;
    std::vector<float> fbDelayBuf_;
    size_t fbWritePos_ = 0;
    size_t fbDelayLen_ = 1776; // ~37 ms at 48 kHz
    float fbDcX1_ = 0.0f;
    float fbDcY1_ = 0.0f;

    // 30 ms One-Pole Parameter Smoothers (indexed by ModTarget flat index: 76 floats)
    std::array<OnePoleSmoother, kNumModTargets> smoothers_{};

    // Pre-allocated RT scratch buffers (sized to maxBlockSize_ in prepare())
    std::vector<float> voiceBuf_;
    std::vector<float> droneBuf_;
    std::vector<float> fbInputBuf_;
    std::vector<float> noiseMonoBuf_;
    std::vector<float> noiseLeftBuf_;
    std::vector<float> noiseRightBuf_;

    // Synthetic vocal formant phrase generator when `fileOn == true`
    double filePhase_ = 0.0;
    double fileEnvPhase_ = 0.0;
    TptStateVariableFilter formant1_;
    TptStateVariableFilter formant2_;

    // Peak/RMS level follower for smooth 12-segment LED meter
    float meterEnv_ = 0.0f;
};

} // namespace biomus
