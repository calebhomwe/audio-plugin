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
| `MixAgentAuditTest` | every control moved from min to max against a fixed stereo+MIDI probe; `operator new` armed only across `processBlock`; 1000× state round-trip; garbage/truncated/future state; a per-parameter click hunt; host-preset integrity across all 132 ordered preset pairs; the reported tail length against the measured one |
| `MixAgentCharacterTest` | module-level measurement: compressor transfer curve/ratio/knee/attack/make-up, limiter latency and 8×-measured true peak, saturation unity gain/harmonic series/aliasing, biquad response against its analytic transfer function, delay interpolator and wobble, reverb late-field flatness and decorrelation, imager width mapping, every drum voice's spectrum/decay/velocity response, instrument tuning/aliasing/release/voice stealing |
| `EditorProbe` | constructs the editor, drives `resized()`, the program ComboBox, all 45 knobs and all 12 toggles; asserts every continuous parameter has a control, every knob's readout changes across its range, double-click returns each knob to its parameter's default, and that simply opening the editor and running the message loop changes no parameter |

Run the suites after any DSP change. `AUDIT.md` records what has been measured, what is still
open and what could not be verified headless. `CHANGELOG.md` records what changed and when.

CI (`.github/workflows/ci.yml`) runs the same steps on every push and pull request on
ubuntu-24.04 with a cached JUCE clone.

---

## Controls

**Top / global** — `IN` and `OUT` trim, ±24 dB, beside the input and output meters.

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
factor (a square and a sine at the same peak level land 2.1 dB apart). The RMS window slows the
level rise, so the Attack knob is calibrated against it: measured time to 63 % of the final
gain reduction is 10.33 ms for a 10 ms setting, 25.33 for 25 and 49.83 for 50 — the printed
value holds within 3.3 % from 10 ms up and within 6 % from 3 ms up. Below about 2 ms the knob
saturates, because the RMS window puts a floor there that no mapping can lift. See `AUDIT.md` N8.

**Imager** — `Width` 0…200 % (measured side gain: 0.000 at 0 %, 0.500 at 50 %, 0.999 at 100 %,
1.998 at 200 % — 100 % really is unaltered), `Balance` −1…+1 with a constant-power cosine law,
`Mono` folds to mono.

**Delay** — `Time` 20…2000 ms, `Feedback` 0…0.95, `Mix`, `Damp` (a one-pole low-pass inside the
feedback loop) and `Width` (ping-pong cross-feed). The read head carries a permanent 0.4 Hz
wobble whose depth follows the delay time: 1.6 cents peak-to-peak at 100 ms, 13.5 cents at
2000 ms. It is deliberate tape/BBD character and there is no control for it.

**Reverb** — `Size` 0.1…1.0 scales the tank, `Decay` 0.2…10 s is the low-frequency RT60,
`Damp` shortens the highs, `Width`, `Mix`, `PreDelay` 0…250 ms (in front of the tank, so it sets
the gap before the onset and leaves a ringing tail alone). The tank is 16 mutually-prime comb
lines spread over a 1:1.95 range, eight per stereo half, into four unity-gain Schroeder
allpasses. The late field is flat to within 2.35 dB from 125 Hz to 8 kHz on a six-seed mean
(3.11 dB worst); the reverb stage costs 0.76–0.88 % of one core at 48 kHz in blocks of 512.

**Limiter** — `Ceiling` −20…0 dB, `Attack` 0.01…10 ms, `Release` 10…500 ms. A 3 ms lookahead
with a 4× polyphase true-peak sidechain; measured with an independent 8× interpolator the output
stays *under* the ceiling on inter-sample-peak material.

**Instruments and drums** — the saw oscillators are band-limited (PolyBLEP), so fold-down
aliasing at C7 stays below −75 dBc. `Inst On`, `Instrument` (the 16 programs), `Inst Level` −40…+6 dB,
`DRUMS` −40…+6 dB. The 12 pads under the FX panels are a C3–B3 preview keyboard and always
play the instrument, never the drums.

Readouts carry units: frequencies in Hz, times in ms, levels in dB, and the 0…1 controls
(Drive, Mix, Feedback, Damp, Width, Size) as a percentage. Double-clicking a knob returns it to
its parameter's default.

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
   unconditional window is a compatibility shim. It is left as-is, and the reason is worth
   stating precisely: nothing this plugin saves contains a note number (the state is 58
   parameter values, a host program index and a favourites list of program indices), so there is
   no plugin state to migrate and a version hint would buy nothing. The note numbers live in the
   **host's MIDI clips**, which the plugin cannot read, version or even detect — so a bass part
   written on channel 1 between B0 and C#2 would silently become a synth part with no way to warn
   anyone. (`PluginProcessor.cpp:312`)
4. **No user WAV loading.** A "bring your own one-shots" slot (decode on the message thread into
   RAM, then play from a voice) is designed but not built.
5. **`-Wold-style-cast` is not adopted.** 1002 numeric C-style casts are the codebase's style;
   `-Wall -Wextra -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual -Wunused` is clean.
6. **Reverb late-field tilt, reduced but not gone.** Rebuilding the comb bank as 16
   mutually-prime lines over a 1:1.95 range took the worst-band deviation from a 4.28 dB
   six-seed mean to 2.35 dB, and L/R correlation is a wash (0.144 → 0.152 on the same six
   seeds). Two figures did not improve: the correlation, marginally, and the mono-sum path's
   mean (3.61 → 4.21 dB, though its seed-to-seed range collapses from 2.84 dB to 0.58 dB).
   Going below about 2 dB looks like an FDN rather than more Schroeder combs. Note that this
   measurement is strongly seed-dependent, so single-seed figures should not be trusted:
   the unmodified build ranged 2.55–5.76 dB across six noise seeds.
7. **Reverb voicing changed in wave 4.** Parameter IDs, ranges and defaults are untouched and
   every session loads identically, but the comb bank is a different room and the wet level is
   held only to within 0.39 dB (typically 0.11 dB). Presets dialled in by ear may want a
   re-listen.
8. **Compressor Attack saturates below about 2 ms.** The knob is calibrated so the printed value
   is the measured attack from 3 ms up, but the detector's 8 ms RMS branch — which is what keeps
   the gain reduction crest-independent — puts a floor of roughly 2 ms on how fast it can rise.
   The calibration is also tied to a reference condition: the measured figure still varies with
   how deep the gain reduction is (14.8 ms at 18 dB of GR against 29.3 ms at 4.5 dB) and with
   frequency (13.8 ms at 100 Hz against 20.5 ms at 5 kHz), as it does for any log-domain
   one-pole detector.
9. **Reverb parameter smoothing follows the host's block size** (`paramCoef` is derived from the
   block size promised in `prepare`, and `updateCoefficients()` runs once per `process()` call).
10. **Nothing has been verified in a real host.** No DAW, no `pluginval` and no audio device were
   available; `EditorProbe` proves the editor's wiring, not its appearance.

## Licence

Copyright (C) 2026 Caleb

This program is free software: you can redistribute it and/or modify it under
the terms of the GNU Affero General Public License as published by the Free
Software Foundation, either version 3 of the License, or (at your option) any
later version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>.

See [`LICENSE`](LICENSE) for the full text and
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for the licences of the
dependencies this project builds against.
