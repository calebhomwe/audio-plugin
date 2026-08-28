## Phase 1: Foundation
- [x] Task 1: Plan + contracts written
- [x] Task 2: unity/ scaffold (all files built + verified)
## Checkpoint: Foundation
- [x] node --check all JS (ALL SYNTAX OK)
- [x] Headless Edge loads shell with zero runtime errors (desktop + phone)
## Phase 2: Core features
- [x] Task 3 (A): features/chat.js — messages/composer/reactions/replies/threads/pins/search/slash/bot
- [x] Task 4 (B): features/community.js + features/dms.js — members/roles/mod/bots/DMs/friends
## Checkpoint: Core
- [x] node --check + Edge scan clean; SMOKE 7/7: send|dm|meetings|calendar|settings|voice|members
## Phase 2b
- [x] Task 5 (C): features/voice.js — voice channels + REAL Go Live + simulated streams + remote overlay
- [x] Task 6 (D): features/meetings.js + settings.js — meetings/calendar/stage + themes/perks
## Phase 3
- [x] Task 7: relay/unity-relay.js + relay/host.html + relay/viewer.html (LAN real screen share)
- [x] Task 8: css/mobile.css full phone layout + final verification sweep
## Checkpoint: Complete
- [x] All files node --check pass; Edge scan clean desktop+phone; smoke 7/7
## Polish round 2 (self-review fixes)
- [x] chat.js: thread-delete mis-splice fixed (guarded findIndex; root thread removed too)
- [x] app.js: unread badge clears on channel open (navChannel)
- [x] chat.js: search result cap effective (<=40)
- [x] Empty-channel / new-conversation welcome state
- [x] relay e2e suite 10/10 (join/mime/binary100KB/control/consent/isolation/leave) + FIN cleanup + ping reaper
Verified: node --check all OK; Edge scan desktop+mobile clean; SMOKE 7/7
