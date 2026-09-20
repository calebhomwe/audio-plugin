# Changelog

## 2026-09-20 (wave 2) — refinements, CI

All measured with `Tests/SmokeTest.cpp` (now 151 checks) and `Tests/EditorProbe.cpp` (9 checks).

- **Limiter is now a true-peak limiter** (`Source/DSP/Limiter.h`). A 4-phase polyphase
  windowed-sinc interpolator (128-tap prototype) feeds the gain computer, so inter-sample peaks
  are limited. The sidechain runs 16 samples behind the input and the attack is clamped to the
  remaining lookahead, so the audio-path latency and `setLatencySamples()` are unchanged. The
  interpolated estimate carries 0.15 dB of headroom against sharper measurement filters.
  VERIFIED at 4× with a −1 dB ceiling: hard-clipped square +1.06 → **−1.04 dBTP**, sine bursts
  −1.09 → −1.17 dBTP, sample peak still exactly at the ceiling, transparent below it.
- **Voice lifecycle** (`InstrumentBank.h`): released voices end at −96 dB or at
  max(3 × release, 2 s) after note-off, where a linear fade (release/2, clamped 50 ms…1 s) takes
  the envelope to exactly zero. VERIFIED with a 24-voice Pad chord released and rendered for
  20 s: tail 11.09 s → 5.25 s, 266 → 126 voice-seconds, ~400 → ~330 ms CPU for the 20 s
  (wall-clock numbers are noisy on a shared box; voice-seconds are deterministic), largest
  sample step at the free point 6.6e-5 (no click). Per program the tail now ends 1.75–5.25 s
  after release (was 1.25–10 s).
- **MIDI program change** 0–15 selects the instrument program at its sample offset (audio
  thread); the `inst_program` parameter follows on the message thread. VERIFIED: PC at offset
  256 + note at 300 renders bit-identically to a processor already on the new program; the
  parameter reads the new program after the message loop runs; PC on channel 10 is ignored.
- **Host preset index is saved** (`hostProgram` attribute on the state XML) and restored without
  re-applying the preset; older states without it load as preset 0 as before. VERIFIED.
- **Drums on MIDI channel 10** (GM convention) in addition to the legacy 35–49 range on other
  channels; GM-style map (toms/bongos → kick family, crashes/clap/side stick → snare family,
  hats/rides/percussion → cymbal). VERIFIED: every note 35–61 on channel 10 renders; 57 = crash
  (zero-crossing rate 3.4 kHz), 60 = drum hit (125 Hz); note 57 on channel 1 still plays the
  instrument. **UI pads never hit the drum range any more**: the preview keyboard is C3–B3
  (48–59) and pads C/C# (48/49) used to play a tom/crash instead of the instrument.
- **Reverb decay knob — measured, documented, not changed.** RT60 at decay 0.5 / damp 0.5:
  200 Hz 0.50 s, 1 kHz 0.37 s, 5 kHz 0.30 s; damp 0: 1 kHz 0.53 s, 5 kHz 0.51 s; decay 2.0 /
  damp 0.5 @200 Hz 2.27 s. So Decay is the low-frequency RT60 (within 15 %) and Damp shortens
  the highs by up to ~40 % at 0.5 — which is what a damping control is for. Making the knob a
  1 kHz reference would push the LF decay above the knob value and hit the 0.999 feedback clamp
  at high damping, so the honest option is to keep it and state it. Asserted in the suite.
  (Corrects the wave-1 note that claimed "~4× faster at 1 kHz": that was INFERRED and wrong.)
- **EditorProbe** now drives the UI: program ComboBox → `inst_program` and back, all 42 knobs
  (parked at min, driven to max: exactly one parameter reaches its maximum each time), all 12
  toggles (11 drive a parameter, FAV is state). Registered with CTest under `xvfb-run`.
- **CI**: `.github/workflows/ci.yml` (ubuntu-24.04, cached JUCE 8.0.9 clone, Release/Ninja,
  builds `MixAgentSmokeTest` + `EditorProbe` + `MixAgent_VST3`, `ctest`, editor probe under
  xvfb). Dry-run of every step in a fresh `build-ci` directory on the verification box: see
  the PR.

## 2026-09-20 — headless verification pass (Linux, JUCE 8.0.9)

Everything below was measured with `Tests/SmokeTest.cpp` (137 checks, ~1.7 s, registered
with CTest) unless marked INFERRED. Parameter IDs, plugin/manufacturer codes and product
names are unchanged, so existing sessions keep loading.

### Fixed

- **Compressor attack/release were effectively instantaneous** (`Source/DSP/Compressor.h`,
  `updateCoefs`). The one-pole *retention* factor `exp(-1/(t·fs))` (≈0.998) was used as the
  per-sample *step*, so the envelope jumped to the target within a couple of samples regardless
  of the knobs. Now `1 - exp(-1/(t·fs))`. VERIFIED: attack 10 ms → 63 % GR at 10 ms; release
  200 ms → 37 % at 222 ms; 4:1 static curve within 0.03 dB, slope 0.250.
- **Reported latency was wrong whenever Saturation was bypassed** (`Source/DSP/Saturation.h`).
  The module reported the oversampler's latency at all times but skipped the oversampler when
  off, so the host compensated 59–60 samples that were not there (measured 132 vs reported 191).
  Bypass now runs a bit-exact delay line of the same length; the oversampler uses
  `useIntegerLatency` (the two FIR stages otherwise sum to a half-sample delay) and its state is
  primed from that ring when re-engaged, so toggling is seamless. VERIFIED: reported == measured
  (193 samples @44.1k incl. limiter) with Saturation off, on, and at 96 kHz.
- **Audio-thread allocation on blocks larger than `prepareToPlay` promised**: `Saturation`
  re-initialised the oversampler and grew `workBuffer`; `InstrumentBank`/`DrumEngine` grew
  their scratch buffers. All three now render in slices of the prepared size. VERIFIED with a
  global `operator new` counter around `processBlock` at block sizes 1 / 512 / 2048 / 4096
  (prepared 512), with MIDI + UI notes: 0 allocations.
- **UI note queue used a `CriticalSection` taken on the audio thread**
  (`PluginProcessor.cpp`). Replaced by a `juce::AbstractFifo` (single producer / single
  consumer, 128 slots, overflow drops). VERIFIED: UI notes sound, overflow is harmless.
- **MIDI was applied at block start only.** Note-on/off, pitch bend and CCs are now applied at
  their sample offset (the synth bus is rendered in sub-blocks split at each event). VERIFIED:
  a note-on at offset 256 produces exact zeros before sample 256 + latency.
- **Six of the sixteen instrument programs had no recipe** (Dark Bell, Glass Pluck, Vox Choir,
  Soft Soul, Bounce, Rage fell through `makeRecipe` to the default: instant attack, no filter,
  *instant release* = click on every note-off). They now have proper ADSR / filter / FM recipes.
  VERIFIED: every program's note-off step ≤ 1.5× its steady-state sample step; every envelope
  reaches exactly 0.0 and frees its voice (Pluck 1.5 s … Pad 10 s after release).
- **Retrigger / voice stealing popped**: `noteOn` reset the voice (`v = Voice{}`), so a
  sounding voice jumped to zero. The amplitude envelope now restarts from the current level and
  oscillator phases / filter memories carry over. VERIFIED (Sub program, pure sine): retrigger
  step 0.0081 vs steady 0.0079; 25th-note steal step 0.0019 vs steady 0.0018.
- **Program changes from the message thread could tear the shared `Recipe`** read by the audio
  thread. `setProgram` now only stores an atomic index; the audio thread rebuilds the recipe.
- **Delay time changes clicked** (`Source/DSP/Delay.h`): the 20 ms one-pole slew moved the read
  head ~9 samples in the first sample (a "zip"), and the LFO depth — derived from the time —
  jumped instantly. Time changes now cross-fade (30 ms, equal power) between the old and the new
  read head and the modulation depth is slewed. VERIFIED: largest step during a 380→200 ms change
  0.0186 vs steady 0.0154 (was 0.175).
- **Limiter released during its own lookahead**: the gain started recovering before the peak
  that caused it had left the 3 ms delay line, so the peak was only caught by the tanh clipper
  stage (distortion instead of gain riding). Added a hold of `delaySamples` after each reduction.
  VERIFIED: +6 dBFS sine → output RMS = ceiling − 3.02 dB (clean gain riding), sample peak never
  above the ceiling, transparent below it (0.05 dB), 4× true peak on sine bursts −1.09 dBTP.
- **Reverb pre-delay read one float past its buffer** (`Source/DSP/Reverb.h`,
  `PreDelay::next`). Found by AddressSanitizer: the smoothed pre-delay settles at a tiny
  positive value, `wIdx - 1e-7` rounds to `wIdx` in float and after the `+ maxLen` wrap to
  `maxLen` itself, one past the buffer. The integer index is now wrapped explicitly. VERIFIED:
  the whole suite runs clean under `-fsanitize=address,undefined` (137/137, 7.3 s).
- **Drum note map**: GM 42 (closed hat) was rendered as an open hat. Fixed (46 stays open).
- **Instrument/drum bus soft limiter** (`agm::softClipBus`, `Common.h`): the synth sum is now
  bounded to ±1.0 (unity below 0.85) before it enters the FX chain, so a 24-voice pile-up at
  +6 dB cannot hard-clip. Host audio is not touched. VERIFIED: 24 voices +6 dB → peak 1.00;
  single voice at the default level 0.19 (well below the knee).
- Added: sustain pedal (CC64, deferred note-offs), all-notes-off (CC123, fast release),
  all-sound-off (CC120), pitch bend ±2 semitones. VERIFIED: bend ratio 1.1224 (= 2^(2/12)).
- Build portability: `JUCE_WEB_BROWSER=0`/`JUCE_USE_CURL=0`, `MIXAGENT_COPY_PLUGIN` option
  (default ON, i.e. unchanged for the owner's Windows workflow), `MIXAGENT_SANITIZE` option,
  `enable_testing()` + `add_test(MixAgentSmokeTest)`, deprecated `Font::getStringWidth` replaced,
  `EditorProbe` writes its PNG to the system temp dir instead of a hard-coded Windows path.

### Verified and left as is

- EQ: ±15 dB at Q=10 exact to 0.001 dB at 1 kHz and 14 kHz @44.1k; shelves reach ±15 dB;
  HP −3 dB at the corner; analytic curve matches. Stable across 44.1/48/96/192 kHz.
- Imager: mono input stays mono at width 200 %; width 0 and the Mono switch collapse to mono.
- Delay feedback (max 0.95) decays monotonically; reverb decays >60 dB and reaches exact zero
  (denormal flush), stable at decay 10 s / size 1.0.
- NaN/Inf input: output finite, signal recovers. Full chain finite and bounded (peak < 2.0)
  at 44.1/48/96/192 kHz with block sizes 1, 32, 64, 512, 2048, 4096 on a 512-sample prepare.
- State: all 58 parameters round-trip exactly incl. `inst_program` and favourites; garbage,
  empty, truncated, foreign-tag and "older" (missing `inst_*`, unknown extra param) state
  neither crash nor half-apply; all 12 factory presets load and render.
- Denormal protection is a named `juce::ScopedNoDenormals noDenormals;` (engaged).
- Linux build (gcc 13, `-Wall -Wextra`): `MixAgentSmokeTest`, `EditorProbe` and `MixAgent_VST3`
  build with 0 warnings from this repo's sources (JUCE's own deprecation notes aside).
  `EditorProbe` constructs the editor, resizes it 1160×920 → 900×700 → 1160×920 and renders a
  snapshot under `xvfb-run` (exit 0).

### Known / open

- ~~Sample-peak limiter~~ → true-peak sidechain (wave 2). Remaining: the true-peak estimate is
  a 4× interpolation; signals engineered against it could still exceed the ceiling by a
  fraction of a dB between samples.
- Reverb Decay = low-frequency RT60; Damp shortens the highs (measured above). Documented, not
  changed.
- ~~Release tails hog voices for ~7.6× the release~~ → bounded at max(3× release, 2 s) (wave 2).
- ~~MIDI program change not mapped~~ → mapped (wave 2).
- ~~Drum map entries 57/60 unreachable~~ → channel 10 routes the whole map (wave 2). Notes
  35–49 on other channels still go to the drums for compatibility with earlier sessions.
- ~~Current program index not saved~~ → saved (wave 2).
- Not verifiable headless: DAW behaviour, pluginval, sound design judgement of the six new
  recipes (they are synthesised, license-clean, and measured for click-free envelopes only).
