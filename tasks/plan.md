# Implementation Plan: Unity — Discord-style app, better

## Overview
Single-device web app demo of a Discord-class chat/community platform named **Unity**.
Polished local demo (simulated users/messages), equal-effort desktop + phone layouts,
real in-browser screen streaming (`getDisplayMedia`), real LAN cross-device screen viewing
via a small Node WebSocket relay, simulated remote-control overlay, Teams-style meetings +
calendar. All Nitro perks free. No backend for chat — state is seeded, persisted to
localStorage, pub/sub driven.

## Architecture Decisions
- **Plain ES modules-free classic scripts** (`window.Unity` namespace) — no build step, works from `file://` and the repo's static server.
- **One DOM, two layouts**: desktop 4-pane grid and phone layout are achieved with responsive CSS over the same DOM (no duplicate mobile code).
- **Store contract first** (`js/store.js`) so feature agents can build in parallel against a frozen API.
- Feature modules self-register: `Unity.feature(id, { init(ctx), mount(el), unmount() })`; `js/app.js` mounts them into fixed shell slots.
- Perf rules from `SWARM_BRIEF.md` apply (no per-frame shadows/gradients/allocations; visibilitychange pauses).

## File map / ownership
| File | Owner | Purpose |
|---|---|---|
| `unity/index.html` | Wave 0 | Shell, script tags, layout slots |
| `unity/css/unity.css` | Wave 0 | Design system + desktop grid |
| `unity/css/mobile.css` | Wave 0 stub → Wave 4 | Phone layout |
| `unity/js/store.js` | Wave 0 | State, seed data, persistence, pub/sub API |
| `unity/js/ui.js` | Wave 0 | DOM builder, avatars, time, toast helpers |
| `unity/js/app.js` | Wave 0 | Registry, router, mounting |
| `unity/js/features/chat.js` | Agent A | Messages, composer, reactions, replies, threads, pins, search, slash commands |
| `unity/js/features/community.js` | Agent B | Members, roles, moderation, invites, bots |
| `unity/js/features/dms.js` | Agent B | DMs, group DMs, friends |
| `unity/js/features/voice.js` | Agent C | Voice channels, REAL Go Live screen share, simulated streams, remote-control overlay |
| `unity/js/features/meetings.js` | Agent D | Teams-style meetings, scheduling, calendar view, meeting stage |
| `unity/js/features/settings.js` | Agent D | Settings, profiles, themes, "Unity Plus" free-perks panel |
| `unity/relay/unity-relay.js` | Wave 3 | Node WS relay (no deps) for LAN viewing |
| `unity/relay/host.html`, `viewer.html` | Wave 3 | Real cross-device screen share pages |

## Frozen store contract (agents must not change)
```js
Unity.state = {
  me: {id:'u-you', name, tag, avatarHue, status, customStatus, plus:true},
  users: [{id,name,tag,avatarHue,status:'online|idle|dnd|offline',bot,roleIds[],bio}],
  servers: [{id,name,iconHue,categories:[{id,name,channels:[{id,name,kind:'text'|'voice'|'stage',topic}]}], memberIds[] , roles:[{id,name,color,hue,perms}]}],
  dms: [{id, kind:'dm'|'group', userIds[], messages:[...]}],
  meetings: [{id,title,serverId,startsAt,durationMin,attendeeIds[],stage}],
  ui: {view:'server'|'dm'|'meetings'|'calendar', serverId, channelId, dmId, threadId, theme:'aurora'|'light'}
}
// messages: {id, userId, ts, text, reactions:{emoji:count}, replyTo?, pinned?, edited?}
API:
  Unity.store.get() -> state            Unity.store.emit()             // call after any mutation
  Unity.store.on(fn)->unsub             Unity.store.save()             // persist localStorage 'unity-state-v1'
  Unity.store.reset()                   Unity.seed.build()             // fresh seed
Helpers: Unity.ui.el(tag, cls, text), Unity.ui.avatar(user, size), Unity.ui.time(ts),
         Unity.ui.toast(msg), Unity.ui.initials(name)
Shell slots: #srv-rail #ch-sidebar #chat-main #member-list #stage-layer #mobile-topbar
```

## Task list (tracked in tasks/todo.md)
- Phase 1 Foundation: Wave 0 scaffold + contracts (me)
- Phase 2 Core features: Wave 1 chat (A) ∥ community+dms (B) → checkpoint verify
- Phase 2b: Wave 2 voice (C) ∥ meetings+settings (D) → checkpoint verify
- Phase 3: Wave 3 LAN relay; Wave 4 mobile pass + final sweep

## Verification (per file, every wave)
1. `node --check <file.js>` passes.
2. Headless Edge scan of `http://localhost:8123/unity/index.html` — no Uncaught/TypeError/SyntaxError in stderr log.
3. Manual: send message, switch server/channel, open DM, start Go Live (permission prompt only in interactive use).

## Risks & Mitigations
| Risk | Impact | Mitigation |
|---|---|---|
| Parallel agents drift from contracts | High | Frozen API above; agents may only add files + read store |
| getDisplayMedia blocked headless | Low | Guard behind user gesture; simulated fallback path |
| Same-file edits collide | High | One owner per file; index.html pre-wires all script tags via stubs created in Wave 0 |

## Open Questions
- None blocking. Qwen endpoint unavailable (Colab notebook not running); swarm runs on primary engine.
