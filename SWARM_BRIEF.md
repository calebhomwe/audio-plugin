# SWARM BRIEF — READ THIS FIRST (shared spec, do NOT re-ask)

## Mission
Phone-first HTML5 arcade hub (YouTube-playables style). All games are single-file HTML at
`C:\Users\code\Documents\Default Project\games\` (37 files), hub at `...\index.html`.
User repo playables: `...\repos\neon-game-arcade\games\skywalker-playables\` (24 files).
Goal: fast on phones, zero runtime errors, real CC0 SFX (no synth beeps), then publish to GitHub Pages.

## SFX (downloaded, CC0 — Juhani Junkala, OpenGameArt "512 Sound Effects (8-bit style)")
`games\assets\sfx\` = tap, pop, coin, hit, boom, jump, whoosh, win, lose, buzz, levelup, tick (.wav) + MANIFEST.json.
Relative path `assets/sfx/<name>.wav` works from `games/<f>.html` (localhost + GitHub Pages).
Standard loader (insert near each game's tone()/AC; adapt mute var name; KEEP tone() as fallback):

const SFXP={};['tap','pop','coin','hit','boom','jump','whoosh','win','lose','buzz','levelup','tick'].forEach(function(n){try{var a=new Audio('assets/sfx/'+n+'.wav');a.preload='auto';a.volume=0.5;SFXP[n]=a}catch(e){}});
function sfx(n){if(muted)return;var a=SFXP[n];if(!a)return;try{a.currentTime=0;var p=a.play();if(p&&p.catch)p.catch(function(){})}catch(e){}}

Semantic map: tap/click/buy/keypress→tap · pop/merge/place/drop→pop · coin/score/pickup/earn/eat/harvest→coin · hit/whack/kill/impact/attack→hit · boom/explode/boss/dyna→boom · jump/flap→jump · whoosh/swipe/hint/deny-soft→whoosh · win/victory/fanfare/ascend/prestige→win · lose/die/death/game-over→lose · wrong/error/miss/blocked→buzz · tick/countdown→tick · combo/milestone/levelup/unlock→levelup.
Rules: replace multi-tone setTimeout arpeggios with ONE sfx() call; never delete tone() (fallback); never change mute button/storage key; farm games use `state.muted` (keep in save object).

## Perf rules (mobile-first)
- No per-frame ctx.shadowBlur → bake glow into offscreen sprite per color, drawImage.
- No per-frame createLinear/RadialGradient → cache; rebuild only on resize.
- No per-frame allocations → pools, swap-pop removal, caps (particles ~300-400, floats ~32).
- No per-frame getBoundingClientRect/innerWidth → cache on resize.
- No innerHTML rebuild per tick → in-place textContent/transform; rebuild only on new game.
- Emoji fillText per frame → pre-render to offscreen canvas once.
- rAF: gate heavy work on state==='play'; draw once when idle; bail on document.hidden; dt clamp.
- Timers (setInterval countdowns): visibilitychange pause; clear on game over.
- touch listeners: passive:true unless preventDefault needed.

## Verification recipe (EVERY edited file)
1. Extract inline <script> content to temp: `[System.IO.File]::WriteAllText($p,$js,(New-Object System.Text.UTF8Encoding($false)))` then `node --check $p` → must pass.
2. Headless Edge runtime scan:
   Start-Process -FilePath "C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe" -ArgumentList '--headless=new','--disable-gpu','--enable-logging=stderr','--virtual-time-budget=4000','--dump-dom','http://localhost:8123/games/<f>.html' -RedirectStandardOutput $out -RedirectStandardError $err -NoNewWindow -Wait
   then check $err for Uncaught/ReferenceError/TypeError/SyntaxError.
   Repo playables: http://localhost:8125/neon-game-arcade/games/skywalker-playables/<f>
   Use a unique --user-data-dir per run to avoid profile lock conflicts.

## PowerShell gotchas (HARD-WON — follow exactly)
- NEVER put JS with ${...} inside double-quoted PS strings (PS interpolates ${}). Use single-quoted PS strings or the Write tool.
- `Set-Content -Encoding UTF8` adds a BOM → use [System.IO.File]::WriteAllText with UTF8Encoding($false).
- No Python on this box. `rg` not installed — use the Grep tool.
- Native app stderr mangles with `2>&1` → use Start-Process -RedirectStandardError.
- Servers: node server.js <root> <port> at C:\Users\code\AppData\Local\Temp\opencode\server.js (8123=project root, 8125=repos\). If a server is dead: `node C:\Users\code\AppData\Local\Temp\opencode\server.js "<root>" <port>` in a Start-Process.

## Hard rules
- Gameplay/economy/scoring math must stay IDENTICAL. No new dependencies. Single-file games stay single-file.
- Never commit secrets. No API keys in files.
- If you return an empty result, you failed — verify your edits actually exist (grep for the inserted code) before reporting.

## Report format
Table: file → changes (line refs) → node --check → headless. One line per file. No prose.
