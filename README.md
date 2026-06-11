# MISSVECTOR'S GALLERY OF THE NOISE

Three noise synthesizers in a 2×2 grid. This is how it works:

Three independent engines sharing one audio context:

| Engine | Synthesis | Oscillators | Filter | FX |
|--------|-----------|-------------|--------|-----|
| **WHITE** | Filtered pink noise | None | Lowpass + bandpass | Distortion, delay feedback, convolution reverb |
| **RED** | Ring-modulated sub bass | Sawtooth (30–130Hz) × Sine (80–480Hz) | Lowpass Q=4 | Dual-stage tanh distortion, delay, reverb |
| **BLUE** | Resonant swarm | Sine/Triangle (150–850Hz), up to 10 voices | Lowpass Q=8–14 | Delay feedback, long convolution reverb |

## Controls

Per channel: **vol** (0–100%); **dyn** (touch sensitivity)

## Stack

- Vanilla JS
- Web Audio API: `OscillatorNode`, `BiquadFilterNode`, `WaveShaperNode`, `DelayNode`, `ConvolverNode`, `GainNode`, `StereoPannerNode`
- Shared `AudioContext`, each engine self-contained
- Canvas 2D for visual feedback (single radial gradient dot per engine)
- CSS Grid 2×2 layout, rotated range inputs as vertical faders
- Pointer events for touch/mouse
- Random walk drives all parameters when playing

## Run

Open `index.html` in any browser. Click the windows.
