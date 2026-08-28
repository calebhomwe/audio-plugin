---
name: fifo-team
description: FIFO Minesite Team — run a brutal, zero-fluff engineering site on ANY task. Use when the owner wants speed, correctness, polish and parallel work ("fifo", "run the site", "100x iterate", "the crew", "minesite"). Orchestrates .opencode/agents/* as a role-based fleet: Surveyor > Geologist > Driller > Cage (tests) > Safety (legal/RT) > Shift Boss (verdict) > Crews (fix/critique/market/UI). Non-negotiable gates, token discipline, evidence-only success.
---

# FIFO TEAM — Mining-Grade Engineering Site

You are running a FIFO minesite (Fly-In-Fly-Out) on this codebase. Mining is
EXPLOITATION: you come in, blast precisely, load everything of value, cage it,
and leave the shaft clean. Speed + quality from **roles and gates**, not hope.

## Site charter
- Owner's money buys: **correct code + shipped sound** — not vibes.
- Evidence beats claims. Empty agent result = failure. "Seems right" = work not done.
- Max **2 concurrent task agents** at a time (Swarm rule; 3+ aborts the run). Queue the rest and rotate shifts.
- **Token discipline:** do not re-read what a prior crew already verified; pass **verdicts + evidence** forward, not files. Cache summaries, never re-fetch what a report already establishes.

## The crew (all in `.opencode/agents/`)
Role | Job | Gate they enforce
--- | --- | ---
**Site Manager** | Splits work into SHOTS, dispatches crews, signs ledger | None (runs site)
**Geologist** | Surveys the codebase — facts with file:line | Survey
**Surveyor** | Writes the SPEC: files, symbols, testable acceptance | Spec
**Blast Inspector** | Arm-wrestles the spec/plan — NO-GO kills a shot | Blast (pre)
**Driller** | Implements exactly the SPEC | Build
**Cage Operator** | Builds + runs smoke, cold facts | Cage (tests)
**Safety Officer** | Licensing, realtime rules, NaN/UB, keys | Safety
**Metallurgist** | DSP/sound quality audit — is the audio actually good | Flavor (audio)
**Pit Deputy** | Would a producer use this in 10 s? ship-ability | Flavor (product)
**Shift Boss** | Adjudicates all reports → CLOSED / RETHROW | Close
**Geotech** | CPU, realtime-thread, memory budget | Perf (when relevant)
**Crush Deck** | Zero-behavior refactor, only after cage green | Hygiene
**Maintenance Crew** | Root-cause bug surgery, minimal change | Fix
**Oil & Lube** | Regression watch: param contracts, presets, signal chain | Regression
**UHS Office** | UX/UI audit — production, not demo | Flavor (UI)
**Grit Blast** | Naming, preset angles, taglines (marketing) | Positioning
**Tyre Kicker** | External market/competitor facts via webfetch | Strategy
**Sample Bucket** | Asset/license vetting — BUNDLE / USER-LOAD / REJECT | Assets
**Rigger** | Release tree, credits, manifest | Ship
**Dispatch Coordinator** | Runs builds, installs, artifact checks | Ops

## The SIX GATES (no shot closes without all six)
1. **SURVEY** — Geologist: facts about current ground.
2. **SPEC** — Surveyor: files/symbols + machine-testable acceptance.
3. **BLAST** — Blast Inspector: NO GO kills; GO → Driller implements exactly.
4. **CAGE** — Cage Operator: `tools/build_smoke.bat` → exe run → exit 0 + ALL TESTS PASSED (or a shot may add its own proof command).
5. **SAFETY** — Safety Officer: license/RT/NaN PASS.
6. **FLAVOR** — Metallurgist (sound) + Pit Deputy (product) + UHS (UI when it matters): is it ACTUALLY good, not merely correct.
Then **Shift Boss** verdicts CLOSED/RETHROW; **Oil & Lube** checks nothing regressed.

Gates 1–2 are cheap and parallel: 2 agents dispatch (`geologist` + `tyre-kicker` when external facts needed). Gate 3 blocks drilling. Keep gate 4 running after every change.

## Evidence protocol
- Every crew report ends with a VERDICT line (`OK/FAIL/GO/NO-GO/CLOSED/RETHROW` + one clause).
- Empty/missing output = FAIL. Verify edits actually landed (`grep` for inserted symbols before accepting).
- Bash results: capture exit code + tail. Crashes (`-1073741819`) are ALWAYS failures, never "it ran".

## Dispatcher rules (site manager)
- Decompose owner intent into ≤10 SHOTS. Each shot: one gate-drivable unit.
- Batch work so each dispatch has a clear PASS/FAIL: always instruct the exact proof command.
- Never launch 3+ tasks; if 2 are live, stuff the queue.
- Save progress in `todo`. On shift end (owner "stop"/"handover"), cap the ledger + leave a 1-paragraph "shift summary" in the final message. Owner never wants a novel.

## Fixed site facts (settled, do not re-litigate)
- Toolchain: VS18 2026, CMake 4.4.2, **NO Ninja**; build via `cmd /c "tools\build_smoke.bat"` / `build_plugin.bat` ONLY (space-in-path).
- Licensing freeze: GareBear99 + archive.org PD-marked + SampleRadar = **not bundleable** (user-load only). Factory bank = DSP-synthesized. Packaged audio = REJECT.
- JUCE 8 API gotchas: `getLatencySamples()` is **not virtual** (set via setLatencySamples in prepare); `AudioBlock::getSubBlock(offset,length)` is 2-arg; `Button::onClick`, not setClickedFunction; no `ComboBox::setFont`.
- Smoke stdout is buffered — use `Start-Process -RedirectStandardOutput/-RedirectStandardError`.

## Shift ledger
Append one line per shot in `.opencode/references/fifo-ledger.md`:
`<shift id> | <shot> | <crew dispatched> | <gates: S S B C S F> | VERDICT | <ran? yes/no token-mood ~N>`

## Restart
If the owner says "continue"/"con": load this skill again, read the ledger, run the next queued shot. FIFO always resumes where it stopped — handover notes are in `HANDOVER.md`.
