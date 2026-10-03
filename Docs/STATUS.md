# Status

_Last updated: 2026-10-03_

## Current state

- Combat phases 1–5 are done. The tests pass (29/29, `BattleSystem.Combat.*`). Phase 5 has two parts:
  - part A: AoE circle/cone with telegraphs, effect modifiers, cue tags and the cue table (checked by the user);
  - part B: JSON replays with a checksum verdict, and `Combat.Batch` with statistics and CSV (1000 fights ≈ 3.4 s), checked by the user.
- Deterministic in `Arena-01`, seed 42: `DA_Setup_AoE` `0xBED8CC97`, `DA_Setup_Taunt` `0x7CE33AAB`, `DA_Setup_Mixed` `0x32F7C942`.
- In-game control panel: setup, seed, start/stop, pause, speed, debug, taunt range, replay save/play, batch 100/1000 (+CSV). Auto-start is off.
- Repo on GitHub (`main`), with Git LFS for binary assets. `Content/ZZ_FAB/` (Fab packs) is local only.

## Open work

- Player commands part A is done (simulation, scripts, replay v2; 34/34 tests). Part B: a unit list on the right of the HUD (own team; click a unit to see its action buttons), Move via a click in the arena, and a selection ring.
- Balance (from the batch): team 0 wins `DA_Setup_AoE` 86.5% and `DA_Setup_Mixed` 99.7%; the Brutes win `DA_Setup_Test` and `DA_Setup_Taunt`.
- Phase 6 (only if needed): finer navigation grid and clearance for unit sizes.
- Optional: link VFX/sound in `DA_CueTable`.
- List the Fab packs used in `Content/ZZ_FAB/` in `Docs/Licenses/README.md`.
