# WORKLOG — Chess Juice build session (2026-08-28)

## What shipped (games/chess.html)
- **Chess-engine rules**: full legal-move engine w/ castling, en passant, promotion; fifty-move, threefold repetition (positionKey/recordPosition), insufficient-material draws — all wired into executeMove + undo snapshots.
- **18→22 puzzles, all machine-verified**: 10 original SAN solutions converted to coordinate via engine (1 broken one dropped); +12 classic mate/fork/pin positions (battery, smothered, back-rank, knight-fork, double-cover corner mates, ladder, en-passant, promotion, knight fork). Verifier: legal move exists + mate1 really mates.
- **175 tips + ACADEMY**: openings(20), tactics(12), mates(8), endgame(8), strategy(8), resources(12), traps(12), drills(10), exercises(8), mistakes(10).
- **v3 knowledge bomb**: 12 real historical combos (Légal, Immortal, Evergreen, Opera, Game of the Century, Marshall–Levitsky, Réti–Tartakower, Boden, Kasparov–Topalov, Philidor's Legacy, Anastasia, Damiano) + opening route trees + endgame commandments + rapid-fire quiz. 🧠 Learn panel (tabs, Escape-closable).
- **Game Review** (chess.com-style): 📊 button replays last 60 half-moves, classifies ⭐/✓/?!/??/🔥 via eval deltas, click row → board arrow. Async-chunked (8 moves/4ms tick).
- **📈 persistent Stats**: localStorage `chessj__stats` — games, W/L/D, streak/best, puzzles solved, fastest mate; panel + reset; counting hooked at all 5 game-over branches + puzzle-solve.
- **chess.com feel**: bot no longer pre-moves White at boot (setup line "🤖 You play White · Bot is Black"); labeled buttons with tooltips; default clean board (teach off); promotion modal w/ pieces + cancel + tap-outside; dots/rings for legal moves; source-square outline for last move; theme pipeline bug FIXED (cssCache key mismatch made the board render monochrome); 14 themes; drag & drop; mute 🔊/🔇; touch/click dedupe (400ms).
- **Audio**: synthesized engine (compressor master chain) — wooden move thock, crunchy capture+sub, check alarm, checkmate fanfare, castle double-thock, sparkle promote, win jingle, hint, combo; all muted-safe + try/catch.

## Bugs found & fixed
1. `scheduleBotMove()` at init → bot played White's first move. Gated by `game.turn === botColor`.
2. Theme CSS cache key mismatch (`--lt` stored vs `lt` read) → monochrome unplayable board.
3. Original 12 puzzles used SAN solutions vs coordinate matcher → unsolvable. Converted; `Qa4` puzzle dropped (illegal in its own FEN).
4. Puzzle mode left bot enabled → race conditions (bot solving/freeze on wrong-move undo). Bot disabled in puzzles, restored on exit.
5. Undo could strand position on bot turn w/o rescheduling.
6. `muteIcon` span missing → Uncaught TypeError at load when muted persisted from a prior session. Handler null-guarded + button span ensured.
7. Pre-existing partial stats block (different panel ids) + my duplicate listener → rewritten `toggleStatsPanel` to real ids (statGames/Record/Streak/Best/Puzzles/Mate), reset wired, statMate row added.

## Verification (latest, all green)
- deep-verify.js: 15P 0F — 20/20 bot-vs-bot games terminate w/o crash; 22/22 puzzles verified; draw-rule + key-stability unit checks.
- harness: chess ok:true 0 errors · Block Blast 18P 0F.
- Headless Edge: runtime clean + interactive probe: Learn/routes/stats/mute/select/move/review/undo all pass, board intact.
- node --check inline script: OK after every patch (auto-revert gate on failure).

## Process notes
- 5 of 6 dispatched "author/verify-loop" agents returned EMPTY (general agent unreliable on multi-step authoring under API flakiness incl. one ENOTFOUND outage) — direct implementation beat re-dispatch; keep swarm agents for read-only audits (UX review agent paid off: found the monochrome + bot-premove criticals).
- Restore-from-desktop cycles silently reverted drag-and-drop + mute button once — re-verify feature presence after any copy, not just syntax.
