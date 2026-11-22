# Building ModdMudd

Requirements: CMake 3.22+, a C++20 compiler, Git. JUCE 9.0.6 is downloaded automatically, pinned to that tag.

## Linux (64-bit)

Tested on Ubuntu 24.04 with GCC 13.

```sh
sudo apt-get install build-essential cmake ninja-build git \
  libasound2-dev libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev \
  libxi-dev libxcomposite-dev libxrender-dev libfreetype-dev libfontconfig1-dev libgl1-mesa-dev

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

JUCE 9 needs `libxi-dev` (XInput multitouch) in addition to JUCE 8's usual list.

Install the bundles:

```sh
mkdir -p ~/.vst3
cp -r "build/ModdMuddFX_artefacts/Release/VST3/ModdMudd FX.vst3" ~/.vst3/
cp -r "build/ModdMuddInstrument_artefacts/Release/VST3/ModdMudd Instrument.vst3" ~/.vst3/
```

Or configure with `-DMODDMUDD_COPY_AFTER_BUILD=ON`.

## macOS

Requires Xcode command line tools. The build is universal (arm64 + x86_64) with a minimum of macOS 11.

```sh
brew install cmake ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
cp -r "build/ModdMuddFX_artefacts/Release/VST3/ModdMudd FX.vst3" ~/Library/Audio/Plug-Ins/VST3/
cp -r "build/ModdMuddInstrument_artefacts/Release/VST3/ModdMudd Instrument.vst3" ~/Library/Audio/Plug-Ins/VST3/
```

For a single architecture, pass `-DCMAKE_OSX_ARCHITECTURES=arm64`. To distribute builds you will need to sign and notarize them; that isn't set up yet.

## Windows (x64)

Requires Visual Studio 2022 or newer with the "Desktop development with C++" workload (MSVC and CMake), plus Git. CMake picks the newest installed Visual Studio.

```bat
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Install by copying both bundles into `C:\Program Files\Common Files\VST3`:

- `build\ModdMuddFX_artefacts\Release\VST3\ModdMudd FX.vst3`
- `build\ModdMuddInstrument_artefacts\Release\VST3\ModdMudd Instrument.vst3`

ModdMudd keeps its plug-in list in `%APPDATA%\ModdMudd` and presets in `Documents\ModdMudd\Presets`.

## Options

| Option | Default | |
|---|---|---|
| `MODDMUDD_BUILD_TESTS` | ON | unit tests, test plug-ins, hosting test, UI snapshot tool |
| `MODDMUDD_COPY_AFTER_BUILD` | OFF | install bundles into the user VST3 folder |
| `MODDMUDD_JUCE_DIR` | (empty) | use a local JUCE checkout instead of downloading |

## Validation

```sh
pluginval --strictness-level 8 --validate-in-process "build/ModdMuddFX_artefacts/Release/VST3/ModdMudd FX.vst3"
```

On a Linux machine without a display, run under `xvfb-run -a`.

## Licensing

JUCE is dual-licensed under AGPLv3 and a commercial JUCE licence. If ModdMudd is distributed under terms the AGPLv3 doesn't allow (for example as closed-source commercial software), a commercial JUCE licence is required. See JUCE's `LICENSE.md`. This repository doesn't have a licence file yet; that choice belongs to the project owner.
