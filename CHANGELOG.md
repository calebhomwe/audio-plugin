# Changelog

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

- The limiter is a sample-peak limiter: on a hard-clipped square wave the 4× true peak
  reaches +1.06 dBTP with a −1 dB ceiling (inter-sample overshoot). Sine material stays within
  the ceiling. A true-peak detector would need an oversampled sidechain.
- Reverb "decay" is the low-frequency RT60; with Damp 0.5 a 1 kHz tail decays ~4× faster
  (Freeverb-style). Musically normal, but the knob label is optimistic for bright material.
- Release times are exponential time constants: a voice stays allocated until its level falls
  below −66 dB, i.e. ~7.6× the nominal release (Pad: 10 s). Stealing such a tail is now
  click-free, so this is a CPU/polyphony consideration only.
- MIDI program change messages are not mapped to instrument programs (host programs / the
  `inst_program` parameter do that). INFERRED design choice, unchanged.
- DrumEngine's note map entries for 57 (snare) and 60 (kick) are unreachable: the processor
  only routes notes 35–49 to the drums. 49 (GM crash) is rendered as a snare — documented
  behaviour, left alone.
- Current program index (`getCurrentProgram`) is not part of the saved state (only the
  parameters are), so a reloaded session shows preset 0 while the parameters are correct.
- `PadGrid.h` has three signed/unsigned comparison warnings (GUI only, harmless).
- Not verifiable headless: DAW behaviour, pluginval, sound design judgement of the six new
  recipes (they are synthesised, license-clean, and measured for click-free envelopes only).
