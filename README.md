# MJR Amp

Open source guitar amp plugin (VST3 / AU / Standalone) for Linux, Windows and macOS,
built on [Neural Amp Modeler](https://github.com/sdatkinson/NeuralAmpModelerCore) captures
and [JUCE](https://juce.com).

No captures, impulse responses or images are bundled: point the plugin at your own
NAM capture library.

## Build

```sh
git clone --recursive <repo>
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

x86-64 builds use AVX2+FMA (any CPU from 2013 on; NAM runs about 25-30% faster). For older CPUs: `-DMJR_AVX2=OFF`.
`MJR_BENCH_ONLY=1 build/mjr-test <library dir> <DI wav>` prints only the CPU benchmark.

Linux deps (Debian/Ubuntu): `libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev
libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libxi-dev libgl1-mesa-dev`.

## Linux standalone

Run it on the host (not inside a build container), through PipeWire's JACK:

```sh
pw-jack "build/MJRAmp_artefacts/Release/Standalone/MJR Amp"
```

Then **AUDIO** > Audio device type **JACK**, pick the interface, and choose the guitar's
input with the **INPUT CH** switch. Close other apps that hold the interface directly.

## Tuner and metronome

- **TUNER** replaces the main view with a chromatic tuner (A4 = 440 Hz) and mutes the amp.
- **METRO** opens the metronome: tempo, tap tempo, beats per bar (accented downbeat), level.
  In a DAW it follows the host tempo and bar position while the transport is playing.

## Amp models

- **CIRCUIT** (default): a model of the 1968 200 W head's circuit, built from its service drawing
  (`src/CircuitAmp.h`): triode stages with bias shift, the plate-driven tone stack solved exactly,
  4x KT88 ultra-linear power amp with global feedback and Presence. The knobs act as on the real
  panel. Click the input jacks to plug into I (bright), II, or I jumped to II. Needs no captures.
- **CAPTURE**: the NAM captures below.

## Capture library

Choose the folder with your `.nam` captures and IRs from **Library...**. The captures
expected and their recorded knob settings are listed in `data/captures.json`: when a
capture is selected the knobs move to those settings, and turning them away adds gain
and EQ relative to the capture.

Optional local artwork: put a front photo of the head (1034 x 390, transparent background) at `assets-local/amp.png` (git-ignored)
before configuring and it replaces the drawn panel.

## Tests

```sh
build/mjr-test <library dir> libs/NeuralAmpModelerCore/example_audio/input.wav [snapshot dir]
```

## License

AGPL-3.0 (JUCE is used under its AGPLv3 option). NeuralAmpModelerCore is MIT. The effects are Airwindows plugins (MIT, Chris Johnson), vendored in `libs/airwindows` with their LICENSE: Dynamics3 (compressor), TapeDelay2 (delays), StereoChorus (chorus), Galactic3 (reverb), Doublelay (doubler). The graphic EQ is our own (JUCE peak filters).
