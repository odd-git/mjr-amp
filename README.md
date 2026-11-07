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

Linux deps (Debian/Ubuntu): `libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev
libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxcomposite-dev libxi-dev libgl1-mesa-dev`.

## License

AGPL-3.0 (JUCE is used under its AGPLv3 option). NeuralAmpModelerCore is MIT.
