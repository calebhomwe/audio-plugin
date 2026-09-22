# MixAgent — audit

**Status: every finding below is marked either FIXED (with the after-measurement) or
LEFT OPEN (with the reason). Summary table at the bottom.**

Headless audit on Linux (gcc 13, JUCE 8.0.9, Release). Every finding below names a
`file:line`, a mechanism, and the command whose output proves it. Things I only suspect are
in a separate list at the end and are labelled as such.

Reproduce everything with:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DMIXAGENT_COPY_PLUGIN=OFF \
      -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
cmake --build build -j3
./build/MixAgentSmokeTest_artefacts/Release/MixAgentSmokeTest      # end-to-end plugin behaviour
./build/MixAgentAuditTest_artefacts/Release/MixAgentAuditTest      # dead knobs, allocations, torture, presets
./build/MixAgentCharacterTest_artefacts/Release/MixAgentCharacterTest   # per-module characterisation
xvfb-run -a ./build/EditorProbe_artefacts/Release/EditorProbe           # editor wiring and readouts
```

25 findings: 4 CRITICAL, 8 MAJOR, 13 MINOR, plus a COSMETIC list and an unproven list.

---

## Instruments built for this audit

| Instrument | Where | What it proves |
|---|---|---|
| Dead-parameter sweep | `Tests/AuditTest.cpp` `deadKnobSuite` | all 58 controls, min vs max, RMS and peak difference in dB |
| Allocation detector | `Tests/AuditTest.cpp` `allocationSuite` | `operator new` armed only across `processBlock`, 24 rate/block combinations |
| State/automation torture | `Tests/AuditTest.cpp` `tortureSuite` | 1000x round-trip, garbage/truncated/future state, per-parameter click hunt |
| Preset integrity | `Tests/AuditTest.cpp` `presetSuite` | every ordered pair of host presets vs a fresh load |
| Reported tail | `Tests/AuditTest.cpp` `tailSuite` | `getTailLengthSeconds()` against the measured -60 dB tail |
| Module characterisation | `Tests/CharacterTest.cpp` | transfer curves, harmonic series, RT60, true peak, drum/voice spectra |

---

## CRITICAL

### C1 — Make-up gain does nothing. `Source/DSP/Compressor.h:108-110`

```cpp
float wetGain = dbToGain(mk - gr);
if (!(wetGain < 1.0f))
    wetGain = 1.0f;          // <- the make-up term can never raise the gain above unity
```

The clamp that is meant to stop the gain computer from *boosting* also throws away the
make-up gain, because make-up and gain reduction share one `wetGain`. Below threshold the
knob is inert; above threshold it can only claw back part of the reduction.

Proof (`MixAgentCharacterTest`, signal 20 dB below threshold):

```
make-up gain with the signal 20 dB below threshold:
  knob +  0.0 dB -> measured    0.00 dB
  knob +  3.0 dB -> measured    0.00 dB
  knob +  6.0 dB -> measured    0.00 dB
  knob + 12.0 dB -> measured    0.00 dB
  knob + 24.0 dB -> measured    0.00 dB
```

Four factory presets set `comp_makeup` (1.5–2.5 dB) and get nothing for it.

**FIXED.** Gain reduction and make-up are separate factors now; the clamp applies only to the
reduction. After: `0.00 / 3.00 / 6.00 / 12.00 / 24.00 dB`.

### C2 — Img Width is a lie over 99 % of its range. `Source/PluginProcessor.cpp:75,151` + `Source/DSP/StereoImager.h:80`

The parameter is declared `0..200` (a percentage) and handed straight to
`StereoImager::setWidth`, which clamps to `0..2` (a linear side-gain multiplier). So the
knob saturates at 2 % and the **default position, 100 %, is 200 % width**.

Proof (`MixAgentCharacterTest`):

```
  width      0 % -> side gain   0.000 (-240.00 dB)
  width     25 % -> side gain   1.998 (6.01 dB)
  width     50 % -> side gain   1.998 (6.01 dB)
  width    100 % -> side gain   1.998 (6.01 dB)
  width    200 % -> side gain   1.998 (6.01 dB)
```

The dead-knob sweep shows the same thing from the other end: `img_width` and `img_mono`
produce *identical* differences (-3.42 dB RMS / -5.08 dB peak), because both are really
comparing "mono" against "width 2".

Consequence: switching the imager on at its default doubles the side signal — a 6 dB image
blow-up and a mono-compatibility problem. Presets 4, 7 and 9 ask for 140/135/150 % and all
get 200 %.

**FIXED.** `setWidth` became `setWidthPercent` and divides by 100. After:
`0.000 / 0.250 / 0.500 / 0.749 / 0.999 / 1.499 / 1.998` for 0/25/50/75/100/150/200 %.
Behaviour change: an existing session with the imager engaged loses 6 dB of side signal,
because it was never supposed to have it.

### C3 — Input/Output gain smoothing is block-size dependent. `Source/PluginProcessor.cpp:380`

```cpp
const float gainCoef = 1.0f - std::exp(-(float)numSamples / (0.010f * (float)sampleRate));
...
for (int i = 0; i < numSamples; ++i) { g += (inTarget - g) * gainCoef; ... }
```

The coefficient is derived for a whole *block* (`numSamples`) but applied once per *sample*,
so the smoothing runs roughly `numSamples` times too fast: at 48 kHz with a 64-sample block
the "10 ms" ramp settles in about 8 samples, and at 512 samples in about 4.

Proof (`MixAgentAuditTest`, per-parameter click hunt; a 440 Hz sine at 0.3 steps by 0.017
per sample of its own accord):

```
  out_gain        0.2735
  in_gain         0.1832
```

**FIXED.** The coefficient is per sample now. After: `out_gain 0.0901, in_gain 0.0626`.

### C4 — Compressor threshold and ratio are not smoothed at all. `Source/DSP/Compressor.h:133-134`

`thresholdDb` and `ratio` are read raw into `tDb`/`slopeFactor` once per block. Everything
else in the module (make-up, mix, bypass) is smoothed; these two are not, so automating them
steps the gain instantly.

Proof (same click hunt):

```
  comp_thresh     0.4116
  comp_ratio      0.3418
```

**FIXED.** Both are one-poled at the same 15 ms as make-up (the ratio via its slope, so there
is no per-sample division). After: `comp_thresh 0.0155, comp_ratio 0.0155`.

---

## MAJOR

### M1 — The "GM drum map" is three sounds wearing ten names. `Source/DSP/DrumEngine.h:55,139,162`

`Voice::note` (line 139) is declared and never written. `familyForNote` picks one of three
recipes and the note number is then discarded, so every note in a family renders the *same
waveform*.

Proof (`MixAgentCharacterTest`):

```
 note  voice        peak dB   f0 Hz   centroid Hz   -20 dB ms   -60 dB ms
   36  kick            -4.3     96.1           95       233.0      1110.0
   41  low tom         -4.3     96.1           95       233.0      1110.0
   45  mid tom         -4.3     96.1           95       233.0      1110.0
   48  hi tom          -4.3     96.1           95       233.0      1110.0
   38  snare           -4.9    161.2          219       209.0       555.0
   39  clap            -4.9    161.2          219       209.0       555.0
   49  crash           -4.9    161.2          219       209.0       555.0
   42  closed hat       1.3    976.3        13956       138.0       304.0
   51  ride             0.3    976.3        13956       138.0       304.0
toms vs kick: BIT-IDENTICAL to the kick
```

Crashes (49/57) are mapped to the *snare* family, so a crash is a 162 Hz tone with a 200 ms
noise burst — centroid 219 Hz, identical to the snare sample-for-sample.

This is also **doc drift**: `CHANGELOG.md` (wave 2) states "GM-style map (toms/bongos → kick
family, crashes/clap/side stick → snare family …) VERIFIED: … 57 = crash (zero-crossing rate
3.4 kHz)". The mapping is real; the implication that those notes sound like the instruments
they are named after is not.

**FIXED.** Every note resolves a `Spec` at note-on. Dominant pitch after:
36 → 96.6 Hz, 41 → 85.4, 45 → 118.1, 48 → 164.4. The crash moved into the cymbal family:
centroid 219 Hz → 10 477 Hz, −20 dB decay 209 ms → 1763 ms. The clap is four gated bursts,
centroid 2745 Hz. Note 36 (the main kick), 38 and 42/44/46 keep their numbers.

### M2 — The snare's "filtered noise burst" is low-passed. `Source/DSP/DrumEngine.h:242`

```cpp
v.hpMem = v.hpMem * 0.6f + white() * 0.4f;   // one-pole LOW-pass, ~3.6 kHz at 44.1 kHz
```

The comment at the top of the file promises "noise burst through a bandpass"; the code is a
one-pole low-pass, so the snare has essentially no top end.

Proof: `snare: body partial -13.6 dB, 4 kHz noise -42.2 dB, centroid 223 Hz`. A snare drum's
spectral centroid sits in the 1–3 kHz region.

**FIXED.** Two tuned shell modes (185 and 330 Hz) plus noise band-passed between a 300 Hz
high-pass and a 9 kHz low-pass. After: centroid 936 Hz.

### M3 — Drum voices are cut off 40 dB down. `Source/DSP/DrumEngine.h:279`

```cpp
if (envExp(v.pos, tail) < 0.01f)   // -40 dB
    v.active = false;
```

Proof: `kick: last non-zero sample is -42.3 dB below the peak (the step to silence)`. That is
a step, not a fade — at a realistic bus level it is an audible tick at the end of every hit.

**FIXED.** Each voice ramps linearly to exact zero over 4 ms starting where its tail is 60 dB
down, or at 3.5 s, whichever comes first. After: `-191.2 dB`.

### M4 — The saw oscillators are not band-limited. `Source/DSP/InstrumentBank.h:338`

`sawWave` is `2*phase - 1`. Harmonics above Nyquist fold straight back down.

Proof (`MixAgentCharacterTest`, note 96 = C7 = 2093 Hz; anything below 0.9 × f0 cannot be a
harmonic of f0):

```
  Pluck        worst sub-fundamental partial   -27.0 dBc at  140 Hz
  Lead         worst sub-fundamental partial   -26.8 dBc at  246 Hz
  Pad          worst sub-fundamental partial   -14.4 dBc at  246 Hz
  Rage         worst sub-fundamental partial    -9.1 dBc at  331 Hz
```

Rage at -9 dBc is fold-down loud enough to be part of the sound. This affects 11 of the 16
programs (every `r.saw` recipe) and is worst exactly where a lead is played.

**FIXED.** PolyBLEP (Välimäki/Huovilainen), two comparisons per oscillator per sample. After:
`Pluck -79.7, Lead -97.3, Pad -75.8, Rage -92.9 dBc` — between 50 and 84 dB less fold-down.

### M5 — Tube mode is 1.6 dB below unity and 3rd-harmonic dominant. `Source/DSP/Saturation.h:292`

```cpp
const float t = std::tanh(x);
return (t + 0.2f * t * t) / 1.2f;   // /1.2 normalises the PEAK, not the small-signal gain
```

Small-signal gain is `1/1.2` = -1.58 dB, so the default saturation mode is not unity at zero
drive and switching modes changes the level.

```
unity gain at minimum drive (1 kHz @ -20 dBFS, mix 1, out 0 dB):
      Tube   -1.61 dB
      Tape   -0.04 dB
      Soft   -0.02 dB
   Exciter   -0.00 dB
```

Separately, the mode is named after a single-ended triode stage, whose signature is an
*asymmetric* transfer curve and therefore a dominant 2nd harmonic. Measured, the 2nd falls
away as drive rises and the 3rd takes over:

```
  mode      drive      H2       H3       H4       H5
    Tube    0.25    -23.1    -23.5    -40.1    -45.1
    Tube    0.50    -22.8    -15.5    -30.8    -28.3
    Tube    1.00    -32.1    -10.0    -32.8    -15.3
```

(The 2nd-harmonic term is `0.2·tanh(x)²`, which saturates while `tanh(x)` keeps squaring up
into odd harmonics.) Tape/Soft being 3rd-dominant is correct — those are symmetric curves.

**FIXED.** `tanh(k·x + b) − tanh(b)` with `k = 1/(1 − tanh²b)` and `b = 0.30 + 0.40·drive`.
Unity at zero drive: −1.61 dB → −0.02 dB. Harmonics after (H2 / H3, dBc):
drive 0.15 @ −6 dBFS `−18.8 / −28.3`; 0.25 @ −6 `−16.8 / −24.1`; 0.50 @ −18 `−17.9 / −37.2`.
At 0.50 @ −6 (`−15.8 / −15.1`) and 1.00 @ −6 (`−25.1 / −9.8`) the stage is in hard clipping
and the odd harmonics catch up, which is what an overdriven triode does; those two points are
measured and printed but not asserted. Settled peak at drive 1.0 on a −3 dBFS 200 Hz sine:
1.090. Behaviour change: Tube is 1.6 dB louder at low drive.

### M6 — Host presets are additive; "Init" resets nothing. `Source/PluginProcessor.cpp:614+`

`setCurrentProgram` writes only the parameters each preset mentions, and `case 0: break;`
means the "Init" preset is a no-op.

Proof (`MixAgentAuditTest`, every ordered pair of the 12 presets, compared against loading
the same preset into a fresh processor):

```
  preset pairs that do not land on the same settings as a fresh load: 118 of 132
  worst: Vocal Presence -> Init leaves 23 parameters at the previous preset's value
```

**FIXED.** `setCurrentProgram` restores every parameter to its default before applying the
preset. After: `0 of 132`. State loading is untouched, so opening a session still restores the
saved parameters rather than the preset.

### M7 — Notes 35–49 are stolen from the instrument on every MIDI channel. `Source/PluginProcessor.cpp:282`

```cpp
auto isDrumNote = [drumChannel](int n) { return drumChannel || (n >= 35 && n <= 49); };
```

A bass line written on channel 1 between B0 and C#2 plays drums. Channel 10 already gives the
whole GM map; the unconditional 35–49 window is a compatibility shim from an earlier build
that costs the instrument its bottom 15 semitones.

**LEFT OPEN, deliberately.** Narrowing the window would silently turn drum parts in saved
sessions into synth parts. Documented in `README.md` under "Known issues and debt".

### M8 — 63 MB and six process documents in the repo belong to other projects.

See "Repo hygiene" below. `HANDOVER.md:23` also tells the owner to expect
"ALL TESTS PASSED (30 checks)"; the suite has 151.

**FIXED.** 134 files / 66,836,259 bytes removed from HEAD (recoverable from history — the
commands are in `README.md`). The six process documents collapsed into one `README.md`, which
the repo did not have at all, plus `CHANGELOG.md` and this file.

---

## MINOR

### N1 — The reverb's diffusion allpasses are not allpasses. `Source/DSP/Reverb.h:242`

```cpp
dl.buf[idx] = in + 0.5f * out;
return 0.75f * out - 0.5f * in;
```

A unity-gain Schroeder allpass needs the same coefficient in both the feed-forward and the
feedback path (`out = -g·in + d`, `buf = in + g·d`). With 0.75 against 0.5 the section has a
magnitude that runs from -6.0 dB at DC to -1.6 dB at the delay's Nyquist, i.e. it colours the
tank instead of only scrambling its phase.

Measured effect on the late field (damping 0, decay 2 s, size 0.7, wet only):

```
   125Hz   3.22     250Hz  -0.89     500Hz  -0.95    1000Hz  -2.10
  2000Hz  -2.48    4000Hz   0.15    8000Hz   3.05     worst deviation 3.22 dB
```

So the audible consequence is 3.2 dB of tilt, not the 4.4 dB ripple the coefficients alone
suggest — the eight combs dominate.

**LEFT OPEN, and the attempt is the reason.** Rebuilding the four sections as proper
unity-gain Schroeder allpasses (`out = −g·x + d`, `buf = x + g·d`, `g = 0.5`) and re-trimming
the wet input so the tail's level was unchanged made every number *worse*: late-field spread
3.22 → 3.97 dB, L/R correlation of the tail 0.163 → 0.289. The tilt is the comb bank's sparse
low-frequency modal density, not the diffusers; flattening it needs a different topology (an
FDN), not a coefficient. The change was reverted and the measurement is recorded in
`Tests/CharacterTest.cpp` next to the 4.5 dB guard.

### N2 — Dead code. `Source/DSP/Saturation.h:154,185`

`processChunk(..., bool shapingOff)` is only ever called with `false`, and the flag is
re-tested on every oversampled sample (`for (int i = 0; i < osNum && !shapingOff; ++i)`).

**FIXED.** Parameter and test removed.

### N3 — Dead members. `Source/DSP/DrumEngine.h:139` (`Voice::note`), `Source/DSP/DrumEngine.h:150` and `Source/DSP/InstrumentBank.h` (`preparedBlock`, written in `prepare`, read nowhere).

**FIXED.** `Voice::note` is now written and used to pick the drum's recipe; `preparedBlock` is
gone from both classes.

### N4 — Reverb pre-delay is applied *after* the tank. `Source/DSP/Reverb.h:285,308`

Pre-delay conventionally sets the gap between the dry sound and the first reflection. Here it
delays the whole wet signal, tail included, which sounds the same for a steady input but
moves the entire reverb rather than only its onset.

**LEFT OPEN.** Moving the pre-delay in front of the tank changes what every existing reverb
setting sounds like, for a difference that is inaudible on sustained material. Documented in
`README.md`.

### N5 — Reverb parameter smoothing follows the host block size. `Source/DSP/Reverb.h:52-54`

`paramCoef`/`sizeCoef` are derived from `bs` (the block size promised in `prepare`) and
`updateCoefficients()` runs once per `process()` call, so the smoothing time changes if the
host delivers a different block size.

### N6 — `-Wshadow`: 3 sites, 8 instances.

`Source/PluginProcessor.cpp:184` — `prepareToPlay(double sr, int blockSize)` shadows
`AudioProcessor::blockSize`; `Source/UI/Style.h:49` shadows `PowerToggle::text`;
`Tests/SmokeTest.cpp:1031` shadows a local `inc`.

**FIXED** at all three sites. `-Wall -Wextra -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual
-Wunused -Werror` on the project's own sources is clean, and CI enforces it.

### N7 — The delay has an always-on, unlabelled 0.4 Hz wobble. `Source/DSP/Delay.h:186`

Depth is `min(time × 0.0015, 1.5) ms` and there is no control for it. Measured pitch swing on
a 1 kHz tone:

```
  always-on wobble at   100 ms: 1 kHz swings 999.6 .. 1000.5 Hz =  1.55 cents peak-to-peak
  always-on wobble at   500 ms: 1 kHz swings 998.1 .. 1002.0 Hz =  6.76 cents peak-to-peak
  always-on wobble at  2000 ms: 1 kHz swings 996.1 .. 1003.9 Hz = 13.47 cents peak-to-peak
```

This is a tape/BBD character choice and it is defensible; it is listed here because it is
undocumented and not switchable, not because it is wrong. **Now documented** in `README.md`
with these numbers, and asserted in `Tests/CharacterTest.cpp` so it cannot grow unnoticed.

### N8 — Compressor attack is about twice the knob value. `Source/DSP/Compressor.h:80-88`

The detector takes `max(peak, 1.414 × sqrt(RMS))` with an 8 ms RMS window, and that window
slows the level rise:

```
  attack knob   1.0 ms -> 63 % of 10.5 dB GR reached at  2.83 ms
  attack knob  10.0 ms -> 63 % of 10.5 dB GR reached at 20.33 ms
  attack knob  50.0 ms -> 63 % of 10.5 dB GR reached at 94.83 ms
```

A hybrid detector is a legitimate design (it is what makes the compressor ignore crest
factor — measured: a square and a sine at the same peak level are only 2.16 dB apart), but the
Attack knob's units are then approximate rather than exact.

### N9 — Width 0 is a mono sum only to within float rounding. `Source/DSP/StereoImager.h:71-72`

`left = dryL + mix·(mid - dryL)` and `right = dryR + mix·(mid - dryR)` are algebraically both
`mid` but round differently. Measured worst L−R difference: **-145.9 dBc**. Not audible; noted
so nobody re-discovers it.

### N10 — Three parameters have no control on the editor. `Source/PluginEditor.cpp`

`in_gain`, `out_gain` and `drum_level` are ordinary automatable parameters with no knob, label
or field anywhere on the UI — reachable only from the host's generic parameter list. The input
and output *meters* are in the top bar; the trims that feed them are not.

Proof (`EditorProbe`, new check):

```
  no control for in_gain
  no control for out_gain
```

(`drum_level` was found by `grep -n "drum_level" Source/PluginEditor.cpp` returning nothing.)

**FIXED.** IN and OUT sit beside their meters, DRUMS beside the instrument LEVEL knob.
42 knobs → 45, and `EditorProbe` now asserts that every continuous parameter has one.

### N11 — Ten knobs could only ever print "0" or "1". `Source/PluginEditor.cpp:100-137`

Ten controls with a 0..1 range (`sat_drive`, `sat_mix`, `comp_mix`, `dly_feedback`, `dly_mix`,
`dly_damp`, `dly_width`, `rvb_damp`, `rvb_width`, `rvb_mix`) were created with
`addKnob(..., "", 0)`, i.e. no unit and zero decimal places, and `Knob::paint` drew
`getTextFromValue(getValue())` — which is `String(value, 0)`. Half the range reads "0" and half
reads "1". The eight frequency knobs printed a bare number with no unit, and `img_width`
printed a bare 0..200 with no percent sign.

**FIXED.** `Knob` takes a display scale and decimal count that affect the readout only: the
0..1 controls read 0–100 %, frequencies say Hz, `img_width` and `rvb_size` say %.
`EditorProbe` asserts every knob's readout differs at 25, 50 and 75 % of its range.

### N12 — Opening the editor reset every parameter. `Source/PluginEditor.cpp:28`

`juce::ComboBox::setSelectedId` notifies **asynchronously** by default, so the constructor's
`presetCombo.setSelectedId(1)` fired `presetCombo.onChange → proc.setCurrentProgram(0)` a few
milliseconds after the window opened. Harmless while preset 0 wrote nothing; a state-wipe once
M6 made a preset restore every default. Caught by `EditorProbe` within the same wave.

**FIXED.** All three of the editor's own initial `setSelectedId` calls pass
`dontSendNotification`, and `EditorProbe` now constructs the editor, runs the message loop and
asserts that not one parameter moved.

### N13 — The reported tail length was a fixed 5 s. `Source/PluginProcessor.h:32`

`getTailLengthSeconds()` returned `5.0` whatever the settings were. A host uses that to decide
how long to keep calling `processBlock` after the transport stops, so under-reporting truncates
the tail on bounce.

Proof (`MixAgentAuditTest`, impulse in, time until the output falls 60 dB below the tail's own
peak, floored at −120 dBFS):

```
  defaults                         measured   0.00 s, reported   5.00 s
  reverb decay 10 s, mix 1         measured   4.68 s, reported   5.00 s
  delay 2 s, feedback 0.95, mix 1  measured  68.00 s, reported   5.00 s
```

**FIXED.** The report is now computed from the settings: `1.25 × Decay + PreDelay + 0.3` for the
reverb, `Time × ln(1000)/−ln(Feedback)` for the delay, floored at 6 s (which covers a released
instrument chord with no FX engaged) and **capped at 30 s**. The cap is deliberate and
documented: 2 s at 0.95 feedback is ~270 s of −60 dB decay in theory and 68 s measured with the
damping filter in the loop, and no host will render minutes of silence — the delay does reach
exact zero, its denormal guard sees to that. After: `6.00 / 12.81 / 30.00 s`.

---

## COSMETIC

- **1002 `-Wold-style-cast` warnings** across the project's own sources (`Source/` and
  `Tests/`). These are all `(float)x` / `(int)x` numeric casts and they are the codebase's
  consistent style. Rewriting 1002 of them to `static_cast` is churn with no behavioural
  benefit and a real chance of typos, so the flag is **not** adopted; `-Wall -Wextra
  -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual` are, and those are driven to zero.
- `tools/build_probe.bat` / `tools/build_smoke.bat` pass `-DBUILD_TESTING=OFF`, which this
  `CMakeLists.txt` never reads (`enable_testing()` is unconditional). Harmless; the target
  names they build all still exist.

---

## Clean results (measured, no action needed)

- **No allocation on the audio thread.** 0 allocations inside `processBlock` across
  4 sample rates × 6 block sizes, while every parameter is swept and MIDI is delivered,
  including oversized (4×) and mono buffers.
- **Biquad/EQ is exact.** Worst |measured − analytic| across 6 filter types × 7 probe
  frequencies: **0.000 dB**. High-shelf cramping at 44.1 kHz with a 16 kHz corner: the +12 dB
  shelf reaches 11.67 dB. Band-frequency automation is coefficient-smoothed (worst step
  0.0200, identical to the signal's own slope).
- **Limiter.** Measured lookahead delay == `getLatencySamples()` (144 at 48 kHz). 8×-measured
  true peak, with an *independent* interpolator, on an inter-sample-peak programme:
  -1.05 dBTP for a -1.0 dB ceiling, -3.05 for -3.0, -0.35 for -0.3 — always under, never over.
  50 Hz at 0 dBFS: 0.000 dB of cycle-to-cycle pumping.
- **Saturation oversampling works.** 15 kHz at 44.1 kHz, drive 1.0: H2's fold-down at
  14.1 kHz is -139 to -177 dBc, H3's at 900 Hz -113 dBc.
- **Delay feedback is stable at the knob maximum** (0.95): decays 20.8 dB over 4.5 s with
  damping off, 46.1 dB with damping at 1.0. Read-head distortion, measured where the LFO is
  momentarily stationary: -51.9 dBc.
- **State.** 1000 round-trips are byte-stable with every parameter unchanged; garbage, empty,
  truncated, foreign and future-version states are all survived; an out-of-range
  `hostProgram` is clamped.
- **Instrument tuning is exact** — 0.0 cents at notes 36/48/60/69/81.
- **Voice stealing is clean**: 200 overlapping note-ons into a 24-voice pool stay finite,
  cap at 24 voices, worst step 0.13 against a peak of 0.54.
- **Reverb stereo decorrelation**: L/R correlation of the tail from a mono source is 0.163.
- **No locks, no logging, no file I/O, no exceptions on the audio path** —
  `grep -rnE "CriticalSection|ScopedLock|std::mutex|MessageManagerLock|DBG\(|Logger|std::cout|printf|File " Source/`
  returns only editor-construction `new`s and `std::atomic::load` calls.

---

## Repo hygiene

Sizes from `du`. "Referenced by" is the result of
`grep -rn <name> --include=*.h --include=*.cpp --include=CMakeLists.txt --include=*.yml .`

| Path | Size | Referenced by |
|---|---|---|
| `Assets/Sounds/source_zips/garebear-808-kit.zip` | 48 MB | only `tools/build_assets.ps1` |
| `Assets/Sounds/source_zips/garebear-drum-kit.zip` | 8.2 MB | only `tools/build_assets.ps1` |
| `Assets/Sounds/source_zips/bpm150-trap-loop.wav` | 3.7 MB | only `tools/build_assets.ps1` |
| `screenshots/` (37 PNGs + `gallery.html`) | 3.3 MB | `AGENTS.md` only |
| `.opencode/` (21 agents, 22 skills, 8 references) | ~600 KB | nothing |
| `.claude/skills/unity-cli/` | ~100 KB | nothing |
| `Tests/block-blast-test.js` | 2 KB | nothing |
| `tools/harness.js`, `balance.js`, `shot.js`, `dbg.js`, `patch-dbg.js` | 56 KB | `AGENTS.md` only |
| `AGENTS.md` | 5.6 KB | browser-game tooling (`node tools/harness.js games/foo.html`) |
| `IMPROVEMENT_LOG.md` | 26 KB | a log of `block-blast.html` changes |
| `SWARM_BRIEF.md` | 4.5 KB | "Phone-first HTML5 arcade hub" |
| `WORKLOG.md` | 4.0 KB | "Chess Juice build session" |
| `tasks/plan.md`, `tasks/todo.md` | 16 KB | a Discord-clone web app named "Unity" |
| `README.md` | — | **does not exist**; the repo has no front door |

No C++ source, no `CMakeLists.txt` rule and no CI step loads anything from `Assets/`:
`InstrumentBank.h` and `DrumEngine.h` synthesise every voice, and there is no
`juce_add_binary_data`, no `AudioFormatManager` and no `createReaderFor` anywhere.
`tools/build_assets.ps1` extracts the zips into `Assets/Sounds/_staging`, builds a `$manifest`
in which every entry's `id` and `file` are the empty string, and then writes only file counts
to the console — nothing consumes its output and no manifest file is produced.

`build/`, `build-ci/` and `build-asan/` are all matched by `.gitignore` and `git status`
is clean.

The Windows workflow scripts (`tools/build_plugin.bat`, `build_probe.bat`, `build_smoke.bat`)
still name targets that exist (`MixAgent_VST3`, `MixAgent_Standalone`, `EditorProbe`,
`MixAgentSmokeTest`) and are kept.

---

## Unproven — suspected, not measured

- **No lock is taken on the audio thread.** The grep above finds no `CriticalSection`,
  `ScopedLock`, `std::mutex` or `MessageManagerLock` in `Source/`. I could not interpose a
  lock detector the way I interposed `operator new`, so this is a grep, not a measurement.
- `setFavorite` / `getFavorites` (`Source/PluginProcessor.cpp:540-600`) mutate `apvts.state`
  from the message thread while the audio thread reads APVTS atomics. The audio thread never
  touches the `ValueTree`, so this is probably fine, but it has not been run under
  ThreadSanitizer.
- The editor has never been *looked at*. `EditorProbe` drives the program ComboBox, every knob
  and every toggle under `xvfb`, which proves the wiring, not the appearance or the layout.
- No DAW, no `pluginval`, no real audio device was available. Host-side behaviour (latency
  compensation, bypass handling, parameter automation from a real host) is inferred from the
  JUCE contract, not observed.
- `maxPreDelaySamples()` (`Source/DSP/Reverb.h:172`) reads `predelays[0].maxLen`, which is 0
  until `prepare()` runs; `setPreDelayMs` before `prepare` would therefore clamp to -2.
  Suspected benign because `prepare` recomputes `preDelayF` afterwards.

---

## Summary

| # | Severity | Finding | Status |
|---|---|---|---|
| C1 | CRITICAL | Compressor make-up gain unreachable | FIXED — 0.00 → 3.00/6.00/12.00/24.00 dB |
| C2 | CRITICAL | Img Width saturates at 2 %; 100 % means 200 % | FIXED — side gain now tracks the percentage |
| C3 | CRITICAL | In/Out gain smoothing ~blockSize times too fast | FIXED — per-sample coefficient |
| C4 | CRITICAL | Compressor threshold/ratio unsmoothed | FIXED — step 0.41/0.34 → 0.0155 |
| M1 | MAJOR | 10 drum notes, 3 waveforms, bit-identical | FIXED — per-note Spec table |
| M2 | MAJOR | Snare noise low-passed, no top | FIXED — centroid 223 → 936 Hz |
| M3 | MAJOR | Drum voices cut off 40 dB down | FIXED — last sample −42.3 → −191.2 dB |
| M4 | MAJOR | Naive saw, fold-down to −9 dBc | FIXED — PolyBLEP, −76 to −97 dBc |
| M5 | MAJOR | Tube mode −1.6 dB and 3rd-dominant | FIXED — unity, 2nd leads by 7–19 dB |
| M6 | MAJOR | Presets additive, "Init" a no-op | FIXED — 118/132 contaminated → 0/132 |
| M7 | MAJOR | Notes 35–49 stolen from the instrument on all channels | LEFT OPEN — would change saved sessions |
| M8 | MAJOR | 63.7 MB unreferenced, six stray process docs, no README | FIXED — 134 files removed, one README |
| N1 | MINOR | Reverb diffusers are not allpasses | LEFT OPEN — fixing them measured *worse* |
| N2 | MINOR | `shapingOff` dead parameter | FIXED |
| N3 | MINOR | Dead members (`Voice::note`, `preparedBlock`) | FIXED |
| N4 | MINOR | Reverb pre-delay after the tank | LEFT OPEN — would change every reverb setting |
| N5 | MINOR | Reverb param smoothing follows the block size | LEFT OPEN — inaudible, documented |
| N6 | MINOR | `-Wshadow` at three sites | FIXED — strict set clean, enforced in CI |
| N7 | MINOR | Undocumented always-on delay wobble | DOCUMENTED + asserted (1.55–13.5 cents) |
| N8 | MINOR | Compressor attack ~2× the knob | LEFT OPEN — the RMS branch is the feature |
| N9 | MINOR | Width 0 mono fold differs by rounding | LEFT OPEN — −145.9 dBc |
| N10 | MINOR | `in_gain`, `out_gain`, `drum_level` have no control on the editor | FIXED — 42 → 45 knobs |
| N11 | MINOR | Ten knobs could only print "0" or "1"; no units | FIXED — percentages, Hz, % |
| N12 | MAJOR | Opening the editor reset every parameter | FIXED — caught by EditorProbe |
| N13 | MINOR | `getTailLengthSeconds()` fixed at 5 s vs a 68 s measured tail | FIXED — computed from the settings, capped at 30 s |

Test counts: 151 → 151 (`MixAgentSmokeTest`), 0 → 11 (`MixAgentAuditTest`),
0 → 42 (`MixAgentCharacterTest`), 9 → 12 (`EditorProbe`). **160 → 216 checks** in total,
29.1 s for the whole `ctest` run.

Sanitizers: the whole suite under `-fsanitize=address,undefined` —
`MixAgentSmokeTest` 151/151, `MixAgentAuditTest` 11/11, `MixAgentCharacterTest` 42/42, all
exit 0 with no AddressSanitizer, LeakSanitizer or UndefinedBehaviorSanitizer report.
