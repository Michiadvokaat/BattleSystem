# Status

_Last updated: 2026-10-05_

## Current state

- Combat phases 1–5 are done. The tests pass (44/44, `BattleSystem.Combat.*`). Phase 5 has two parts:
  - part A: AoE circle/cone with telegraphs, effect modifiers, cue tags and the cue table (checked by the user);
  - part B: JSON replays with a checksum verdict, and `Combat.Batch` with statistics and CSV (1000 fights ≈ 3.4 s), checked by the user.
- Deterministic in `Arena-01`, seed 42: `DA_Setup_AoE` `0xBED8CC97`, `DA_Setup_Taunt` `0x7CE33AAB`, `DA_Setup_Mixed` `0x32F7C942`.
- Player commands are done: the command log in the simulation (Move, player abilities without cooldown), scripts, replay v2 with checkpoints, a unit list at the top right with action buttons, arena clicks, a selection ring, and a move disc and line. Checked by the user.
- LevelDesigner is done: levels as JSON in `Levels/` (`Demo`, `Level-01`), built in the bottom-left menu (edit mode, size, wall/hedge/water/unit, painting, save/load, play), shown in the arena with a fitted camera, and copied into replays. Checked by the user.
- WaveSpawner is done (checked by the user in PIE): levels have `waves` (spawns with type, cell and time), a wave pause in Project Settings (`WavePauseSeconds`), a Call wave button / `CallWave` command, and a Spawn tool and wave row in the LevelDesigner, plus a spawn list at the bottom right (start enemies and every wave's spawns, editable) and Rename/Delete of level files; all checked by the user.
- Character looks (checked by the user in PIE): `UCombatAppearance` Data Assets with modular skeletal mesh slots (random options per seed, merged into one mesh and shared per combination), swappable slots (Leader Pose, `SetSlotMesh`, `Combat.SetSlot`), tag overrides, props on sockets and uniform/width/height scale. `UCombatUnitDefinition::Appearance` links a look. Looks `DA_Look_Melee/Ranger/Tank/Child_Male_Random` in `/Game/Characters/Looks` (local, from `Scripts/CreateCharacterAppearances.py`). 45/45 tests.
- Character animation (checked by the user in PIE): `UCombatAnimSet` per skeleton (AnimBP `ABP_Combat`, locomotion blend space on its Speed axis via `LocomotionX/Y`, tag → montage actions, idle breaks, turn rate, corpse time); attack montages timed to the hit via an `Impact` notify, `Anim.Hit`/`Anim.Death`, `AnimationTag` on attacks; the animation rate follows pause and speed. `DA_AnimSet_Child` has Mixamo throw/push/punch/hit/death (retargeted) and the pack's locomotion and idle breaks. 47/47 tests.
- Free camera (checked by the user in PIE): editor-like controls in `ACombatPlayerController` (RMB look + WASD/QE, RMB click still cancels, MMB pan, wheel zoom to cursor, Alt+LMB orbit, Alt+RMB dolly, F / Reset camera = overview), clamped around the grid. LMB+RMB pans sideways / world up-down (checked in PIE); tuning under Project Settings > Combat > Camera. The camera only returns to the overview when the level/setup kind or the grid size changes. 46/46 tests.
- In-game control panel: setup, seed, start/stop, pause, speed, debug, taunt range, replay save/play, batch 100/1000 (+CSV). Auto-start is off.
- Repo on GitHub (`main`), with Git LFS for binary assets. `Content/ZZ_FAB/` (Fab packs) is local only.

## Open work

- Balance (from the batch): team 0 wins `DA_Setup_AoE` 86.5% and `DA_Setup_Mixed` 99.7%; the Brutes win `DA_Setup_Test` and `DA_Setup_Taunt`.
- Character animation: LevelDesigner previews do not show looks yet; list Mixamo in `Docs/Licenses/README.md`.
- Character looks, later: per-bone scaling (own AnimGraph node + editor module), parts from other skeletons (retarget first), and "Allow CPU Access" on the source meshes for cooked builds. Mutable remains the alternative if this falls short.
- Phase 6 (only if needed): finer navigation grid and clearance for unit sizes.
- Optional: link VFX/sound in `DA_CueTable`.
- List the Fab packs used in `Content/ZZ_FAB/` in `Docs/Licenses/README.md`.
