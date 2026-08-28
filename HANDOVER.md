# HANDOVER — MixAgent (Trap/Rap Instrument + FX Plugin)

Last updated: 2026-08-21. Read this top-to-bottom; then `AGENTS.md` for env rules, `SWARM_BRIEF.md` for shared swarm context.

## 1. What this is

A JUCE 8.0.9 C++ audio plugin (VST3 + Standalone) for trap/rap production. Two fused roles:

1. **Instrument bank (the star)** — `Source/DSP/InstrumentBank.h`: multi-timbral chromatic synth, 16 DSP-synthesized programs (Pluck, Bell, Keys, Lead, Pad, Brass, Strings, E.Piano, Organ, Sub, DarkBell, GlassPluck, VoxChoir, SoftSoul, BounceKeys, RageLead). Fully synthesized = zero licensing risk, every factory sound is ours.
2. **FX master strip** — 7-band EQ → Saturation (4× oversampled) → Compressor → Stereo Imager → Delay → Reverb → Limiter. Instrument bus renders INTO the strip like an insert (drums/instruments feed the FX chain).
3. **Drum one-shots (small extra)** — `Source/DSP/DrumEngine.h`: synth kick/snare/cymbals, GM-style note map. Not 808-focused (808 work was deliberately de-prioritized per owner).

**Product direction (owner-mandated, repeated loudly):** SOUNDS and INSTRUMENTS are the priority. NOT 808 bass. NOT just an FX strip. Think Nexus-style bank, HeatUp-style glue.

## 2. Build (verified working)

Toolchain: **VS 18 Enterprise (2026), cl 19.51** + CMake 4.4.2. **Ninja is NOT installed** (a swarm agent's claim otherwise was false — don't trust it). JUCE 8.0.9 is cached in `build\_deps\juce-src` (no network needed).

```powershell
# smoke test (build + run):
cmd /c "tools\build_smoke.bat"
.\buildDrum\MixAgentSmokeTest_artefacts\Debug\MixAgentSmokeTest.exe   # expect: ALL TESTS PASSED (30 checks)

# full plugin:
cmd /c "tools\build_plugin.bat"
```

Artifacts:
- VST3: `buildDrum\MixAgent_artefacts\Debug\VST3\MixAgent.vst3` (auto-copied to `C:\Program Files\Common Files\VST3\MixAgent.vst3`)
- Standalone: `buildDrum\MixAgent_artefacts\Debug\Standalone\MixAgent.exe`

Build dir: `buildDrum/` (generator `Visual Studio 18 2026` -A x64, Debug). Stale generator in cache = wipe `buildDrum` and rerun. Paths contain a space ("Default Project") — always wrap cmake in the .bat wrappers, never inline in PowerShell.

## 3. File map

| File | Role |
|---|---|
| `Source/PluginProcessor.h/.cpp` | APVTS params (all `*_enabled/freq/...` + `inst_*`), FX chain, MIDI routing, preset bank (12), UI note queue (`uiNoteOn/Off` under `CriticalSection`), `getAPVTS()` |
| `Source/DSP/InstrumentBank.h` | **Priority.** 16 programs, 24-voice poly, per-program Recipe (osc count/detune, FM ratio/amount, one-pole LP, ADSR rates). `renderAdd()` mixes into host buffer. Program switch changes shared recipe — in-flight voices keep going on the new recipe (accepted behavior) |
| `Source/DSP/DrumEngine.h` | Drum one-shots only (kick 35/36/37/41/60, snare 38/39/40/49/57, else cymbal). 24-voice pools, round-robin, tails run to natural end. (Bass family + `bass_*` params removed — see §7 FIXED) |
| `Source/DSP/Saturation.h` | 4× oversampled (2×2 polyphase FIR). **Fixed this session:** `process()` chunks oversized blocks into `maxSamplesToProcess` (host may send > promised block; was a real OOB crash, regression-tested) |
| `Source/DSP/{EQ,Compressor,StereoImager,Delay,Reverb,Limiter,Biquad,Common}.h` | FX modules. Pattern: `prepare/process/reset/setEnabled` + APVTS-driven setters. **Don't touch lightly** — smoke test covers them |
| `Source/UI/PadGrid.h` | 12-pad chromatic preview keyboard (C3–B3), mouse triggers `proc.uiNoteOn`, blinks on `getInstrumentActive()` |
| `Source/UI/{Knob,Meter,Spectrum,Style}.h` | Custom widgets, `agm::ui::` namespace |
| `Source/PluginEditor.h/.cpp` | 1160×900. Top bar + EQ section (spectrum + 7 knob columns) + 6 FX panels + bottom **INSTRUMENT LIBRARY** strip (power, program ComboBox, level knob, keyboard). `fullyBuilt` guard + explicit `resized()` at ctor end (see §7 FIXED) |
| `Tests/SmokeTest.cpp` | 16 `check()` sites, 30 executed (per-program loop ×16 programs): FX transparency/EQ/comp/lim, state roundtrip, favorites save/load, oversized-block regression, all 16 programs finite+bounded |
| `Tests/EditorProbe.cpp` + `tools/build_probe.bat` + CMakeLists `EditorProbe` target | Editor smoke test: constructs editor & drives resize, catching the resized()-before-members crash class. `cmd /c "tools\build_probe.bat"` → `buildDrum\EditorProbe_artefacts\Debug\EditorProbe.exe` |
| `tools/build_smoke.bat`, `tools/build_plugin.bat` | Verified build wrappers (vcvars + cmake, space-safe) |
| `tools/build_assets.ps1` | **WIP, don't trust yet** — asset extraction/manifest from earlier plan |
| `Assets/Sounds/source_zips/` | Downloaded free kits (GareBear99 808+drum, PD loop). **DO NOT BUNDLE** — see §6 |

## 4. Audio flow

```
MIDI noteOn ──┬─ note 35–49 → DrumEngine   ─┐
              └─ everything else → InstrumentBank ─→ renderAdd() into host buffer (pre-FX)
Host input ──→ InGain ──→ EQ ─→ Sat ─→ Comp ─→ Imager ─→ Delay ─→ Reverb ─→ Limiter ─→ OutGain ─→ out
                              (instruments feed the chain like an insert)
```

- MIDI: `acceptsMidi()=true`, parsed in `processBlock` (host + UI queue drained under lock).
- UI notes: editor → `proc.uiNoteOn/Off` → `MidiBuffer` under `CriticalSection` → drained on audio thread. Never write DSP state from UI thread directly.
- Presets (12, via program change): 0–5 FX-only (Init, Clean Master, Vocal Presence, Drum Bus Punch, Wide & Spacey, Warm Tape); **6–11 trap subgenre combos** (Drill Bell, Rage Lead, Jersey Keys, Plugg Pad, BoomBap EP, Sub Glue) = `inst_program` + tuned FX chain. Note: FX presets don't reset instrument state (they layer).

## 5. Test status

`MixAgentSmokeTest.exe` → **30/30 PASS** as of this handover (16 check sites; the per-program loop runs 16 checks). Run it after ANY DSP change. It's fast (~2s).

## 6. Legal (frozen decisions — do not re-litigate)

License swarm verified (headers + license text read):
- **GareBear99 kits: SHIP-NO as embedded content.** "Free to use in commercial and non-commercial productions. No credit required. **Do not redistribute the raw samples**" — embedding = redistribution. User-loadable only.
- **archive.org 150bpm loop: SHIP-NO.** PD Mark is self-declared by a non-author uploader ("Downloaded from Samplefocus.com") — unauditable provenance.
- **SampleRadar packs (not downloaded, rejected):** "royalty-free" = use in compositions, NOT redistribution.
- **Factory bank = 100% DSP-synthesized** → no attribution needed, no takedown risk. This is why InstrumentBank synthesizes instead of sampling.
- `Assets/Sounds/source_zips/` may stay on disk for R&D, never enter the installer. `Assets/Credits/` is empty — if we ever ship user-loadable-pack docs, credits files must exist (per-file for CC-BY).

## 7. Known issues / debt (ranked)

1. **CMake `IS_SYNTH FALSE`** while plugin now emits MIDI audio — consider `TRUE` so hosts categorize it as instrument (changes standalone defaults; test smoke after).
2. **InstrumentBank single shared Recipe** — program switch mutates in-flight voices. Snapshot recipe per voice for correctness.
3. **No user WAV loading** — the "bring your own one-shots" slot (AudioFormatManager on message thread → RAM → voice) is designed (see swarm spec in git history) but not built.
4. **Debug build only** — no Release config exercised; `tools/build_*.bat` hardcode Debug.
5. Browser (Hub side): no search/audition-on-hover (product critic's top Nexus features). Favorites exist and persist in the plugin.
6. `tools/build_assets.ps1` is abandoned WIP from the sample-pack plan.

### FIXED (recent)

- **PDC latency** — `setLatencySamples()` now = limiter + saturation latency (`PluginProcessor.cpp`); host-reported latency correct (was broken/shadowed).
- **Favorites ValueTree nesting bug** — `getFavorites()` rewrite; favorites survive save/load roundtrip (smoke: favorites set/toggle/persist).
- **Editor startup crash** — root cause: JUCE `AudioProcessorEditor` base fires `resized()` before members are built; fixed with `fullyBuilt` guard + explicit `resized()` at ctor end; `EditorProbe` regression-covers it.
- **Saturation oversized-block OOB** — chunk processing (`processChunk`) for blocks > prepared size; regression-tested in smoke.
- **DrumEngine bass cleanup** — dead `bass_*` params + `Family::Bass` removed; DrumEngine is one-shots only now.

## 8. Roadmap (next value, in order)

1. ~~Fix PDC~~ — **DONE** (§7 FIXED).
2. ~~Clean dead code~~ — **DONE** (bass family removed, §7 FIXED).
3. User-loadable WAV slots for drums (message-thread decode, non-bundled).
4. Browser upgrade: audition on hover (fire note 60 on ComboBox change), favorites, search (favorites already persist in plugin).
5. ~~More programs: reese bass, FM lead, arpeggiated pluck, bell with strike variation~~ — **DONE**: bank grew 10→16 (Dark Bell, Glass Pluck, Vox Choir, Soft Soul, Bounce, Rage).
6. Release build + artifact copy script; version bump to 1.0.0-rc.
7. Naming/branding: "MixAgent" undersells a trap instrument — decide before any release.

## 9. Env gotchas (hard-won, this session)

- PowerShell + space-in-path + `cmd /c` = quoting hell. Use the .bat wrappers.
- `Set-Content -Encoding UTF8` adds BOM — use `[System.IO.File]::WriteAllText` with `UTF8Encoding($false)`.
- `rg` not installed → use the Grep tool. No Python. Node v25 available.
- Headless Edge at `C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe` (for the HTML side of this repo, if relevant).
- JUCE source for API checks: `build\_deps\juce-src\modules\...` (e.g. `juce_dsp\containers\juce_AudioBlock.h` has `getSubBlock(offset, length)` — note: 2-arg, NOT (channel, offset, length); `juce_AudioBuffer.h` has NO view-constructor).
- Smoke test stdout is buffered — on crash, `check()` lines are lost; use `Start-Process -RedirectStandardError` + stderr breadcrumbs to localize.
- Limiter prints `LIM PREPARE/LIM GETLAT` to stderr — noise, not errors.
- Editor crash/deadlock issues: `cmd /c "tools\build_probe.bat"` → `buildDrum\EditorProbe_artefacts\Debug\EditorProbe.exe` (needs a GUI session / --headless-friendly run; it constructs the editor and pumps resize).

## 10. Session history (what happened when)

- MixAgent FX strip pre-existed (6 presets, smoke suite).
- Owner asked: "trap/rap plugin, awesome, like HeatUp/Nexus."
- Swarm #1: verified free sound sources (all rejected for bundling, §6).
- Swarm #2 (2 harsh critics + 2 fact researchers): killed sample bundling, flagged PDC/voice-model/staging, produced DSP spec + asset plan.
- Owner corrections (loud): **808s NOT priority → SOUNDS/INSTRUMENTS priority, NO 808s.**
- Built: InstrumentBank (10 programs) + browser UI + drums-as-extra; smoke grew 9→21; Saturation chunking fix; 12-preset bank; VST3+Standalone building clean; standalone launches OK.
- **2026-08-27 fleet audit:** bank 10→16 programs; editor 1160×900; PDC latency, favorites nesting bug, editor startup crash (resized-before-built → `fullyBuilt` guard), Saturation OOB — all fixed & smoke-covered; `EditorProbe` target added; DrumEngine bass family deleted; smoke 30/30.
