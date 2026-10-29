# Building ElukkaSynth

ElukkaSynth is a JUCE 8 / CMake project. It builds a VST3 instrument (`ElukkaSynth_VST3`) and an
offline test program (`ElukkaTest`). There is no standalone app.

JUCE is a git submodule (`JUCE/`, pinned to 8.0.4). After cloning run:

```bash
git submodule update --init --recursive
```

## macOS (universal arm64 + x86_64)

Xcode command line tools and CMake 3.22+.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ElukkaSynth_VST3 ElukkaTest -j8
./build/ElukkaTest_artefacts/Release/ElukkaTest
```

The VST3 is copied into `~/Library/Audio/Plug-Ins/VST3` after building (turn off with
`-DELUKKA_COPY_PLUGIN_AFTER_BUILD=OFF`).

## Linux (x86-64)

Tested on Ubuntu 22.04 with GCC. Build dependencies (Debian/Ubuntu names):

```bash
sudo apt install build-essential cmake pkg-config \
    libasound2-dev libfreetype-dev libfontconfig1-dev \
    libx11-dev libxext-dev libxrandr-dev libxinerama-dev libxcursor-dev libgl1-mesa-dev
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ElukkaSynth_VST3 ElukkaTest --parallel
./build/ElukkaTest_artefacts/Release/ElukkaTest       # needs an X display; use xvfb-run headless
```

Result: `build/ElukkaSynth_artefacts/Release/VST3/ElukkaSynth.vst3/` (copy it to `~/.vst3/`).
The Linux build enables JUCE's MP3 decoder (`JUCE_USE_MP3AUDIOFORMAT=1`) because, unlike macOS and
Windows, Linux has no system codec JUCE can use.

## Windows (x64)

Visual Studio 2022 with the "Desktop development with C++" workload and CMake 3.22+.

```bat
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target ElukkaSynth_VST3 ElukkaTest --parallel
```

Install the built plugin into the system VST3 folder. Run this from the repo root in PowerShell
**as Administrator** (the target folder is under `Program Files`):

```powershell
Copy-Item -Recurse -Force "build\ElukkaSynth_artefacts\Release\VST3\ElukkaSynth.vst3" "C:\Program Files\Common Files\VST3\"
```

Result: the bundle is installed as `C:\Program Files\Common Files\VST3\ElukkaSynth.vst3\`, and the
build copy remains at `build\ElukkaSynth_artefacts\Release\VST3\ElukkaSynth.vst3\`. To run the
offline tests, use `build\ElukkaTest_artefacts\Release\ElukkaTest.exe`. The C++ runtime is linked
statically, so testers do not need the VC++ redistributable. Plugin copying after build is off by
default on Windows and Linux.

## Packaging a beta ZIP

```bash
scripts/package.sh windows build/ElukkaSynth_artefacts/Release/VST3/ElukkaSynth.vst3 dist
scripts/package.sh linux   build/ElukkaSynth_artefacts/Release/VST3/ElukkaSynth.vst3 dist
scripts/package.sh macos   build/ElukkaSynth_artefacts/Release/VST3/ElukkaSynth.vst3 dist   # on a Mac
```

This writes `dist/ElukkaSynth-v<version>-<Windows-x64|Linux-x64|macOS-universal>.zip` containing the bundle and a
tester README (`packaging/README-*.txt`). The version comes from `project(... VERSION)` in
`CMakeLists.txt`.

## Continuous integration

`.github/workflows/build.yml` builds Windows x64 (MSVC 2022) and Linux x64 (Ubuntu 22.04, GCC) on
every push, checks the architecture and bundle layout, uploads the two ZIPs as workflow artifacts
and runs `ElukkaTest`. Pushing a tag such as `v1.0.0` additionally creates a GitHub release with the
ZIPs attached (marked as a pre-release). macOS is built and packaged locally as before, is not part of CI, and
its ZIP is added to the release by hand:

```bash
gh release upload v<tag> dist/ElukkaSynth-v<version>-macOS-universal.zip
```
