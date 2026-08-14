---
name: juce-plugin-build
description: Use when building, compiling, or debugging JUCE/C++ audio plugins (VST3, Standalone) on Windows - CMake FetchContent setup, MSVC toolchain, headless smoke tests, FL Studio install. Trigger keywords: JUCE, VST3, audio plugin, mixing plugin, CMake, DSP, FL Studio.
---

# JUCE Plugin Build (Windows)

Workflow for shipping JUCE audio plugins from a clean Windows box.

## Toolchain
- CMake: `C:\Program Files\CMake\bin\cmake.exe` (install: `winget install Kitware.CMake`). Per-session PATH: `$env:PATH += ";C:\Program Files\CMake\bin"`.
- MSVC via Visual Studio (verify: `vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`). Build with `cmake --build build --config Release` (MSBuild, no vcvars needed).
- git required for FetchContent.

## Project skeleton
- CMakeLists: FetchContent JUCE pinned: `FetchContent_Declare(juce GIT_REPOSITORY https://github.com/juce-framework/JUCE.git GIT_TAG 8.0.9 GIT_SHALLOW TRUE)` + `juce_add_plugin(Target PRODUCT_NAME ... FORMATS VST3 Standalone ...)`.
- Non-plugin targets (console smoke tests) need `juce_generate_juce_header(Target)` and explicit `target_link_libraries(... juce::juce_audio_basics juce::juce_audio_processors juce::juce_dsp ...)`.
- Layout: `Source/PluginProcessor.*`, `Source/PluginEditor.*`, `Source/DSP/*.h` (header-only modules, namespace agm), `Source/UI/*.h`, `Tests/SmokeTest.cpp`.

## Build + verify loop
```
cmake -B build -S .
cmake --build build --config Release -j
build\MixAgentSmokeTest_artefacts\Release\MixAgentSmokeTest.exe
cmake --install build --config Release
```
Install drops VST3 into `C:\Program Files\Common Files\VST3` - FL Studio scans this on startup or via Options > Manage plugins > Find installed plugins.

## Pitfalls
- JUCE 8 = C++17. CMake 4 OK with JUCE >= 8.0.6.
- VST3 SDK is bundled inside JUCE - no separate download.
- Zipper noise: smooth continuous params per-sample (exponential approach) or `agm::SmoothBypass` from `Source/DSP/Common.h` for bypass ramps.
- Lookahead processors must report `getLatencySamples()`.
- No allocation/logging inside `processBlock`. Use `juce::ScopedNoDenormals`.
- `PLUGIN_MANUFACTURER_CODE`/`PLUGIN_CODE` are exactly 4 chars.
- Metering/analyzer data crosses threads via `std::atomic<float>`.
- JUCE licensing: GPLv3 or commercial - flag to user.

## DSP module convention
Header-only `class X` in `Source/DSP/X.h`: `prepare(double sr, int block)`, `reset()`, `process(juce::AudioBuffer<float>&)` (handles 1 or 2 channels), `setEnabled(bool)` (click-free via SmoothBypass), plain setters for params. Float DSP, no NaN, no allocation.

## Testing without audio hardware
Headless console test: `juce::ScopedJuceInitialiser_GUI`, instantiate processor, `prepareToPlay`, run sines through `processBlock`, assert: bypass transparency, EQ gain accuracy, compressor GR > 0 under hot input, limiter ceiling respected, no NaN with extreme settings, preset/state roundtrip.
