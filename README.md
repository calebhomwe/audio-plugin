# MixAgent

A JUCE 8 audio plugin (VST3 + Standalone, Windows and Linux) that is two things in one: a
**16-program synthesiser with a drum machine**, and the **mix chain** those sounds are played
through. Everything it makes it synthesises — there are no samples, no impulse responses and no
third-party audio content anywhere in the build, so every factory sound is the project's own.

```
MIDI ch 10, or notes 35-49 on any channel ──→ DrumEngine  ─┐
everything else, plus every UI pad         ──→ InstrumentBank ─┴─→ synth bus (soft-limited) ─┐
                                                                                            │
host input ─→ In Gain ─→ EQ ─→ Saturation ─→ Compressor ─→ Imager ─→ Delay ─→ Reverb ─→ Limiter ─→ Out Gain ─→ out
                        ▲
                        └── the instruments feed the chain like an insert
```

- **Instrument bank** — `Source/DSP/InstrumentBank.h`. 16 programs (Pluck, Bell, Keys, Lead, Pad,
  Brass, Strings, E.Piano, Organ, Sub, Dark Bell, Glass Pluck, Vox Choir, Soft Soul, Bounce, Rage),
  24 voices, chromatic, with pitch bend (±2 semitones), CC64 sustain, CC120/123 and MIDI program
  change 0–15.
- **Drum engine** — `Source/DSP/DrumEngine.h`. Synthesised one-shots on a GM-style note map,
  with a per-note table so each note is its own drum: a bridged-T style pitch-dropping
  resonator for kicks (36 Hz–42 Hz body), toms (74–178 Hz) and bongos (225/310 Hz); two tuned
  shell modes plus band-passed noise for snares, the rim click and a four-burst clap; and six
  square oscillators at the mode ratios of a free circular plate (Kirchhoff plate theory)
  through a two-pole high-pass for hats, rides, the ride bell, splash, chinese cymbal and
  crashes. Velocity changes brightness and length, not just level.
- **Mix chain** — `Source/DSP/`. 7-band EQ, 4×-oversampled saturation (Tube/Tape/Soft/Exciter), a
  feed-forward compressor with a soft knee, a mid/side imager, a modulated delay, a Schroeder
  reverb and a lookahead true-peak limiter.
- **12 host presets** — six mix-chain presets (Init, Clean Master, Vocal Presence, Drum Bus Punch,
  Wide & Spacey, Warm Tape) and six that also pick an instrument (Drill Bell, Rage Lead, Jersey
  Keys, Plugg Pad, BoomBap EP, Sub Glue).

Plugin code `AgMx`, manufacturer code `Agmx`, and all parameter IDs are frozen: saved sessions keep
loading.

---

## Build

### Windows (the maintainer's workflow)

Visual Studio 2026 (cl 19.51) + CMake. JUCE 8.0.9 is cached in `build\_deps\juce-src`, so no
network access is needed. The `.bat` wrappers exist because the project path contains a space —
use them rather than calling cmake inline from PowerShell.

```bat
cmd /c "tools\build_smoke.bat"     :: configure + build the headless test app
buildDrum\MixAgentSmokeTest_artefacts\Debug\MixAgentSmokeTest.exe

cmd /c "tools\build_plugin.bat"    :: VST3 + Standalone
cmd /c "tools\build_probe.bat"     :: the editor smoke test
```

Artefacts land in `buildDrum\MixAgent_artefacts\Debug\` and, because
`MIXAGENT_COPY_PLUGIN` defaults to `ON`, the VST3 is also copied to
`C:\Program Files\Common Files\VST3\MixAgent.vst3`. A stale generator in the CMake cache means
deleting `buildDrum` and re-running.

### Linux / headless

gcc 13 or clang 18, CMake ≥ 3.24, Ninja. Point `FETCHCONTENT_SOURCE_DIR_JUCE` at a local JUCE
8.0.9 checkout to skip the download, and turn the system-folder copy off.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE-8.0.9 -DMIXAGENT_COPY_PLUGIN=OFF
cmake --build build -j3
```

Options: `MIXAGENT_COPY_PLUGIN` (default `ON`) copies the built plugin into the system plugin
folder; `MIXAGENT_SANITIZE` (default `OFF`) builds the console test apps with
`-fsanitize=address,undefined`.

---

## Tests

Four headless targets, all registered with CTest. They need no audio device; the editor probe
needs a display, and CMake wires it through `xvfb-run` when that is available.

```sh
ctest --test-dir build --output-on-failure     # all four

./build/MixAgentSmokeTest_artefacts/Release/MixAgentSmokeTest          # end-to-end plugin behaviour
./build/MixAgentAuditTest_artefacts/Release/MixAgentAuditTest          # dead knobs, allocations, torture, presets
./build/MixAgentCharacterTest_artefacts/Release/MixAgentCharacterTest  # per-module characterisation
xvfb-run ./build/EditorProbe_artefacts/Release/EditorProbe             # editor construction + control wiring
```

| Target | What it covers |
|---|---|
| `MixAgentSmokeTest` | latency vs `getLatencySamples()`, real-time safety, NaN/Inf and 44.1–192 kHz × blocks 1–4096, instruments, drums, every FX module, state round-trip, all 12 presets |
| `MixAgentAuditTest` | every control moved from min to max against a fixed stereo+MIDI probe; `operator new` armed only across `processBlock`; 1000× state round-trip; garbage/truncated/future state; a per-parameter click hunt; host-preset integrity across all 132 ordered preset pairs |
| `MixAgentCharacterTest` | module-level measurement: compressor transfer curve/ratio/knee/attack/make-up, limiter latency and 8×-measured true peak, saturation unity gain/harmonic series/aliasing, biquad response against its analytic transfer function, delay interpolator and wobble, reverb late-field flatness and decorrelation, imager width mapping, every drum voice's spectrum/decay/velocity response, instrument tuning/aliasing/release/voice stealing |
| `EditorProbe` | constructs the editor, drives `resized()`, the program ComboBox, every knob and every toggle, and checks each one reaches its parameter |

Run the suites after any DSP change. `AUDIT.md` records what has been measured, what is still
open and what could not be verified headless. `CHANGELOG.md` records what changed and when.

CI (`.github/workflows/ci.yml`) runs the same steps on every push and pull request on
ubuntu-24.04 with a cached JUCE clone.

---

## Controls

**Top / global** — `Input` and `Output` trim, ±24 dB.

**EQ** (7 bands, all in series; a shelf or peak band at 0 dB gain is an exact bypass)

| Control | Range | Notes |
|---|---|---|
| `HP On`, `HP Freq` | 20–1000 Hz | 12 dB/oct Butterworth high-pass |
| `LS Freq`, `LS Gain` | 40–1200 Hz, ±15 dB | low shelf |
| `P1/P2/P3 Freq, Gain, Q` | 60–3000 / 150–8000 / 500–14000 Hz, ±15 dB, Q 0.2–10 | peaking bands |
| `HS Freq`, `HS Gain` | 800–20000 Hz, ±15 dB | high shelf |
| `LP On`, `LP Freq` | 500–20000 Hz | 12 dB/oct low-pass |

**Saturation** — `Mode` picks Tube (a biased, asymmetric single-ended stage: the 2nd harmonic
leads the 3rd by 7–19 dB until it is driven into hard clipping), Tape (symmetric with a 12 kHz
roll-off), Soft (cubic) or Exciter (high-band only). Every mode is within 0.04 dB of unity at
zero drive. `Drive` adds up to 24 dB of input
gain into the shaper; `Mix` blends dry/wet; `Out` trims ±12 dB. Runs at 4× and reports its
latency, which stays constant whether the module is on or off.

**Compressor** — `Thresh` −60…0 dB, `Ratio` 1…20:1, `Attack` 0.1…100 ms, `Release` 10…1000 ms,
`Knee` 0…24 dB (quadratic), `Makeup` 0…24 dB, `Mix` 0…1 for parallel compression. The detector
is a hybrid of peak and an 8 ms RMS, which makes the gain reduction nearly independent of crest
factor (a square and a sine at the same peak level land 2.2 dB apart) at the cost of the Attack
knob being approximate rather than exact — see `AUDIT.md` N8.

**Imager** — `Width` 0…200 % (measured side gain: 0.000 at 0 %, 0.500 at 50 %, 0.999 at 100 %,
1.998 at 200 % — 100 % really is unaltered), `Balance` −1…+1 with a constant-power cosine law,
`Mono` folds to mono.

**Delay** — `Time` 20…2000 ms, `Feedback` 0…0.95, `Mix`, `Damp` (a one-pole low-pass inside the
feedback loop) and `Width` (ping-pong cross-feed). The read head carries a permanent 0.4 Hz
wobble whose depth follows the delay time: 1.6 cents peak-to-peak at 100 ms, 13.5 cents at
2000 ms. It is deliberate tape/BBD character and there is no control for it.

**Reverb** — `Size` 0.1…1.0 scales the tank, `Decay` 0.2…10 s is the low-frequency RT60,
`Damp` shortens the highs, `Width`, `Mix`, `PreDelay` 0…250 ms.

**Limiter** — `Ceiling` −20…0 dB, `Attack` 0.01…10 ms, `Release` 10…500 ms. A 3 ms lookahead
with a 4× polyphase true-peak sidechain; measured with an independent 8× interpolator the output
stays *under* the ceiling on inter-sample-peak material.

**Instruments and drums** — the saw oscillators are band-limited (PolyBLEP), so fold-down
aliasing at C7 stays below −75 dBc. `Inst On`, `Instrument` (the 16 programs), `Inst Level` −40…+6 dB,
`Drum Level` −40…+6 dB. The 12 pads under the FX panels are a C3–B3 preview keyboard and always
play the instrument, never the drums.

---

## Sample content and licensing (frozen decisions — do not re-litigate)

The factory bank is **100 % DSP-synthesised**. That is a deliberate licensing decision, not an
accident: it means nothing needs attribution and nothing can be taken down.

Three downloaded kits used to sit in `Assets/Sounds/source_zips/` (≈63 MB). They were never
referenced by any source file, CMake rule or CI step, and the licence review concluded none of
them could ship:

- **GareBear99 808 kit and drum kit** — "free to use in commercial and non-commercial
  productions, no credit required, **do not redistribute the raw samples**". Embedding them in a
  plugin *is* redistribution, so they could only ever have been user-loaded, never bundled.
- **archive.org 150 bpm loop** — the Public Domain Mark is self-declared by an uploader who is
  not the author ("Downloaded from Samplefocus.com"), so the provenance is unauditable.

They were removed from the working tree in the wave-3 hygiene pass so that a clone is ~63 MB
smaller. **They are still in git history and are recoverable**:

```sh
git show bed39b7:Assets/Sounds/source_zips/garebear-808-kit.zip  > garebear-808-kit.zip
git show bed39b7:Assets/Sounds/source_zips/garebear-drum-kit.zip > garebear-drum-kit.zip
git show bed39b7:Assets/Sounds/source_zips/bpm150-trap-loop.wav  > bpm150-trap-loop.wav
```

`tools/build_assets.ps1` is the abandoned extraction script for those kits. It is kept because it
is the maintainer's, but it is unfinished: it stages the zips, builds a manifest in which every
entry's `id` and `file` are empty, and writes only file counts to the console. Nothing consumes
its output. It skips missing zips without erroring, so it still runs after the removal — it just
stages nothing.

No manufacturer's code, presets, artwork, impulse responses or trademarked names are used
anywhere in this project, and none may be added.

---

## Known issues and debt

Ranked; the measurements behind them are in `AUDIT.md`.

1. **Instrument bank shares one Recipe.** Switching program mutates voices that are still
   sounding. A per-voice recipe snapshot would be correct. (`InstrumentBank.h`)
2. **Notes 35–49 are captured by the drums on every MIDI channel**, so the instrument cannot play
   B0–C#2 from an ordinary keyboard track. Channel 10 already reaches the whole GM map; the
   unconditional window is a compatibility shim. Changing it would alter how existing sessions
   sound, so it is left as-is and documented. (`PluginProcessor.cpp:282`)
3. **Host presets are additive.** A preset only writes the parameters it mentions, so browsing
   presets layers them; "Init" resets nothing at all. 118 of the 132 ordered preset pairs land
   somewhere other than a fresh load of the same preset. (`PluginProcessor.cpp:614+`)
4. **Reverb pre-delay is applied after the tank**, so it moves the whole wet signal rather than
   only the onset. (`Reverb.h`)
5. **No user WAV loading.** A "bring your own one-shots" slot (decode on the message thread into
   RAM, then play from a voice) is designed but not built.
6. **`-Wold-style-cast` is not adopted.** 1002 numeric C-style casts are the codebase's style;
   `-Wall -Wextra -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual -Wunused` is clean.
7. **Reverb late-field tilt.** The tank is flat to within 3.22 dB from 125 Hz to 8 kHz with
   damping off. The four diffusers are not unity-gain allpasses, which looks like the cause —
   but rebuilding them as proper allpasses measured *worse* (spread 3.22 → 3.97 dB, L/R
   correlation 0.163 → 0.289). The tilt is the comb bank's sparse low-frequency modal density;
   flattening it needs an FDN, not a coefficient.
8. **Compressor Attack is approximate, by design.** The detector takes
   `max(peak, 1.414·√RMS)` with an 8 ms RMS window, which is what makes the gain reduction
   nearly independent of crest factor — and which stretches the knob: 1/10/50 ms measure
   2.83/20.33/94.83 ms to 63 % of the final reduction. Making it exact means dropping the RMS
   branch and changing how the compressor responds to everything.
9. **Nothing has been verified in a real host.** No DAW, no `pluginval` and no audio device were
   available; `EditorProbe` proves the editor's wiring, not its appearance.
