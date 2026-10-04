# Status

_Last updated: 2026-10-04_

## Current state

- Combat phases 1–5 are done. The tests pass (43/43, `BattleSystem.Combat.*`). Phase 5 has two parts:
  - part A: AoE circle/cone with telegraphs, effect modifiers, cue tags and the cue table (checked by the user);
  - part B: JSON replays with a checksum verdict, and `Combat.Batch` with statistics and CSV (1000 fights ≈ 3.4 s), checked by the user.
- Deterministic in `Arena-01`, seed 42: `DA_Setup_AoE` `0xBED8CC97`, `DA_Setup_Taunt` `0x7CE33AAB`, `DA_Setup_Mixed` `0x32F7C942`.
- Player commands are done: the command log in the simulation (Move, player abilities without cooldown), scripts, replay v2 with checkpoints, a unit list at the top right with action buttons, arena clicks, a selection ring, and a move disc and line. Checked by the user.
- LevelDesigner is done: levels as JSON in `Levels/` (`Demo`, `TestLevel`), built in the bottom-left menu (edit mode, size, wall/hedge/water/unit, painting, save/load, play), shown in the arena with a fitted camera, and copied into replays. Checked by the user.
- WaveSpawner is done (checked by the user in PIE, test level `WaveTestLevel01`): levels have `waves` (spawns with type, cell and time), a wave pause in Project Settings (`WavePauseSeconds`), a Call wave button / `CallWave` command, and a Spawn tool and wave row in the LevelDesigner.
- In-game control panel: setup, seed, start/stop, pause, speed, debug, taunt range, replay save/play, batch 100/1000 (+CSV). Auto-start is off.
- Repo on GitHub (`main`), with Git LFS for binary assets. `Content/ZZ_FAB/` (Fab packs) is local only.

## Open work

- Balance (from the batch): team 0 wins `DA_Setup_AoE` 86.5% and `DA_Setup_Mixed` 99.7%; the Brutes win `DA_Setup_Test` and `DA_Setup_Taunt`.
- Phase 6 (only if needed): finer navigation grid and clearance for unit sizes.
- Optional: link VFX/sound in `DA_CueTable`.
- List the Fab packs used in `Content/ZZ_FAB/` in `Docs/Licenses/README.md`.
