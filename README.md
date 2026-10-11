# MixAgent

JUCE 8.0.9 VST3/Standalone instrument and mixing strip: EQ, oversampled saturation,
compressor, stereo imaging, delay, reverb, limiter, synthesized instruments and drums.

## Build on Windows

Install Visual Studio with **Desktop development with C++**, CMake 3.24 or newer,
Git and Python 3.11 or newer. From the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DMIXAGENT_COPY_PLUGIN_AFTER_BUILD=OFF
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure --no-tests=error --repeat until-fail:2
```

The VST3 is under `build/MixAgent_artefacts/Release/VST3/MixAgent.vst3`.
Copy the entire `.vst3` directory to `C:\Program Files\Common Files\VST3`, then
rescan plugins in your DAW. The standalone executable is under
`build/MixAgent_artefacts/Release/Standalone`. GitHub Actions builds Windows
and Linux artifacts and runs the same tests on pull requests and pushes to master.
JUCE is pinned to commit `f72bad64d29715216226685810c5196bd0d79d77`.
See [native builds and release gates](docs/BUILD.md) for Linux, repeatable
stress/benchmarks and complete package verification.

## Playing and mixing

- Melodic instruments accept MIDI channels 1–9 and 11–16 over the full note range.
- Drums use General MIDI channel 10. Older sessions that put percussion on
  channel 1 should move that MIDI to channel 10.
- The mouse keyboard plays the selected instrument; **AUDITION** plays a 300 ms
  preview. Selecting or automating a program does not insert extra notes.
- Favorites and category filters keep stable instrument identities.
- Double-click a knob to restore its actual parameter default.
- Factory presets always start from defaults; **Init** resets the strip.
- The oversampling and limiter paths retain their latency during bypass, so
  host delay compensation remains consistent.

## Verification

CTest runs processor smoke tests, DSP regressions, an offscreen editor probe,
and a host test that loads the actual compiled VST3 bundle through JUCE.
The DSP suite checks mono/stereo processing at 44.1, 48 and 96 kHz, oversized
blocks, non-finite input, fixed oversampling delay and limiter transients.
On Linux it also measures `malloc`/`calloc`/`realloc` during drum, instrument and
saturation rendering. The processor suite covers timestamped MIDI, program/state
recall, UI queue overflow, and gain smoothing across different block sizes.
The editor probe verifies filtered program selection and default reset values,
then writes `build/mixagent-ui.png`.
The VST3 host test scans the factory, validates stereo dry audio against reported
latency, sends timestamped melodic and percussion MIDI through the wrapper,
checks note release and parameter state recall, and creates/destroys the editor
before unloading. It runs without attaching a native editor window.

The deterministic stress target adds adversarial callback sizes, automation,
sample-rate changes, sustained MIDI voices and state/lifecycle checks with
real audio oracles. Its `--benchmark` mode repeats fixed workloads and reports
callback distributions and output hashes without timing pass/fail thresholds.
The actual VST3 host runs twice behind a guard that validates native structures,
architecture, vendor and class identities, and checks every bundle file's hash
before and after loading. Packaging tests cover incomplete/corrupt releases and
native host exit propagation.

Native DAW validation on your target OS remains necessary before a release.
