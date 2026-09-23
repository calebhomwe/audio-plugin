# Changelog

## 2026-09-23 (wave 4) — closing what wave 3 left open

Four items. Each started by reproducing wave 3's number before anything changed, and two of
those re-measurements overturned the earlier conclusion. Full workings in `AUDIT.md` under
"Wave 4".

### Fixed

- **Reverb pre-delay now sits in front of the tank** (`Source/DSP/Reverb.h`). It was applied to
  the tank's output. The audit assumed moving it would change every existing reverb setting;
  measurement says otherwise — the tank is linear and time-invariant, so the delay commutes
  with it, and the 40 ms impulse response before and after differs by **−118.1 dBc** with
  83.2 % of samples bit-identical. No preset's sound moves. What *was* wrong is the knob under
  automation: stepping pre-delay 0 → 100 ms during a decaying tail used to slide a read pointer
  back over the wet history and re-play the louder earlier part of the decay, **3.66 dB** away
  from the un-automated reference. Now **0.00 dB**. Onset still lands at the pre-delay setting
  plus the tank's own 17.67 ms, and the tail length is unchanged at every setting.
- **Reverb comb bank rebuilt: 16 mutually-prime lines over a 1:1.95 range, eight per stereo
  half** (`Source/DSP/Reverb.h`). The old set was the Freeverb numbers, with 21 of its 28 pairs
  sharing a common factor and seven of eight divisible by 3, split 4/4 so each channel had four
  lines over a 1.2:1 spread. Eight candidate sets were measured over six noise seeds: no 8-line
  set beats 4.41 dB however well conditioned, so line count — not coefficients — is the lever,
  exactly as a modal-density diagnosis predicts. Worst-band deviation **4.28 → 2.35 dB** on the
  six-seed mean (5.76 → 3.11 dB worst, 3.22 → 3.11 dB on wave 3's single seed). Per-octave-band
  RT60 deviation 0.577 → 0.513 s; modal ringing below 500 Hz +21.13 → +19.34 dB. Wet level is
  held to within 0.39 dB by a 1/sqrt(lines) scale. **The reverb is voiced differently** —
  parameter IDs, ranges and defaults are untouched so sessions load identically, but the tank is
  a different room. Costs 0.42–0.43 % → 0.76–0.88 % of one core.
- **Compressor Attack knob calibrated to the attack it delivers** (`Source/DSP/Compressor.h`).
  It read about twice the knob: 1/10/50 ms measured 2.83/20.33/94.83 ms. The cause is the
  detector's 8 ms RMS branch, which is what keeps the gain reduction crest-independent, so the
  branch stays and the mapping was calibrated against it instead. Now 10.33 ms for a 10 ms
  setting, 25.33 for 25, 49.83 for 50 — within 3.3 % from 10 ms up and 6 % from 3 ms up.
  Crest-independence intact (2.16 → 2.13 dB between a square and a sine).
  **Behaviour change for saved sessions:** the same number on the knob now buys about half the
  attack time. Factory presets 1–4 (15/5/25/20 ms) will attack roughly 1.9× faster.

### Corrected — two wave-3 findings were wrong

- **The reverb's diffusion sections *are* unity-gain allpasses.** `AUDIT.md` N1 claimed
  `out = 0.75*d − 0.5*x` against a 0.5 feedback gives −6.0 dB at DC to −1.6 dB at Nyquist. That
  reads the coefficients against each other and misses that the delayed value is read before the
  write: `H(z) = (z^-m − 0.5)/(1 − 0.5 z^-m)`, the textbook allpass. **Measured ripple of one
  section over 0…π, at all four lengths: 0.000 dB.** The replacement wave 3 built and reverted
  has `H(z) = (1.25 z^-m − 0.5)/(1 − 0.5 z^-m)` — 2.183 dB of ripple and +3.52 dB at DC, four in
  series. That, not modal density, is why it measured worse. The sections were always right.
- **`SmokeTest`'s RT60 estimator was the broken instrument.** It took two 50 ms points 250 ms
  apart and divided, which over a long decay measures the band's modal beating. Against the *old*
  reverb it read **7.889 s for a 5.0 s setting** — 58 % high, and never tested, because the check
  only looked at 0.5 s and 2.0 s. Replaced with a least-squares fit over the 40 dB below the
  start of the decay, now asserting three settings at the same 15 % tolerance. It passes against
  both the old bank (0.501/1.904/4.330 s) and the new one (0.496/2.111/4.729 s).

### Measured and deliberately NOT changed

- **Notes 35–49 still go to the drums on every channel.** Wave 3 left this alone because it
  "would change saved sessions"; the evidence refines that. Nothing the plugin saves holds a note
  number — the state's six distinct XML attribute names are `version encoding hostProgram id
  value p`, i.e. 58 parameter values, a host program index and a favourites list of program
  indices — so there is no plugin state to migrate and a version hint would buy nothing. The note
  numbers live in the **host's MIDI clips**, which the plugin cannot read, version or detect.
  That makes the case for leaving it alone stronger, not weaker.
- **L/R decorrelation did not improve and is reported as such**: 0.163 → 0.171 on wave 3's seed,
  0.144 → 0.152 on a six-seed mean. All eight comb candidates landed between 0.109 and 0.176
  against a baseline ranging 0.036–0.347 across seeds, so the measurement cannot separate them
  and was not allowed to decide.
- **The mono-sum reverb path's tilt got slightly worse**: 3.61 → 4.21 dB on the six-seed mean,
  although its seed-to-seed range collapses from 2.84 dB to 0.58 dB.

### Tests

- Four checks added to `MixAgentCharacterTest` (42 → 46): the wet onset equals the pre-delay
  setting plus the tank's own onset; the tail length does not change with the pre-delay;
  automating the pre-delay leaves a ringing tail untouched; measured attack equals the printed
  knob within 10 % from 3 ms up. Each was run against the pre-wave-4 code to confirm it fails
  there.
- **216 → 220 checks**, 4/4 ctest targets pass in 71.3 s. Allocations inside `processBlock`
  across 24 rate/block combinations: **0**. Reported latency equals measured at every setting
  (193 samples, saturation on and off, limiter engaged, and at 96 kHz).
- No assertion was loosened: the reverb flatness guard tightened from 4.5 dB to 4.0 dB and the
  RT60 check gained a setting at an unchanged tolerance.

## 2026-09-22 (wave 3) — merciless audit, hygiene pass, authenticity

`AUDIT.md` is the full record: every finding with a `file:line`, a mechanism and the command
that proves it, marked FIXED with an after-measurement or LEFT OPEN with a reason.

### New audit instruments (permanent tests)

- `MixAgentAuditTest` — a dead-parameter sweep over all 58 controls (every module engaged, a
  stereo probe carrying audio and MIDI; all 58 move the output by more than −80 dB RMS), an
  `operator new` counter armed only across `processBlock` (**0 allocations** across 4 sample
  rates × 6 block sizes plus oversized and mono buffers, while every parameter is swept and
  MIDI is delivered), a 1000× state round-trip, garbage/truncated/future-version state, a
  per-parameter click hunt, and host-preset integrity across all 132 ordered preset pairs.
- `MixAgentCharacterTest` — module-level measurement of every processor against the topology it
  is named after: compressor transfer curve/ratio/knee/attack/make-up/detector, limiter latency
  and 8×-measured true peak with an *independent* interpolator, saturation unity gain, harmonic
  series and aliasing, biquad response against its analytic transfer function, delay
  interpolator and wobble, reverb late-field flatness and decorrelation, imager width mapping,
  every drum voice's spectrum/decay/velocity response, instrument tuning/aliasing/release/voice
  stealing.
- 160 → 216 checks in total across four targets, all registered with CTest and wired into CI.
- Sanitizers: the whole suite under `-fsanitize=address,undefined` is clean — 151/151, 11/11
  and 42/42, exit 0, no AddressSanitizer, LeakSanitizer or UndefinedBehaviorSanitizer report.

### Fixed

- **Compressor make-up gain was unreachable** (`Source/DSP/Compressor.h`). Make-up and gain
  reduction shared one value that was then clamped to ≤ 1.0 so the gain computer could not
  boost — and that clamp threw the make-up away with it. VERIFIED with the signal 20 dB below
  threshold: knob +3/+6/+12/+24 dB gave **0.00/0.00/0.00/0.00 dB**, now gives
  **3.00/6.00/12.00/24.00 dB**. Four factory presets ask for 1.5–2.5 dB and now get it.
- **Compressor threshold and ratio were the only unsmoothed controls in the module.** Both are
  now one-poled at the same 15 ms as make-up (the ratio via its slope, so no per-sample
  division). VERIFIED, worst step when flipped between extremes every block against a 440 Hz
  sine whose own slope is 0.017: `comp_thresh 0.4116 → 0.0155`, `comp_ratio 0.3418 → 0.0155`.
- **Img Width was a lie over 99 % of its range** (`Source/DSP/StereoImager.h`,
  `Source/PluginProcessor.cpp`). The 0..200 % parameter went straight into a setter that
  clamped to 0..2 as a linear side-gain multiplier, so the control saturated at 2 % and its
  default position, 100 %, meant 200 % width. VERIFIED side gain at 0/25/50/75/100/150/200 %:
  was `0.000/1.998/1.998/1.998/1.998/1.998/1.998`, now
  `0.000/0.250/0.500/0.749/0.999/1.499/1.998`.
- **In/Out gain smoothing was block-size dependent** (`Source/PluginProcessor.cpp`). The
  coefficient was derived for a whole block and applied once per sample, so a "10 ms" ramp
  settled in about 8 samples at a 64-sample block. VERIFIED worst step:
  `out_gain 0.2735 → 0.0901`, `in_gain 0.1832 → 0.0626`.
- **Host presets were additive and "Init" was a no-op** (`Source/PluginProcessor.cpp`).
  `setCurrentProgram` now restores every parameter to its default before applying the preset.
  VERIFIED: of the 132 ordered preset pairs, **118 used to land somewhere other than a fresh
  load of the same preset; now 0 do**. State loading is untouched, so opening a session still
  restores the saved parameters rather than the preset.
- **The GM drum map was three recipes wearing ten names** (`Source/DSP/DrumEngine.h`).
  `Voice::note` was declared and never written, so the note number was thrown away after
  choosing a family: notes 41/45/48 (toms) came out BIT-IDENTICAL to note 36 (kick), 39 (clap)
  and 49/57 (crash) bit-identical to 38 (snare), 51 (ride) identical to 42 (closed hat). Every
  note now resolves a per-note `Spec` at note-on.
  - Kick/tom/bongo: body pitch, strike pitch, sweep time and ring time per note. VERIFIED
    dominant pitch: `36/41/45/48` was `96.1/96.1/96.1/96.1 Hz`, now `96.6/85.4/118.1/164.4`;
    toms' −20 dB decay `233 ms → 617/652/628 ms`. Note 36's numbers are unchanged, so the main
    kick sounds exactly as it did. The beater click was 12 *samples* long — i.e. its duration
    changed with the sample rate — and is now specified in milliseconds.
  - Snare/clap: the "filtered noise burst" was a one-pole LOW-pass. It is now two tuned shell
    modes (185 and 330 Hz, roughly a fifth apart, as a snare head's first two modes are) plus
    noise band-passed between a 300 Hz high-pass and a 9 kHz low-pass. VERIFIED spectral
    centroid **223 Hz → 936 Hz**. Note 39 is a real clap: the same noise gated into four bursts
    9 ms apart followed by the room tail, centroid 2745 Hz. Note 37 is a rim click, 40 a
    tighter electric snare.
  - Hats/rides/cymbals: six square oscillators tuned to the mode ratios of a free circular
    plate (1, 2.08, 3.41, 3.89, 5.00, 6.43 — Kirchhoff plate theory) with per-hit scatter,
    through a two-pole high-pass. VERIFIED on the closed hat: energy above 4 kHz relative to
    below it **−2.5 dB → +17.6 dB**. Crashes 49/57 moved out of the snare family: the crash's
    centroid **219 Hz → 10 477 Hz**, its −20 dB decay **209 ms → 1763 ms**.
  - Voice lifecycle: a voice used to be dropped at 1 % of peak, a step at −40 dB. Each voice
    now ramps linearly to exact zero over 4 ms starting where its tail is 60 dB down (or at
    3.5 s). VERIFIED: the kick's last non-zero sample **−42.3 dB → −191.2 dB** below its peak.
  - Velocity: amplitude still spans 10.2 dB from 0.25 to 1.0, and a softer hit is now also
    darker and shorter (the noise low-pass and cymbal high-pass follow velocity, decays scale
    0.70–1.00, a harder strike starts from a higher pitch).
  - Per-sample `std::exp()` is gone from every drum envelope and the pitch sweep — recursive
    multipliers now.
- **The saw oscillators were not band-limited** (`Source/DSP/InstrumentBank.h`). PolyBLEP
  (Välimäki/Huovilainen), two comparisons per oscillator per sample. VERIFIED, loudest partial
  BELOW the note's own fundamental at C7 (note 96, 2093 Hz):
  `Pluck −27.0 → −79.7`, `Lead −26.8 → −97.3`, `Pad −14.4 → −75.8`, `Rage −9.1 → −92.9 dBc`.
  Affects 11 of the 16 programs.
- **Tube saturation was 1.6 dB down and 3rd-harmonic dominant** (`Source/DSP/Saturation.h`).
  `(tanh(x) + 0.2·tanh(x)²)/1.2` normalises the peak, not the small-signal gain, and its
  squared term saturated while `tanh` kept squaring up into odd harmonics. It is now
  `tanh(k·x + b) − tanh(b)` with `k = 1/(1 − tanh²b)` and `b = 0.30 + 0.40·drive`, which is a
  biased (asymmetric) stage — the thing that gives a single-ended triode its 2nd harmonic.
  VERIFIED unity at minimum drive: **−1.61 dB → −0.02 dB** (Tape −0.04, Soft −0.02, Exciter
  −0.00 were already right). VERIFIED H2/H3 in dBc: drive 0.15 @ −6 dBFS `−18.8/−28.3`,
  0.25 @ −6 `−16.8/−24.1`, 0.50 @ −18 `−17.9/−37.2`. At 0.50 @ −6 and 1.00 @ −6 the stage is
  in hard clipping and the odd harmonics catch up, which an overdriven triode also does; those
  points are measured and printed but not asserted. Settled peak at drive 1.0 on a −3 dBFS
  200 Hz sine: 1.090. Oversampling still keeps fold-down aliasing at −138 dBc or lower.
- **`-Wshadow` at three sites**: `prepareToPlay`'s `blockSize` shadowed
  `AudioProcessor::blockSize`, `PowerToggle`'s constructor argument shadowed its member, and a
  local in `SmokeTest.cpp`. `-Wall -Wextra -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual
  -Wunused -Werror` on the project's own sources is clean and CI enforces it.
- **Dead code**: `Saturation::processChunk`'s `shapingOff` parameter (always `false`, re-tested
  on every oversampled sample) and `InstrumentBank::preparedBlock` are gone.

### Editor

- **Three parameters had no control at all.** `in_gain`, `out_gain` and `drum_level` were
  reachable only through host automation: the input and output *meters* were in the top bar,
  the trims that feed them were not. IN and OUT now sit beside their meters, DRUMS beside the
  instrument LEVEL knob. 42 knobs → 45, and `EditorProbe` asserts that every continuous
  parameter has one.
- **Ten knobs could only ever print "0" or "1".** Every 0..1 control (`sat_drive`, `sat_mix`,
  `comp_mix`, `dly_feedback`, `dly_mix`, `dly_damp`, `dly_width`, `rvb_damp`, `rvb_width`,
  `rvb_mix`) was drawn with zero decimal places, so `String(value, 0)` printed "0" for the
  bottom half of the range and "1" for the top. They read 0–100 % now. The eight frequency
  knobs printed a bare number and now say Hz; `img_width` and `rvb_size` say %. `EditorProbe`
  asserts every knob's readout differs at 25, 50 and 75 % of its range.
- **Opening the editor used to reset every parameter** once presets began restoring defaults.
  `juce::ComboBox::setSelectedId` notifies asynchronously by default, so the constructor's
  `presetCombo.setSelectedId(1)` fired `setCurrentProgram(0)` a few milliseconds after the
  window appeared. All three of the editor's own initial selections pass
  `dontSendNotification` now, and `EditorProbe` constructs the editor, runs the message loop
  and asserts that not one parameter moved. This was caught by the probe inside the same wave.
- `EditorProbe`: 9 → 12 checks.

- **The reported tail length was a fixed 5 s.** A host uses `getTailLengthSeconds()` to decide
  how long to keep calling `processBlock` after the transport stops. VERIFIED with an impulse:
  a 2 s delay at 0.95 feedback is still above −60 dB after **68 s**, and a 10 s reverb after
  4.68 s, against a reported 5 s. The figure is now computed from the settings
  (`1.25 × Decay + PreDelay + 0.3` for the reverb, `Time × ln(1000)/−ln(Feedback)` for the
  delay), floored at 6 s and capped at 30 s — the cap is deliberate, because the honest figure
  for extreme feedback runs to minutes and no host will render that. After: 6.00 / 12.81 /
  30.00 s for the three cases above.

### Measured and deliberately NOT changed

- **Reverb diffusers.** The four "allpasses" are not allpasses: `out = 0.75·d − 0.5·x` against
  a feedback of 0.5 gives |H| from −6.0 dB at DC to −1.6 dB at the line's Nyquist, which looked
  like the cause of the tank's 3.22 dB of tilt across 125 Hz..8 kHz. Rebuilding them as proper
  Schroeder allpasses and re-trimming the wet level to match made every number **worse**:
  late-field spread **3.22 → 3.97 dB**, L/R correlation of the tail **0.163 → 0.289**. The tilt
  is the comb bank's sparse low-frequency modal density; flattening it needs a different
  topology (an FDN), not a coefficient. Reverted.
- **Notes 35–49 are captured by the drums on every MIDI channel**, so the instrument cannot
  play B0–C#2 from an ordinary keyboard track. Narrowing the window would silently turn drum
  parts in saved sessions into synth parts.
- **Reverb pre-delay is applied after the tank**, so it moves the whole wet signal rather than
  only the onset. Changing it changes what every existing reverb setting sounds like.
- **Compressor attack is about twice the knob** (knob 1/10/50 ms → 2.83/20.33/94.83 ms to 63 %
  of the final gain reduction), because the detector takes `max(peak, 1.414·√RMS)` with an 8 ms
  RMS window. That hybrid is what makes the compressor ignore crest factor — a square and a
  sine at the same peak level are only 2.16 dB apart — so the knob's units are approximate
  rather than wrong.
- **The delay's always-on 0.4 Hz wobble.** Depth is `min(time × 0.0015, 1.5) ms` with no
  control. VERIFIED pitch swing on a 1 kHz tone: **1.55 cents** peak-to-peak at 100 ms,
  **6.76** at 500 ms, **13.47** at 2000 ms. Tape/BBD character; now documented and asserted so
  it cannot grow unnoticed.

### Repo hygiene

134 files / **66,836,259 bytes (63.7 MB)** removed from HEAD. All of it is still in git history
at `bed39b7` and is recoverable; nothing was rewritten or purged.

- `Assets/Sounds/source_zips/` (63 MB: two GareBear99 kits and a PD loop). No source file,
  CMake rule or CI step ever loaded them — `InstrumentBank` and `DrumEngine` synthesise every
  voice, and there is no `juce_add_binary_data`, no `AudioFormatManager` and no
  `createReaderFor` anywhere in `Source/`. The licence review (now recorded in `README.md`) had
  already concluded none of the three could be bundled. Restore with
  `git show bed39b7:Assets/Sounds/source_zips/<name> > <name>`.
- `screenshots/` (37 PNGs named after browser games), `.opencode/`,
  `.claude/skills/unity-cli/`, `Tests/block-blast-test.js`,
  `tools/{harness,balance,shot,dbg,patch-dbg}.js` — another project's leftovers.
- `AGENTS.md` (browser-game tooling), `IMPROVEMENT_LOG.md` (a `block-blast.html` changelog),
  `SWARM_BRIEF.md` (an HTML5 arcade brief), `WORKLOG.md` (a chess session),
  `tasks/{plan,todo}.md` (a Discord-clone spec) — six process documents, none about this
  plugin, and no `README.md` at all.
- There is now one `README.md` (what it is, how to build on Windows and Linux, how to run the
  tests, what every control does, the frozen sample-licensing decisions, ranked known issues),
  this `CHANGELOG.md`, and `AUDIT.md`. `HANDOVER.md`'s real content moved into the README; its
  stale claims are gone (it told the reader to expect 30 checks from a 151-check suite,
  described the UI note queue as `CriticalSection`-guarded when it is an `AbstractFifo`, and
  listed `IS_SYNTH FALSE` when `CMakeLists.txt` sets it `TRUE`).
- Kept: `tools/build_plugin.bat`, `build_probe.bat`, `build_smoke.bat` (all still name targets
  that exist) and `build_assets.ps1` (the maintainer's, though unfinished — its manifest
  entries all have an empty `id` and `file` and nothing consumes its output).

### Behaviour changes

- Enabling the imager at its default no longer doubles the side signal (6 dB less side).
- Selecting a host preset now resets every parameter to its default first. Loading a saved
  session is unaffected.
- Tube saturation is 1.6 dB louder at low drive with a different harmonic balance. Presets 2,
  7 and 11 use it.
- Drum notes 37/39/40, 41/43/45/47/48/50, 49/51/52/53/55/57/59 and 60/61 all sound different,
  because they used to be duplicates of 36, 38 or 42. Notes 36, 38 and 42/44/46 keep their
  character. Voice levels were re-voiced so every note's peak is within 4 dB of the old
  build's kick (−4.3 dBFS at velocity 1 with Drum Level at 0 dB).
- Saw-based instrument programs have far less aliasing, which makes high notes sound cleaner
  and slightly less bright.


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
