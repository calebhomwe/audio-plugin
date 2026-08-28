# AGENTS.md — working notes for AI sessions

## Project
- Phone-first HTML5 arcade hub: 37 single-file games in `games/` + hub `index.html` at project root.
- Also a JUCE 8.0.9 C++ audio plugin **MixAgent** (CMake FetchContent; VST3 + Standalone targets, `Tests/SmokeTest.cpp`).
- Shared swarm spec: `SWARM_BRIEF.md` — **new agents read it first** instead of re-receiving the full prompt.

## Environment
- Windows, PowerShell 5.1, Node v25. **No Python.** `rg` not installed — use the Grep tool.
- `gh` CLI authenticated as `calebhomwe`.
- Headless Edge: `C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe`.
- Local servers: `node C:\Users\code\AppData\Local\Temp\opencode\server.js <root> <port>` — 8123 = project root, 8125 = `repos\`. If dead, start with Start-Process.

## Verification
0. Fast in-process smoke test (no browser): `node tools/harness.js games/foo.html [frames]` — stubs DOM/canvas/audio, runs inline script, drives rAF + synthetic input, prints JSON, exit 1 on runtime error. Batch all games by looping `Get-ChildItem games -Filter *.html`.
1. Extract inline `<script>` to a temp file with `[System.IO.File]::WriteAllText($p,$js,(New-Object System.Text.UTF8Encoding($false)))` (UTF-8 **no BOM**), then `node --check $p` — must pass.
2. Headless Edge runtime scan (check stderr for Uncaught/ReferenceError/TypeError/SyntaxError):
   ```powershell
   Start-Process -FilePath "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" `
     -ArgumentList '--headless=new','--disable-gpu','--enable-logging=stderr','--virtual-time-budget=4000','--dump-dom','<url>' `
     -RedirectStandardOutput $o -RedirectStandardError $e -NoNewWindow -Wait
   ```
   Give each run a unique `--user-data-dir`. (If Start-Process yields empty output, run via a small .bat wrapper — see `C:\Users\code\AppData\Local\Temp\opencode\edge-scan.bat`.)
3. Balance gauntlet (headless bot playthroughs): `node tools/balance.js [sims]` — stubs DOM like the harness, drives `update()` with a kiting bot, prints aggregate JSON (wins, median survived, hp samples). Target: simplistic bot wins ~40-50%. Seeded per sim index → reproducible.
4. Gameplay screenshots: `--virtual-time-budget` drives **timers but NOT rAF** — the game loop barely advances. Inject a temp `<script>` that calls `startGame()` then loops `update(1/30)` manually (auto-pick `applyChoice(currentChoices[0])` on `state==='levelup'`, click `chestBtn.onclick` on `state==='chest'`), then `--screenshot`.

## PowerShell gotchas (hard-won)
- Never put JS with `${...}` inside double-quoted PS strings (PS interpolates `${}`); use single-quoted strings or the Write tool.
- `Set-Content -Encoding UTF8` adds a BOM — use `[System.IO.File]::WriteAllText` with `UTF8Encoding($false)`.
- Native-app stderr mangles with `2>&1` — use `Start-Process -RedirectStandardError`.

## Swarm rules
- Max **2 concurrent** Task agents (3+ aborts).
- Empty agent result = failure — verify edits actually exist (grep for the inserted code) before reporting.
- Separate human/agent sessions may edit the same file live (2026-08-26: `games/survivor-wave.html` grew 24KB→92KB mid-session). Before editing a hot file: check mtime twice ~6s apart; re-read right before editing; prefer small surgical edits (exact-string `edit` survives concurrent writes better than full rewrites).

## Audio
- 12 CC0 WAV SFX in `games/assets/sfx/` (Juhani Junkala, OpenGameArt, CC0) + `MANIFEST.json`.
- Standard pattern: `SFXP` preload map + `sfx(name)` with WebAudio `tone()` fallback. Games use persisted muted flags.

## Performance baseline (already applied — keep it)
No per-frame shadowBlur/gradients/allocations; cached sprites; particle pools; rAF state-gating; `visibilitychange` pauses. Details in `SWARM_BRIEF.md`.

## Agent skills (addyosmani/agent-skills)
24 lifecycle skills in `.opencode/skills/`, shared checklists in `.opencode/references/`.
- Before acting, check if a skill applies; if it does, invoke it with the `skill` tool and follow it exactly.
- Mapping: define→`spec-driven-development`/`interview-me`/`idea-refine`; plan→`planning-and-task-breakdown`; build→`incremental-implementation`+`test-driven-development`; verify→`debugging-and-error-recovery`; review→`code-review-and-quality`; ship→`shipping-and-launch`/`git-workflow-and-versioning`.
- Meta-skill `using-agent-skills` governs discovery. Verification in skills is non-negotiable — "seems right" never counts.

## JUCE build (see `.opencode/skills/juce-plugin-build/SKILL.md`)
```
cmake -B build -S .
cmake --build build --config Release
.\build\MixAgentSmokeTest_artefacts\Release\MixAgentSmokeTest.exe
cmake --install build
```
Build dirs are gitignored.

## Publishing (GitHub Pages, public)
LIVE: **https://calebhomwe.github.io/arcade-hub/** (repo `calebhomwe/arcade-hub`, public).
1. Copy `index.html` + `games/*.html` + `games/assets/sfx/` ONLY (selective copy — `games/` also contains a stray `BloxburgLite` folder and possibly a stray `.git`; never publish those) to a clean temp dir, `git init -b main`, commit.
2. `gh repo create <name> --public`, push.
3. `gh api repos/calebhomwe/<name>/pages -X POST -f source[branch]=main -f source[path]=/`
4. Update: re-copy into the temp publish dir, commit, `git push`.
- The root repo's `.gitignore` intentionally still ignores `games/` and `index.html` (root repo = JUCE plugin repo; Pages publish is a separate clean copy).
- Screenshot gallery: `screenshots\gallery.html` (38 phone shots); regen via `C:\Users\code\AppData\Local\Temp\opencode\screenshots-and-probe.ps1` (also writes pixel-probe stats to `...\visual-probe.txt`).
