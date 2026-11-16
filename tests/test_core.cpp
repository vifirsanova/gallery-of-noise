#include "TestHarness.hpp"

#include "core/BioSignals.hpp"
#include "core/Commands.hpp"
#include "core/DroneSynth.hpp"
#include "core/Effects.hpp"
#include "core/Engine.hpp"
#include "core/JsonSerializer.hpp"
#include "core/NoiseSynth.hpp"
#include "core/RingBuffer.hpp"
#include "core/Smoother.hpp"
#include "core/State.hpp"
#include "core/WavFile.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <numeric>
#include <thread>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr double kTwoPi = 6.28318530717958647692;

std::vector<float> makeSine(size_t n, double freqHz, float amp = 0.5f) {
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i) {
        v[i] = amp * static_cast<float>(std::sin(kTwoPi * freqHz * static_cast<double>(i) / kSampleRate));
    }
    return v;
}

std::vector<float> makeDeterministicNoise(size_t n, float amp = 0.5f) {
    std::vector<float> v(n);
    uint32_t s = 0xDEADBEEFu;
    for (size_t i = 0; i < n; ++i) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        v[i] = amp * static_cast<float>(static_cast<int32_t>(s)) * (1.0f / 2147483648.0f);
    }
    return v;
}

std::vector<float> makeImpulse(size_t n, float amp = 1.0f) {
    std::vector<float> v(n, 0.0f);
    if (!v.empty()) v[0] = amp;
    return v;
}

float rmsEnergy(const float* p, size_t n) {
    if (n == 0) return 0.0f;
    double acc = 0.0;
    for (size_t i = 0; i < n; ++i) acc += static_cast<double>(p[i]) * static_cast<double>(p[i]);
    return static_cast<float>(std::sqrt(acc / static_cast<double>(n)));
}

bool allFinite(const std::vector<float>& v) {
    for (float x : v) {
        if (!std::isfinite(x)) return false;
    }
    return true;
}

} // namespace

// ============================================================================
// 1. RT-Safety & Lock-Free Primitives
// ============================================================================
TEST_CASE("RT-safe lock-free primitives and 30ms smoother", "[core][rt]") {
    REQUIRE(std::is_trivially_copyable_v<biomus::FullState>);
    REQUIRE(std::is_trivially_copyable_v<biomus::Command>);

    biomus::SpscRingBuffer<int, 16> rb;
    REQUIRE(rb.empty());
    for (int i = 0; i < 15; ++i) {
        REQUIRE(rb.push(i * 10));
    }
    REQUIRE(!rb.push(999)); // capacity-1 usable slots in lock-free ring
    for (int i = 0; i < 15; ++i) {
        int val = -1;
        REQUIRE(rb.pop(val));
        REQUIRE(val == i * 10);
    }
    REQUIRE(rb.empty());

    // 30 ms One-Pole Smoother step response check: after 1 tau (1440 samples @ 48kHz) -> ~63.21%
    biomus::OnePoleSmoother sm;
    sm.prepare(kSampleRate, 0.030f);
    sm.reset(0.0f);
    sm.setTarget(1.0f);
    float v = 0.0f;
    for (int i = 0; i < 1440; ++i) v = sm.next();
    REQUIRE(v == Approx(1.0 - std::exp(-1.0)).margin(0.005));
}

// ============================================================================
// 2. DSP Effects Unit Tests (Spec 7.1: Sine, Noise, Impulse + Reset Determinism)
// ============================================================================
TEST_CASE("All 12 DSP effects: finite output, deterministic reset, expected acoustics", "[core][dsp]") {
    constexpr int kN = 4096;

    auto verifyDeterminismAndFinite = [&](biomus::Effect& fx, float x, float y, float k0 = 0.5f, float k1 = 0.5f) {
        fx.prepare(kSampleRate, 256);
        fx.setParameters(x, y, k0, k1);

        for (const auto& src : {makeSine(kN, 440.0), makeDeterministicNoise(kN), makeImpulse(kN)}) {
            auto b1 = src;
            auto b2 = src;
            fx.reset();
            for (int off = 0; off < kN; off += 128) fx.process(b1.data() + off, 128);
            REQUIRE(allFinite(b1));

            fx.reset();
            for (int off = 0; off < kN; off += 128) fx.process(b2.data() + off, 128);
            for (int i = 0; i < kN; ++i) {
                REQUIRE(b1[static_cast<size_t>(i)] == Approx(b2[static_cast<size_t>(i)]).margin(1e-6));
            }
        }
    };

    SECTION("PAN unity at Y=0.5 and gain trim") {
        biomus::PanEffect pan;
        verifyDeterminismAndFinite(pan, 0.5f, 0.5f);
        auto sig = makeSine(512, 440.0, 0.5f);
        auto orig = sig;
        pan.prepare(kSampleRate, 512);
        pan.setParameters(0.25f, 0.5f);
        pan.process(sig.data(), 512);
        REQUIRE(pan.stereoPan() == Approx(0.25f));
        for (size_t i = 0; i < sig.size(); ++i) {
            REQUIRE(sig[i] == Approx(orig[i]).margin(1e-6));
        }
    }

    SECTION("HPF attenuates sub-bass and passes treble") {
        biomus::HpfEffect hpf;
        verifyDeterminismAndFinite(hpf, 0.5f, 0.5f, 0.707f);
        hpf.prepare(kSampleRate, 512);
        hpf.setParameters(0.6f, 0.5f, 0.707f); // cutoff ~ 316 Hz

        auto low = makeSine(2048, 35.0, 0.6f);
        auto high = makeSine(2048, 4000.0, 0.6f);
        hpf.reset();
        hpf.process(low.data(), 2048);
        hpf.reset();
        hpf.process(high.data(), 2048);

        const float rmsLow = rmsEnergy(low.data() + 512, 1536);
        const float rmsHigh = rmsEnergy(high.data() + 512, 1536);
        REQUIRE(rmsLow < rmsHigh * 0.15f);
    }

    SECTION("LPF passes bass and attenuates ultrasonic/high treble") {
        biomus::LpfEffect lpf(false);
        verifyDeterminismAndFinite(lpf, 0.4f, 0.5f, 0.707f);
        lpf.prepare(kSampleRate, 512);
        lpf.setParameters(0.35f, 0.5f, 0.707f); // ~530 Hz

        auto low = makeSine(2048, 100.0, 0.6f);
        auto high = makeSine(2048, 10000.0, 0.6f);
        lpf.reset();
        lpf.process(low.data(), 2048);
        lpf.reset();
        lpf.process(high.data(), 2048);

        REQUIRE(rmsEnergy(high.data() + 512, 1536) < rmsEnergy(low.data() + 512, 1536) * 0.10f);
    }

    SECTION("COMP reduces dynamic range between loud and quiet signals") {
        biomus::CompEffect comp;
        verifyDeterminismAndFinite(comp, 0.7f, 0.8f, 0.003f, 0.2f);
        comp.prepare(kSampleRate, 512);
        comp.setParameters(0.8f, 0.9f, 0.002f, 0.20f);

        auto quiet = makeSine(4096, 440.0, 0.05f);
        auto loud = makeSine(4096, 440.0, 0.90f); // 18x louder input
        comp.reset();
        comp.process(quiet.data(), 4096);
        comp.reset();
        comp.process(loud.data(), 4096);

        const float outRatio = rmsEnergy(loud.data() + 1024, 3072) / rmsEnergy(quiet.data() + 1024, 3072);
        REQUIRE(outRatio < 6.0f); // Significantly compressed from 18:1 input ratio
    }

    SECTION("FOLD, BITCRUSH, RINGMOD, DRIVE shape waveforms deterministically") {
        biomus::FoldEffect fold;
        biomus::BitcrushEffect crush;
        biomus::RingModEffect ring;
        biomus::DriveEffect drive;

        verifyDeterminismAndFinite(fold, 0.7f, 0.8f);
        verifyDeterminismAndFinite(crush, 0.5f, 0.9f);
        verifyDeterminismAndFinite(ring, 0.5f, 1.0f);
        verifyDeterminismAndFinite(drive, 0.8f, 0.6f);

        // RingMod at wet=1.0 on DC=1.0 outputs pure carrier sine with ~0.707 RMS
        std::vector<float> dc(2048, 1.0f);
        ring.prepare(kSampleRate, 512);
        ring.setParameters(0.5f, 1.0f);
        ring.process(dc.data(), 2048);
        REQUIRE(rmsEnergy(dc.data(), 2048) == Approx(0.7071f).margin(0.02f));
    }

    SECTION("Time-based effects (CHORUS, REVERB, SHIMMER, DELAY) produce impulse tails") {
        biomus::ChorusEffect chorus;
        biomus::ReverbEffect reverb;
        biomus::ShimmerEffect shimmer;
        biomus::DelayEffect delay;

        verifyDeterminismAndFinite(chorus, 0.3f, 0.7f, 0.6f, 0.3f);
        verifyDeterminismAndFinite(reverb, 0.85f, 0.6f, 3.0f, 8000.0f);
        verifyDeterminismAndFinite(shimmer, 0.75f, 0.7f, 0.2f, 0.0f);
        verifyDeterminismAndFinite(delay, 0.1f, 0.6f, 4000.0f, 0.5f);

        // Impulse tail check on Reverb & Delay
        auto impRev = makeImpulse(8192, 1.0f);
        reverb.prepare(kSampleRate, 512);
        reverb.setParameters(0.85f, 0.7f, 3.0f, 8000.0f);
        reverb.process(impRev.data(), 8192);
        REQUIRE(rmsEnergy(impRev.data() + 2000, 4000) > 1e-4f);

        auto impDel = makeImpulse(16384, 1.0f);
        delay.prepare(kSampleRate, 512);
        delay.setParameters(0.10f, 0.5f, 5000.0f, 0.8f); // ~140ms (~6720 samples)
        delay.process(impDel.data(), 16384);
        REQUIRE(rmsEnergy(impDel.data() + 6000, 3000) > 1e-3f);
    }
}

// ============================================================================
// 3. State Management Tests (Spec 7.1)
// ============================================================================
TEST_CASE("State presets, effect cycling, toggling, reordering, XY and knobs", "[core][state]") {
    biomus::FullState s = biomus::makeDefaultState();
    REQUIRE(s.preset == biomus::PresetId::VOICE);
    REQUIRE(s.voice.active == biomus::VoiceFxId::REVERB);

    // Check BREATH and DRIFT presets set expected parameters & modulations
    biomus::applyPreset(s, biomus::PresetId::BREATH);
    REQUIRE(s.preset == biomus::PresetId::BREATH);
    REQUIRE(s.master.fb == Approx(0.35f));
    const auto fbMod = s.modulation.get(biomus::masterModIndex(biomus::MasterParamId::Feedback));
    REQUIRE(fbMod.source == biomus::BioSource::Breath);
    REQUIRE(fbMod.depth == Approx(0.8f));

    biomus::applyPreset(s, biomus::PresetId::DRIFT);
    const auto revMod = s.modulation.get(biomus::voiceXyModIndex(biomus::VoiceFxId::REVERB, 0));
    REQUIRE(revMod.source == biomus::BioSource::EyeX);
    REQUIRE(revMod.depth == Approx(0.7f));

    // Cycle effect wrap-around
    s.voice.active = s.voice.order[0];
    biomus::cycleVoiceEffect(s, -1);
    REQUIRE(s.voice.active == s.voice.order[biomus::kNumVoiceFx - 1]);
    biomus::cycleVoiceEffect(s, +1);
    REQUIRE(s.voice.active == s.voice.order[0]);

    // Toggle & XY preservation across reorder
    s.voice.active = biomus::VoiceFxId::CHORUS;
    biomus::setVoiceXY(s, biomus::VoiceFxId::CHORUS, 0.33f, 0.77f);
    const bool prevEn = s.voice.enabled[static_cast<size_t>(biomus::VoiceFxId::CHORUS)];
    biomus::toggleVoiceEffect(s);
    REQUIRE(s.voice.enabled[static_cast<size_t>(biomus::VoiceFxId::CHORUS)] == !prevEn);

    // Move CHORUS (index 6) left and right, verify parameters preserved
    REQUIRE(s.voice.order[6] == biomus::VoiceFxId::CHORUS);
    biomus::moveVoiceEffect(s, -1);
    REQUIRE(s.voice.order[5] == biomus::VoiceFxId::CHORUS);
    REQUIRE(s.voice.xy[static_cast<size_t>(biomus::VoiceFxId::CHORUS)].x == Approx(0.33f));
    REQUIRE(s.voice.xy[static_cast<size_t>(biomus::VoiceFxId::CHORUS)].y == Approx(0.77f));

    // Knob clamping
    biomus::applyVoiceKnob(s, 0, +10.0f); // CHORUS mix max = 1.0
    REQUIRE(s.voice.knobs[static_cast<size_t>(biomus::VoiceFxId::CHORUS)][0] == Approx(1.0f));
    biomus::applyVoiceKnob(s, 0, -10.0f); // min = 0.0
    REQUIRE(s.voice.knobs[static_cast<size_t>(biomus::VoiceFxId::CHORUS)][0] == Approx(0.0f));
}

// ============================================================================
// 4. Modulation Matrix Tests (Spec 7.1 exact requirements)
// ============================================================================
TEST_CASE("Modulation effectiveValue obeys all Spec 7.1 invariants", "[core][modulation]") {
    biomus::BioState bio{};
    bio.breath = 0.5f;
    bio.heart = 1.0f;
    bio.eyeX = 0.0f;

    // 1. source == None -> returns base value regardless of bio
    REQUIRE(biomus::effectiveValue(0.6f, 0.0f, 1.0f, biomus::BioSource::None, 1.0f, bio) == Approx(0.6f));

    // 2. source == Breath and bio == 0.5 -> returns base value
    REQUIRE(biomus::effectiveValue(0.6f, 0.0f, 1.0f, biomus::BioSource::Breath, 0.8f, bio) == Approx(0.6f));

    // 3. bio == 0 and bio == 1 shifts by expected -/+ 0.5 * depth * range
    bio.breath = 0.0f;
    REQUIRE(biomus::effectiveValue(0.5f, 0.0f, 1.0f, biomus::BioSource::Breath, 0.6f, bio) == Approx(0.2f));
    bio.breath = 1.0f;
    REQUIRE(biomus::effectiveValue(0.5f, 0.0f, 1.0f, biomus::BioSource::Breath, 0.6f, bio) == Approx(0.8f));

    // Clamping at bounds
    REQUIRE(biomus::effectiveValue(0.9f, 0.0f, 1.0f, biomus::BioSource::Breath, 1.0f, bio) == Approx(1.0f));

    // 4. depth == 0 -> unchanged
    REQUIRE(biomus::effectiveValue(0.42f, 0.0f, 1.0f, biomus::BioSource::Breath, 0.0f, bio) == Approx(0.42f));
}

// ============================================================================
// 5. Noise Sequencer & Parameter Locks Tests (Spec 7.1)
// ============================================================================
TEST_CASE("Noise 16-step sequencer advances, applies parameter locks, and resets locks on next step", "[core][sequencer]") {
    biomus::NoiseState ns{};
    ns.playing = true;
    ns.tempo = 120.0f; // 16th note = 0.125 s = 6000 samples @ 48 kHz

    auto& tr = ns.tracks[0];
    tr.voice.cutoff = 1200.0f;
    tr.steps[0].trigger = true; // Base trigger on step 0 (no lock)
    // Put a parameter lock on step 1: cutoff = 7500 Hz
    tr.steps[1].trigger = true;
    biomus::setNoiseVoiceParamAndLock(tr, 1, biomus::NoiseLockField::Cutoff, 7500.0f);
    // Restore base voice cutoff back to 1200 Hz while keeping step 1's lock at 7500 Hz!
    tr.voice.cutoff = 1200.0f;
    // Step 2 is triggered WITHOUT locks -> must restore back to 1200 Hz!
    tr.steps[2].trigger = true;
    // Step 3 is NOT triggered -> must also keep unlocked base!
    tr.steps[3].trigger = false;

    biomus::NoiseSynthEngine synth;
    synth.prepare(kSampleRate);

    std::vector<float> mono(6100, 0.0f);
    // Process first 3000 samples (inside step 0)
    synth.process(3000, ns, mono.data(), nullptr, nullptr);
    REQUIRE(tr.currentStep == 0);
    REQUIRE(!synth.voiceDsp(0).hasActiveStepLocks());
    REQUIRE(synth.voiceDsp(0).activeVoiceParams().cutoff == Approx(1200.0f));
    REQUIRE(rmsEnergy(mono.data(), 3000) > 1e-4f);

    // Advance another 3100 samples -> crosses 6000-sample boundary into Step 1 (locked!)
    synth.process(3100, ns, mono.data(), nullptr, nullptr);
    REQUIRE(tr.currentStep == 1);
    REQUIRE(synth.voiceDsp(0).hasActiveStepLocks());
    REQUIRE(synth.voiceDsp(0).activeVoiceParams().cutoff == Approx(7500.0f));

    // Advance 6000 samples -> crosses into Step 2 (triggered without lock -> resets to base 1200 Hz!)
    synth.process(6000, ns, mono.data(), nullptr, nullptr);
    REQUIRE(tr.currentStep == 2);
    REQUIRE(!synth.voiceDsp(0).hasActiveStepLocks());
    REQUIRE(synth.voiceDsp(0).activeVoiceParams().cutoff == Approx(1200.0f));

    // Advance 6000 samples -> crosses into Step 3 (untriggered -> stays unlocked)
    synth.process(6000, ns, mono.data(), nullptr, nullptr);
    REQUIRE(tr.currentStep == 3);
    REQUIRE(!synth.voiceDsp(0).hasActiveStepLocks());
}

// ============================================================================
// 6. Full Audio Graph & 24-bit WAV Round-Trip Accuracy (< 1e-3, Spec 7.2)
// ============================================================================
TEST_CASE("Full Engine audio graph and 24-bit WAV golden reference comparison (< 1e-3)", "[core][wav]") {
    constexpr size_t kFrames = 4800; // 100 ms @ 48 kHz
    auto inputSine = makeSine(kFrames, 330.0, 0.4f);

    // Reference Engine pass
    biomus::Engine refEngine;
    refEngine.prepare(kSampleRate, 128);
    biomus::applyPreset(refEngine.mutableState(), biomus::PresetId::VOICE);
    refEngine.mutableState().droneOn = true;
    refEngine.syncSmoothersToState();

    std::vector<float> refOut(kFrames, 0.0f);
    for (size_t off = 0; off < kFrames; off += 128) {
        const int blk = static_cast<int>(std::min<size_t>(128, kFrames - off));
        refEngine.process(inputSine.data() + off, refOut.data() + off, blk);
    }
    REQUIRE(allFinite(refOut));
    REQUIRE(rmsEnergy(refOut.data(), kFrames) > 0.02f);

    // Save reference to 24-bit WAV, reload, and compare against fresh Engine run within 1e-3 (Spec 7.2)
    const std::string tmpWav = "/tmp/biomus_golden_ref_test.wav";
    REQUIRE(biomus::writeWav24(tmpWav, refOut.data(), kFrames, 48000, 1));

    biomus::WavData loaded;
    REQUIRE(biomus::readWav(tmpWav, loaded));
    REQUIRE(loaded.sampleRate == 48000);
    REQUIRE(loaded.channels == 1);
    REQUIRE(loaded.samples.size() == kFrames);

    biomus::Engine dutEngine;
    dutEngine.prepare(kSampleRate, 128);
    biomus::applyPreset(dutEngine.mutableState(), biomus::PresetId::VOICE);
    dutEngine.mutableState().droneOn = true;
    dutEngine.syncSmoothersToState();

    std::vector<float> dutOut(kFrames, 0.0f);
    for (size_t off = 0; off < kFrames; off += 128) {
        const int blk = static_cast<int>(std::min<size_t>(128, kFrames - off));
        dutEngine.process(inputSine.data() + off, dutOut.data() + off, blk);
    }

    float maxDiff = 0.0f;
    for (size_t i = 0; i < kFrames; ++i) {
        maxDiff = std::max(maxDiff, std::fabs(dutOut[i] - loaded.samples[i]));
    }
    REQUIRE(maxDiff < 1e-3f);
    std::filesystem::remove(tmpWav);
}

#ifndef BIOMUSIC_HAVE_CATCH2
int main() {
    int passed = 0;
    for (const auto& t : minicatch::registry()) {
        std::cout << "[RUN ] " << t.name << "\n";
        t.fn();
        ++passed;
        std::cout << "[ OK ] " << t.name << "\n";
    }
    std::cout << "All " << passed << " test suites passed.\n";
    return 0;
}
#endif
