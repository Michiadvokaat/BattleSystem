# Status

_Last updated: 2026-10-05_

## Current state

- Combat phases 1–5 are done. The tests pass (44/44, `BattleSystem.Combat.*`). Phase 5 has two parts:
  - part A: AoE circle/cone with telegraphs, effect modifiers, cue tags and the cue table (checked by the user);
  - part B: JSON replays with a checksum verdict, and `Combat.Batch` with statistics and CSV (1000 fights ≈ 3.4 s before phase 6, ≈ 22 s with the navigation layer), checked by the user.
- Deterministic in `Arena-01`, seed 42 (since phase 6): `DA_Setup_Test` `0x6682B9DD`, `DA_Setup_AoE` `0xA3C795EA`, `DA_Setup_Taunt` `0x7CE33AAB`, `DA_Setup_Mixed` `0xE1494EB8`; `PiecesDemo` `0xF8FAFC01`. Replays saved before phase 6 no longer reproduce.
- Player commands are done: the command log in the simulation (Move, player abilities without cooldown), scripts, replay v2 with checkpoints, a unit list at the top right with action buttons, arena clicks, a selection ring, and a move disc and line. Checked by the user.
- LevelDesigner is done: levels as JSON in `Levels/` (`Level-01`, `PiecesDemo`, `WallWindows`, `BuildTest01`), built in the bottom-left menu (edit mode, size, save/load, play), shown in the arena with a fitted camera, and copied into replays. Checked by the user.
- LevelDesigner modes (checked by the user in PIE; merged to `main` from `leveldesigner-modes`, tag `before-leveldesigner-modes`): the cell kinds wall/hedge/water are gone (level format 4 without rows; `Demo` and `Klaslokaal01` deleted); Build Mode / Unit Mode / Spawn Mode each show only their own controls; the catalog is `Building`/`Furniture`/`Props` with subcategories (two button rows, pieces small to large with a size label). 60/60 tests.
- WaveSpawner is done (checked by the user in PIE): levels have `waves` (spawns with type, cell and time), a wave pause in Project Settings (`WavePauseSeconds`), a Call wave button / `CallWave` command, and a Spawn tool and wave row in the LevelDesigner, plus a spawn list at the bottom right (start enemies and every wave's spawns, editable) and Rename/Delete of level files; all checked by the user.
- Character looks (checked by the user in PIE): `UCombatAppearance` Data Assets with modular skeletal mesh slots (random options per seed, merged into one mesh and shared per combination), swappable slots (Leader Pose, `SetSlotMesh`, `Combat.SetSlot`), tag overrides, props on sockets and uniform/width/height scale. `UCombatUnitDefinition::Appearance` links a look. Looks `DA_Look_Melee/Ranger/Tank/Child_Male_Random` in `/Game/Characters/Looks` (local, from `Scripts/CreateCharacterAppearances.py`). 45/45 tests.
- Character animation (checked by the user in PIE): `UCombatAnimSet` per skeleton (AnimBP `ABP_Combat`, locomotion blend space on its Speed axis via `LocomotionX/Y`, tag → montage actions, idle breaks, turn rate, corpse time); attack montages timed to the hit via an `Impact` notify, `Anim.Hit`/`Anim.Death`, `AnimationTag` on attacks; the animation rate follows pause and speed. `DA_AnimSet_Child` has Mixamo throw/push/punch/hit/death (retargeted) and the pack's locomotion and idle breaks. 47/47 tests.
- Free camera (checked by the user in PIE): editor-like controls in `ACombatPlayerController` (RMB look + WASD/QE, RMB click still cancels, MMB pan, wheel zoom to cursor, Alt+LMB orbit, Alt+RMB dolly, F / Reset camera = overview), clamped around the grid. LMB+RMB pans sideways / world up-down (checked in PIE); tuning under Project Settings > Combat > Camera. The camera only returns to the overview when the level/setup kind or the grid size changes. 46/46 tests.
- Visual level building with catalog pieces (merged to `main` from the experiment branch `leveldesigner-pieces`): Step A done and committed: edge walls (walls on cell borders) in the grid and simulation; fights without edge walls unchanged. Step B done and checked in PIE: `UCombatPieceCatalog` (`/Game/Environment/DA_PieceCatalog`, 40 pieces from `Catalogus` + 3 scaled wall variants; `bScaleToFit`), level format 3 with `pieces`, pieces shown in the arena and adding their blocking to the grid, demo level `PiecesDemo`. 53/53 tests. Step C done and checked in PIE: Piece tool with a category/palette (thumbnails in the editor), live preview (mesh + green/red/orange footprint), R / Shift+R and Rotate buttons, place/erase per layer. Step D done and checked in PIE: detail objects (`Details*` categories, 21 props) on a 3x3 grid per cell, 45 degree turns, standing on the furniture under them (`SurfaceHeight`), optionally blocking their cell. 55/55 tests. Polish before merging to main (the user's list): undo/redo done (checked in PIE); Ctrl+click moves a piece and a right click deselects / puts back / erases (checked in PIE); walls Up/Cutaway/Down (V) done (checked in PIE); wall clearance in the simulation done; the door jam the user found is fixed (two-step move resolution and a clearance-wide steering line); checked in PIE. 58/58 tests; `PiecesDemo` 100 fights: no stuck units, 16-22 s (team 1 wins 99%: a balance question of the demo).
- In-game control panel: setup, seed, start/stop, pause, speed, debug, taunt range, replay save/play, batch 100/1000 (+CSV). Auto-start is off.
- Repo on GitHub (`main`), with Git LFS for binary assets. `Content/ZZ_FAB/` (Fab packs) and the other local content folders (`Characters`, `Environment`, `Meshes`, ...) are local only.

- Unit ghost and rotation in the LevelDesigner (checked by the user in PIE; merged to `main` from `unit-ghost`, tag `before-unit-ghost`): a see-through ghost of the unit under the cursor in Unit and Spawn Mode, and a start rotation in 45 degree steps (R / Shift+R) stored per unit and spawn (level format 5, presentation only).

- Solid floors (checked by the user in PIE; merged to `main` from `solid-floors`, tag `before-solid-floors`): `Building/Floors/SolidFloor_1x1`..`_4x4` with a color per placed piece (swatches, color picker, eyedropper I), level format 6.

- Wall items (checked by the user in PIE; merged to `main` from `wall-items`, tag `before-wall-items`): catalog group `WallProps` (166 meshes from the user) hung on walls, on positions along them, with a height and a tilt; level format 7.

- Phase 6, navigation layer (checked by the user in PIE; merged to `main` from `nav-subgrid`, tag `before-nav-subgrid`): routes on 3x3 sub-cells per clearance class (from the unit radius, capped so every unit fits through a one-cell door); movement unchanged; about 8x per tick. 62/62 tests.

- Unit positions (checked by the user in PIE; on branch `unit-positions`, tag `before-unit-positions`): units and spawns on one of 9 positions per cell, rotation per 11.25 degrees, Pos/Rot in the spawn list; level format 8.

## Open work

- Merge `unit-positions` to `main`.

- Balance (from the batch): team 0 wins `DA_Setup_AoE` 86.5% and `DA_Setup_Mixed` 99.7%; the Brutes win `DA_Setup_Test` and `DA_Setup_Taunt`.
- Character looks, later: per-bone scaling (own AnimGraph node + editor module), parts from other skeletons (retarget first), and "Allow CPU Access" on the source meshes for cooked builds. Mutable remains the alternative if this falls short.
- Optional: link VFX/sound in `DA_CueTable`.
- List the Fab packs used in `Content/ZZ_FAB/` in `Docs/Licenses/README.md`.
