# Devlog

Dated history: decisions, bugs, rejected approaches. Newest entries at the bottom.

## 2026-10-03

- Created the UE 5.8 C++ project `BattleSystem` and the combat system design (`Docs/Ontwerp-Gevecht.md`): a deterministic, headless-capable fixed-step simulation on a custom grid. GAS, NavMesh and physics are rejected for combat logic (see the design's "Besluiten" table).
- Set up the git repo with Git LFS for binary assets. Third-party Fab packs live in `Content/ZZ_FAB/` and are gitignored to keep the repo small.
- Adopted the working agreements (`Docs/Werkafspraken.md`), and added `STATUS.md`, `Architecture.md`, `DEVLOG.md` and `Licenses/README.md`.
- Combat phase 1 code (see `Docs/Architecture.md`). Decisions:
  - Melee attacks have a windup in ticks; a hit is dropped if the target died during the windup.
  - Fights auto-start through `ACombatGameMode` and can also be started from the console.
  - Placeholder units are a C++ cylinder actor.
  - Test assets are created with editor Python (`Scripts/CreateCombatTestAssets.py`).
  - The grid collects obstacles itself instead of obstacles registering, to be independent of BeginPlay order.
- Bug caught by `SeedChangesFight`: the seeded random delay started as an initial attack cooldown. It always ran out while units were still walking, so the seed had no effect. It is now a first-attack delay that only counts down once the unit is in range (`MaxFirstAttackDelay`).
- `ReceiveHit` clashes with `AActor::ReceiveHit`; the Blueprint events are `ReceiveUnitAttack/Hit/Death`.
- Headless editor runs: ExecCmds are comma-separated, and `Quit` does not exit editor mode; use `QUIT_EDITOR`.
- Units stayed grey and did not flash: the engine cylinder uses `DefaultMaterial`, which has no `Color` parameter, so `SetVectorParameterValue` did nothing. The body MID is now made from `BasicShapeMaterial` (`BodyMaterialBase`, overridable per Blueprint).
- Phase 1 done: the arena `Arena-01` is built in the editor (grid, 3 obstacles, top-down camera, `CombatGameMode` override) and the fight was checked visually.
- Phase 2 code: per-team distance maps (integer costs 10/14, ties → lowest ID), targeting by walking distance, route following with path smoothing, separation between all units, and slide/stop so units never end in a blocked cell. Melee now also needs a clear line. Debug drawing via `Combat.Debug`. Choices: all units separate (allies and enemies); the wall scenario is tested in `Arena-01` (`DA_Setup_Wall`); the distance maps are also rebuilt after every death so targets are never stale.
- `CreateCombatTestAssets.py` no longer overwrites existing assets (it used to reset values tuned in the editor); `FORCE_UPDATE` restores the old behavior.
- Cost: a 5-unit fight went from 0.07 ms to ~2.4 ms headless. Keep an eye on this for `Combat.Batch` (phase 5).
- Phase 2 done: the wall is in `Arena-01` (one obstacle at x = 6, y = 0..8; the old obstacles were removed), and the user checked visually that the Krijger in `DA_Setup_Wall` goes for the reachable Brute.
- Phase 3 code:
  - Line of sight (`HasLineOfSight`, the shared `IsLineClear` traversal).
  - A list of attacks per unit, each with its own cooldown; the shortest-range attack that can reach wins.
  - Ranged units take a visible enemy in range first.
  - Homing projectiles as simulation objects (`ProjectileSpawned`/`ProjectileEnded`); `ACombatProjectileActor` shows them.
  - Test assets `DA_Boogschutter`, `DA_Doelpop`, `DA_Setup_Archer` and `DA_Setup_Mixed`.
  - Choices: no kiting; mixed units use melee up close.
- Units without a usable attack now stop at the stop distance instead of creeping 1 cm per tick (only affected units without attacks).
- `DA_Setup_Test` takes 331 ticks with the wall in Arena-01 (243 with the old obstacles). Fights take ~3.5 ms headless.
- In-game control panel, built fully in C++ (Slate, at the user's request rather than a Widget Blueprint). `ACombatHUD` shows `SCombatControlPanel` with setup, seed, start/stop, pause, speed and debug-level controls, plus a status line. Pause/speed only scale the presentation accumulator. `bAutoStartFight` is now off, so fights start from the panel. There are no automated UI tests; the user checks it in PIE.
- Ranged units (any ranged attack) show as a cube, melee units and units without attacks as a cylinder (`MeleeBodyMesh`/`RangedBodyMesh` on `ACombatUnitActor`, overridable per Blueprint).
- Phase 3 done: the user checked the fights and the control panel in PIE.
- Phase 4 code:
  - Effect model (`FCombatEffectList`: tags, duration in ticks, Refresh/Stack/Ignore, BlockedByTags).
  - Threat list of 8 per unit (damage × `ThreatMultiplier`). Decay is a half-life or linear (`ThreatDecayMode`, at the user's request), expressed as factor and amount per tick.
  - Target choice every retarget interval in the order taunt > threat > visible > nearest, with hysteresis.
  - Own A* route (`CombatPathfinding::FindPath`) when the target is not the team map's nearest.
  - `Attack.Taunt` as an area attack around the unit.
  - Debug colors per target reason. Test assets `DA_Tank` and `DA_Setup_Taunt`.
- Effect checksums use indices, not tag names: FName indices are not stable between processes.
- Behavior change: `DA_Setup_Test` now ends after 233 ticks instead of 331 (units turn to whoever hits them and switch targets less). In `DA_Setup_Taunt`, team 1 (3 Brutes) wins with seeds 42 and 7.
- Taunt visibility: a short magenta circle when an area attack (taunt) goes off (`AreaAttackFired` event → `ACombatUnitActor::OnAreaAttack`), and a permanent range circle via `Combat.ShowRanges` / the panel button "Taunt range".
- Taunt range slider in the panel (an override applied at the next Start; "Asset" uses the Data Asset), and a magenta "T" above every unit with `Status.Taunted`.
- Phase 4 done: the user checked taunt, the range circle, the slider and the T marker in PIE.
- Phase 5 part A:
  - `Attack.AoE` with `CircleAtTarget`, `CircleAroundSelf` and `Cone`, and telegraphs (fixed place, no dodging). Friendly fire is set per attack (`bAffectsEnemies` / `bAffectsAllies`).
  - Effect modifiers (move speed, damage dealt, damage taken).
  - Cue tags on events, and `UCombatCueTable` (Niagara, sound, debug color).
  - Taunt now uses the same area mechanism. The checksums of `DA_Setup_Taunt` (`0x7CE33AAB`) and `DA_Setup_Mixed` (`0x32F7C942`) are unchanged, so behavior is identical.
  - The area flash moved from the unit actor to the subsystem (its color comes from the cue table).
  - New assets: `DA_Magier`, `DA_Bijlman`, `DA_Vaandeldrager`, `DA_Setup_AoE`, `DA_CueTable`.
  - Fights take ~3.5–4.5 ms headless.
- Phase 5 part B:
  - `FCombatSimSettings` is the one place for fight-relevant settings. `BuildSimConfig` and `ApplyTo` replace the old inline conversion; the checksums are unchanged.
  - JSON replays with build, map and grid checks and a checksum verdict.
  - `Combat.Batch` and the panel's Batch 100/1000 report win rates, durations and per-type damage and survival, with optional CSV.
  - 1000 fights of `DA_Setup_AoE` take 3.4 s. Team 0 wins 86.5% in `DA_Setup_AoE` and 99.7% in `DA_Setup_Mixed`; the Magier and Bijlman almost never survive.
  - Replays reference the setup asset rather than a snapshot of the unit stats: changing a Data Asset makes an old replay DIFFERENT, which the verdict shows.
- Phase 5 done: the user checked AoE, replay save/play (identical) and Batch 1000 in PIE.
- Health bar above each unit and status labels for active effects on its center (screen-space widgets, Slate in code; `StatusIcons` setting). They replace the debug "T". Presentation only: checksums unchanged.
- Health bar stayed green: the default `SProgressBar` style tints its striped fill texture (`PrimaryHover`), which multiplied with our color. The bar now uses its own style: a plain white fill on a dark background, without the fill animation.
- Player commands part A (simulation): `FCombatCommand` (Move/Ability) with a tick, a command log in the simulation, and `QueueCommand`.
  - Move orders override the AI and taunts. Player abilities (`PlayerAbilities`) have no cooldown.
  - `UCombatCommandScript` for Simulate and Batch, and replay format 2 with commands and checkpoints every 20 ticks.
  - Checksums without commands are unchanged (`0xBED8CC97`, `0x7CE33AAB`, `0x32F7C942`).
  - `DA_Setup_Taunt` with `DA_Script_TauntDemo` gives `0xB029A714`. Over 1000 fights the archers deal 155 damage instead of 58, but team 0 still always loses.
  - Console commands `Combat.Move` and `Combat.Ability` for testing until the unit list exists.
- Player commands part B (UI): a unit list at the top right (own team, with health, labels and the order text; click a row for its Move and ability buttons), `ACombatPlayerController` (left click selects or sets the Move target on the grid plane, right click cancels), a selection ring, and a move disc and line in the arena. Attacks get an optional `DisplayName`. UI only: no simulation changes.
- Player commands part B done: the user checked the unit list, Move/Taunt, arena selection and replay in PIE.
- LevelDesigner part A:
  - `FCombatLevel` (JSON in `Levels/`, rows of `.#h~` and units) and `CombatLevels::BuildConfig`.
  - `FCombatFightSource` (setup or level) throughout the subsystem.
  - The arena shows a level (resized floor, instanced blocks, obstacles hidden), and the camera fits the grid.
  - Replay format 3 copies the level. Start, Simulate and Batch accept `level=`, and levels appear in the panel's dropdown.
  - `Levels/Demo.json` reproduces `DA_Setup_Taunt` in Arena-01 exactly (`0x7CE33AAB`). 37/37 tests.
- LevelDesigner part B: a bottom-left menu with edit mode, name/save, load/new, size, wall/hedge/water/unit tools with type and team, and Play. Mouse painting with drag, erasing with the right button, and preview units. Start in the control panel plays the edited level while editing.
- LevelDesigner part B done: the user built and played `Levels/TestLevel.json` in PIE.
- Camera orientation for levels: Arena-01's camera has yaw 0, so grid X (width) ran upwards on screen. When fitting a level the camera is now set to (pitch -90, yaw -90): width to the right, height downwards. Setups get the original camera back.
- Units are flat board pieces: `BodyHeight` is 10 cm (was 180), the nose scales with the body (at most 25 cm), and projectiles fly at 15 cm (`FlightHeight`, was 110).
- Level blocks are low like the pieces: walls 30 cm (was 200), hedges 20 cm (was 120), water 6 cm, as properties on `ACombatGrid`. The obstacles placed in Arena-01 keep their own size.

## 2026-10-04

- WaveSpawner:
  - Level format 2: `waves` with `spawns` (`type`, `cell` {x, y}, `time` in seconds after the wave start). Version 1 files load without waves. Spawns are always team 1; `units` keeps both teams.
  - Decisions (with the user): the next wave starts `WavePauseSeconds` (Project Settings, default 5 s) after the previous is clear (all spawned, no team 1 alive); the first one that long after the fight starts. A `CallWave` command (control panel button, `Combat.CallWave`) starts the next wave right away, also during a running wave (overlap). A spawn on an occupied cell still happens (separation). The heroes win when all waves are beaten, the enemies when the heroes die (also with waves left).
  - Wave units get their unit ID when they appear, so the definition is found through `FCombatUnit::SourceIndex` (config units, then all wave spawns) in the subsystem and the batch.
  - Fights without waves keep their checksums (`0xBED8CC97`, `0x7CE33AAB`, `0x32F7C942`, Demo `0x7CE33AAB`). A temporary level with two waves of Brutes against Demo's heroes: `0xB59CC52C` twice, the Brutes win.
  - LevelDesigner: Spawn tool (type + time), wave row (`<` `>` `+` `-`), previews of the selected wave with their time.
  - 43/43 tests (6 new wave tests).
- WaveSpawner done: the user checked it in PIE (`Levels/WaveTestLevel01.json`).
- Spawn list for the LevelDesigner: bottom right in edit mode, every wave with its spawns sorted by time; per row type, wave, X, Y and time to edit, Move (then a click in the arena) and remove. Invalid cells are refused. Presentation only: no simulation changes, 43/43 tests.
- Spawn list "empty" bug report: `WaveTest2` had three empty waves; its enemies were placed with the Unit tool on team 1 (start units), not as spawns. The list now also shows "Start enemies" (editable, movable, removable) and "(empty: use the Spawn tool)" under empty waves.
- Spawn list done: the user checked it in PIE (`WaveTestLevel01`, `WaveTest2`).
- LevelDesigner Rename and Delete: on the level selected in the dropdown; rename takes the Name field and refuses an existing name; delete needs a second click within 3 s; the open level stays as unsaved. 44/44 tests (new: LevelFileRenameDelete).
- Rename/Delete done: the user checked it in PIE. While testing, the test levels `TestLevel`, `WaveTest2` and `WaveTestLevel01` were deleted (still in git history) and `Levels/Level-01.json` was made.
- Editor GPU crash (`DXGI_ERROR_DEVICE_REMOVED` / `DRIVER_INTERNAL_ERROR`) when copying ~80 skeletal meshes in the Content Browser. Cause: `USkeletalMeshThumbnailRenderer` caches one preview scene per asset path (up to 128), and with reserved GPUScene buffers every scene reserves several 2 GB virtual ranges. Past ~256 GB the driver fails a 2 GB `CreateReservedResource`. Measured with `rhi.DumpResourceMemory all Name=GPUScene.InstanceSceneData`: one extra scene per copy, not freed by `obj gc`. Fix in `DefaultEngine.ini` `[SystemSettings]`: `r.GPUScene.UseReservedResources=0` and `r.GPUScene.InstanceDataTileSizeLog2=-1` (normal growable buffers, 0.06 MB per scene). `r.Nanite.Streaming.ReservedResources` was not the cause. The user confirmed: 80 copies, no crash.

## 2026-10-05

- Character looks, route 1 of two investigated (Data Asset + own build vs the Mutable plugin; Mutable stays the fallback). Decisions with the user:
  - A look is a `UCombatAppearance` Data Asset (diffable, filled by Python) instead of a Blueprint per figure.
  - Variation: fixed parts plus optional random options per slot, picked with their own stream seeded by `HashCombine(fight seed, unit ID)`, so replays look the same and the simulation checksum is untouched.
  - 30–100 figures on screen and parts that must change during play: fixed parts are merged into one mesh (`SkeletalMerging` plugin) and shared per combination; hats, glasses and the like are swappable Leader Pose components. Gameplay changes go through tag overrides (effect and granted tags), never through the simulation.
  - Shape: uniform, width and height scale now; per-bone scaling later (needs an AnimGraph node and an editor module). No animations yet.
- All 161 child/hero meshes use `/Game/ZZ_FAB/City_Characters/Skeletons/SKEL_Child_Skeleton`; a headless check merged the fixed parts of all four looks.
- `ACombatUnitActor::SetActiveTags(Tags)` clashed with `AActor::Tags` (C4458, warnings are errors); the parameter is `InTags`.
- Character looks checked by the user in PIE. `DA_Krijger` → `DA_Look_Melee`, `DA_Boogschutter` → `DA_Look_Ranger`, `DA_Tank` → `DA_Look_Tank`, `DA_Brute` → `DA_Look_Child_Male_Random` (set in the editor; the looks are local content, so without `/Game/Characters` these references are empty and the units show the placeholder).
- Free camera, editor-style (the user's choices): RMB drag looks and RMB click still cancels (drag threshold); in LevelDesigner edit mode RMB keeps erasing, so the camera uses MMB, the wheel and Alt there; bounded around the shown grid; the camera stays between fights and only returns to the overview when the kind of overview (level vs setup) or the grid size changes, or on F. Middle drag "grabs" the ground (the point under the cursor follows it) instead of the editor's screen-plane pan, because that matches a top-down arena. The placed camera transform is now remembered at the first camera use, not only at the first level fit, so a setup overview restores the map's camera even after the player moved it. `CameraMinPitch` defaults to -90 so the fitted straight-down view is not tilted by the first zoom.
- Free camera checked by the user in PIE.
- LMB+RMB drag pans like the editor: mouse X along the camera's right, mouse Y along world Z. To allow the chord, a left click outside edit mode now acts on release instead of on press (otherwise pressing LMB first would already select or issue a Move). In edit mode the first button has already painted or erased one cell before the chord starts.
- LMB+RMB pan checked by the user in PIE.
- Character animation, decisions with the user: animations per skeleton in a `UCombatAnimSet` (referenced by the look); throw/push/punch/hit/death from Mixamo, retargeted to `SKEL_Child_Skeleton` in the editor (the City_Characters pack only has locomotion and idle/social animations); attack montages are scaled so their `Impact` notify lands on the simulation's hit; death plays a montage, holds the last pose and hides after `CorpseDuration`.
- `UCombatAppearance::AnimClass` was replaced by `AnimSet` (its `AnimClass` is the AnimBP); no look had one set.
- Death pose: rather than relying on each montage's auto blend-out setting, `UCombatAnimInstance` pauses the death montage just before its blend-out would start.
- Attack montages also follow the time scale: the play rate lines Impact up in simulation seconds, and `GlobalAnimRateScale` applies the speed buttons and pause on top.
- Locomotion stayed idle: `DA_AnimSet_Child` first had no AnimClass (the AnimBP was made after the script ran; rerunning it filled it in), and then `BS_Child_Idle_Run` turned out to have its Speed axis on Y (X is an unused 0..100 axis) while the AnimBP fed Speed to X. Found with temporary `[AnimDebug]` logs (Speed reached the instance) and the user's AnimGraph screenshot. Fix: `UCombatAnimInstance::LocomotionX/Y`, filled on the axis named "Speed", so the AnimBP no longer has to know the blend space's layout.
- Character animation checked by the user in PIE; `[AnimDebug]` logs removed. Attack `AnimationTag`s set in the editor: Krijger Punch, Brute and Tank Push, Boogschutter, Magier, Bijlman and Vaandeldrager Throw.
- LevelDesigner previews show the looks (same look seed formula as fights, `GetLookSeed`). An animated figure's first facing is now set at once; previews get only one update, so with `TurnRate` they faced the wrong way. `CombatCamera::RayToPlane` divides by a clamped Z: after the unity files shifted, MSVC reported C4723 (divide by 0) for the parallel ray in the CameraMath test.
- LevelDesigner preview looks checked by the user in PIE.
- LevelDesigner edit mode gets the full camera (the user's choice): right drag looks and flies as in the game, a right click erases one cell on release, and erase strokes moved to Shift + left drag. The hint line in the LevelDesigner says so.

## Branch `leveldesigner-pieces` (tag `before-pieces`)

- Visual level building, decisions with the user: a catalog of pieces (`UCombatPieceCatalog`) filled from `/Game/Environment/Catalog/<Category>/` (subfolder = category, copies of the chosen pack meshes); a piece sets the gameplay of its cells; walls stand on the **borders** between cells and every border piece blocks, except door frames (category Doors, visual only); fixed layers per cell (floor, cell, border, later detail); footprints of several cells that rotate in 90° steps; the detail grid (N×N per cell) goes into the level format now and is built later. Plan: A edge walls in the simulation, B catalog and level format, C LevelDesigner palette, preview and rotation, D detail objects.
- Step A: edge walls in `FCombatGridData` (`CanStep`, `IsLineClear`, `CrossesNoEdgeWall`, checksum) and `ResolveMove`. Distance maps and A* follow through `CanStep`. Tests EdgeWallGrid, EdgeWallPath, EdgeWallFight (50/50). Fights without edge walls are unchanged: `Combat.Simulate 42` in Arena-01 still gives `0xBED8CC97` (AoE), `0x7CE33AAB` (Taunt) and `0x32F7C942` (Mixed). Units are still points: a body can reach halfway through a thin wall until the clearance work of phase 6.
- Step B: pieces in the level format (version 3) carry their own footprint and blocking (copied from the catalog at placement), so fights, batches and replays never depend on the catalog; the catalog only supplies the mesh. Mesh placement is automatic from the mesh bounds (centered on the footprint or border line, bottom on the floor); the pack's pivots differ (doors at one end, windows and pillars centered on their height), which the bounds handle; `Offset` remains for a window's sill height. The catalog folder is `/Game/Environment/Catalogus` (the user's name). `CreatePieceCatalog.py` made 40 pieces: 6 door frames (Edge, open), 4 floors (4x4), 7 furniture (1x1, block walking), 7 walls (Edge, 4 long) + 4 blocks/pillars (Cell, block both), 12 windows (Edge, 2-4 long). `PiecesDemo`: `Combat.Simulate 42` gives `0x330782FF` twice; without its pieces `0xA86DB452`, so the pieces take part in the fight. 53/53 tests.
- Step B checked in PIE by the user: borders, windows, door frame and furniture stand right. Two fixes: (1) the pieces rendered with the default material, because the pack's `M_Material` lacks the instanced-static-mesh usage flag ("missing usage flag InstancedStaticMeshes! Default Material will be used in game"); pieces are now plain static mesh components instead of instancing (the user's choice, works for any pack). (2) The demo's door opening was 4 borders wide with a 1-border frame, because all of the pack's walls are 4 m (shorter thin pieces are only low 2 m partitions); catalog entries got `bScaleToFit`, the script adds `SM_Walls_008_1m/_2m/_3m`, and the demo's right wall is now 4 m + 1 m + door + 2 m.
- Z-fighting between the grid's floor plane and floor pieces (both at Z = 0): the plane now sinks `PieceFloorDrop` (2 cm) while the level has floor pieces (the user's choice over hiding the plane or lifting the pieces).
- Step B checked by the user in PIE (materials, the closed right wall, no z-fighting).
- Step C: Piece tool in the LevelDesigner. Thumbnails use `FAssetThumbnail` from `UnrealEd`, linked only in editor builds (packaged builds show names). The preview is the real mesh plus footprint marks in the engine shape material (no translucent material needed). Pieces are placed on a press only (strokes would shift long walls along the drag); erasing removes the pieces of the selected piece's layer that the preview covers. Placement centers the piece on the cursor (cells) or on the nearest border of its direction (edges). The user asked for a Rotate button next to R / Shift+R.
- Step C checked by the user in PIE.
- Step D, decisions with the user: detail objects from catalog folders starting with `Details` (the user made `Details_Food`, `Details_OfficeSupplies`, `Details_Toys`, 21 props); one detail per position of a 3x3 grid per cell, turned in 45 degree steps; optionally blocking the whole cell; standing automatically on what is under them. Exceptions such as a book on a table or a toy on a closet follow from the layers: a detail is on another layer than the furniture, and stands on the furniture's `SurfaceHeight` (default the top of its mesh bounds) when its position is above that furniture's bounds. Details on details are not supported yet. Each detail stores its own `DetailGrid` so later catalog changes do not move placed details.
- Step D checked by the user in PIE.
- Polish, decisions with the user: undo/redo, an eyedropper on Ctrl+click, walls Up/Cutaway/Down (V key and a control panel button; lowered = scaled to `LowWallHeight`), and wall clearance capped by `WallClearance` (45 cm) in `FCombatSimSettings` so old replays (0) still reproduce. Undo/redo records changes centrally in `RefreshDesignView` (every level change goes through it) by comparing JSON, so new kinds of edits are covered automatically.
- Undo/redo checked by the user in PIE. Eyedropper built: Ctrl + left click, topmost piece first (detail, cell piece, nearest border within a quarter cell, floor).
- Bug (user report): after the eyedropper change the palette stuck on one category, because its Tick followed the selected piece's category every frame and so undid every category click. It now follows only when the selected piece changes (`FollowedPiece`).
- User feedback on the eyedropper: it looked like dragging while the original stayed. By the user's choice Ctrl+click now moves the piece (pick-up and put-down are one undo step; a right click puts it back), and a right click deselects the selected piece; with nothing selected, a right click or Shift+click erases the topmost piece under the cursor.
- Move and right click checked by the user in PIE. Walls Up/Cutaway/Down built: lowering squeezes a border piece in Z to `LowWallHeight` (the texture is squeezed too, acceptable for a game view); Cutaway tests the line camera -> unit against each wall's full-height bounds every frame.
- Walls Up/Cutaway/Down checked by the user in PIE. Wall clearance: `FCombatGridData::PushClear` after the cell part of `ResolveMove`, with `min(radius, WallClearance)`; `WallClearance` (45 cm) is a fight setting, so old replays (field missing = 0) take the old code path unchanged. Checksums in Arena-01, seed 42: AoE `0xBED8CC97` and Taunt `0x7CE33AAB` unchanged (no unit gets within 45 cm of a wall there), Mixed `0x32F7C942` -> `0x65DD729E`. `PiecesDemo` went from 14.4 s to 32.4 s for seed 42: its door is one cell wide, so it is now a real bottleneck; 100 fights had no draws or time limits.
- Bug (user report): with wall clearance two units jammed at the one-cell door of `PiecesDemo`. Temporary `[DoorDebug]` logs (headless batch) showed two Brutes beside the door, one above and one below it, pressed against the wall: their steering aimed through the doorway along a thin line they could not follow with 45 cm clearance, so their move went mostly into the wall, and the small sideways part towards the door was cancelled by their separation push. Fixes: with clearance the move and the separation push are resolved one after the other, and steering checks a clearance-wide line (`IsSteerLineClear`, `IsWideLineWalkable`). Result: no stuck reports in 100 fights (16-22 s, was up to 35 s). Mixed seed 42 is now `0xC4B63228`; AoE and Taunt unchanged. New test ClearanceDoor (verified to fail without the wide line). Logs removed.
- Wall clearance and the door fix checked by the user in PIE.

## Branch `wall-variants` (tag `before-wall-variants`)

- First asset edit by script: `Scripts/MakeWallVariants.py` made `SM_Walls_007_300cm` (400 x 20 x 300, Z build scale 1.5) from the 2 m `SM_Walls_007`; `CreatePieceCatalog.py` added it (65 pieces). Lesson: `StaticMeshEditorSubsystem` does not exist in the `-run=pythonscript` commandlet, and `EditorStaticMeshLibrary` there returns -1 for the LOD count (it needs an editor), so the first run saved a plain unscaled copy; the script now runs in editor mode (`-ExecCmds="py ..., QUIT_EDITOR"`), refuses to run without the subsystem, and scales an existing variant of the wrong height.
- `SM_Walls_007_300cm` checked by the user in PIE.
- Door leaves and frames (the user's choices): catalog items and placed pieces got a `Slot`; pieces only replace pieces of the same layer and slot, so a door leaf (slot "Leaf", category folder `DoorLeaves`) and a frame (`Doors`) share a border. A leaf blocks walking and sight (a closed door); a frame alone is an opening. Move/erase without a selection take the leaf first. The user moved `SM_door_005/009/023` to `Catalogus/DoorLeaves`; `CreatePieceCatalog.py` now removes entries whose mesh left its category folder (here the three old `Doors/` ids). `PiecesDemo` used `Doors/SM_door_009` as its frame; it now uses the frame `Doors/SM_door_006`. 59/59 tests.
- Door leaves and frames checked by the user in PIE.

## Branch `wall-openings` (tag `before-wall-openings`)

- `wall-variants` (3 m wall, door leaves) merged to `main` first. Wall openings, decisions with the user: route B (real cuts with Geometry Script, over splitting walls per border, a material mask or pack modules with holes); the hole is the opening's mesh bounds or a custom box per catalog item; windows and door frames cut (slot "Opening"), door leaves sit in frames; gameplay stays per border: a frame alone is a passage even with a wall under it, a leaf or window blocks. Expected cost: only walls with openings become dynamic meshes (one draw per material, no LOD/Nanite on those few), the boolean runs once per distinct wall + cuts thanks to the cache; a log line measures it. Existing level files got the slots for their windows and door frames (BuildTest01 with a one-line text edit, to keep its layout). 60/60 tests.
- User feedback: in Cutaway/Down the door frame's top bar came down to 40 cm, because lowering squeezed every border piece in Z. By the user's choice lowering now cuts: each border piece gets a low version cut off at `LowWallHeight` (with the same openings, through the cut cache) and lowering swaps visibility, like The Sims. 60/60 tests.
- Lowering by cutting checked by the user in PIE. Two follow-ups: (1) a rainbow on the cut faces: the pack colors by a texture atlas and the cutter box's 0..1 UVs spanned it; the cutter's UVs now all take the UV of the wall triangle nearest the cut. (2) The door frame preview vanished inside the wall: walls under a previewed opening are now swapped for cut versions while it is shown.
- Atlas UV fix and preview cuts checked by the user in PIE. `Content/Meshes/` (local meshes) is ignored like `Content/Environment/`.

## Branch `leveldesigner-modes` (tag `before-leveldesigner-modes`)

- 2026-10-05. LevelDesigner layout, decisions with the user: the cell kinds wall/hedge/water are dropped entirely (tools, `Rows` in the level format, the grid's blocks); walls come from Build Mode. Level format 4 has no rows; older files load with their rows ignored. `Levels/Demo.json` and `Klaslokaal01.json` (built from cell kinds) were deleted. The tools became Build Mode / Unit Mode / Spawn Mode, each showing only its own controls (wave row and spawn list only in Spawn Mode). The catalog got two levels by folder: `Building/` (Walls, Windows, Doors, DoorLeaves, Floors), `Furniture/` (Chairs, Tables), `Props/` (Food, OfficeSupplies, Toys); the palette shows a row of groups and a row of subcategories, pieces from small to large with a size label. `RestructureCatalog.py` (one-off) moved 62 meshes and renamed the ids in the levels; `CreatePieceCatalog.py` now moves existing entries with their tuning when their mesh moved folder (before, a moved mesh lost its entry), and added 5 windows that were in the folder but not yet in the catalog (65 pieces). Tests: the level tests use blocking pieces instead of cell kinds, plus a version 3 file whose wall row is ignored. 60/60 tests; `DA_Setup_Test` seed 42 `0x2B6741F2` twice, `DA_Setup_Mixed` unchanged `0xC4B63228`, `PiecesDemo` `0x9D04F767` twice.
- Checked by the user in PIE; merged to `main`.

## Branch `unit-ghost` (tag `before-unit-ghost`)

- 2026-10-05. Unit ghost and unit rotation in the LevelDesigner, decisions with the user: the rotation is a start pose (stored in the level, shown in the editor and when a fight starts; presentation only, so checksums stay), for units and spawns alike, in 45 degree steps with one "last used" value; the ghost is the real unit model, see-through in the team color, over a green/red cell plate like the pieces. Level format 5 (`Rotation` on units and spawns; older files load with the old default look: team 0 facing +X, the rest and spawns -X). `BuildConfig` returns the rotations next to the definitions, so units spawned in a fight (also wave spawns) start in them. New `M_DesignGhost` (translucent, unlit, Fresnel edge) from `CreateDesignGhostMaterial.py`, referenced by `UCombatSettings::DesignGhostMaterial`. 60/60 tests (LevelFormat and LevelToConfig check rotations); `DA_Setup_Test` `0x2B6741F2` twice, `DA_Setup_Mixed` `0xC4B63228`, `PiecesDemo` `0x9D04F767`, all unchanged.
- Checked by the user in PIE; merged to `main`.

## Branch `solid-floors` (tag `before-solid-floors`)

- 2026-10-05. Solid floors with a color per placed piece, decisions with the user: four catalog pieces (1x1 to 4x4) instead of a size setting or a paint layer; the color is stored per piece (level format 6, `Color`, presentation only); swatches from `UCombatSettings::FloorColors` plus the engine color picker; an eyedropper on key I (Alt+click is the camera orbit). No new assets: the engine plane with `BasicShapeMaterial` (Color parameter) through a new catalog flag `bTintable`. The module now depends on AppFramework (color picker). 69 catalog pieces; 60/60 tests (LevelFormat checks the color round trip and white for older files); checksums unchanged (`0x2B6741F2` twice, Mixed `0xC4B63228`, `PiecesDemo` `0x9D04F767`).
- Bug (user report): placed solid floors showed a grey checkerboard. The engine plane uses `WorldGridMaterial`, not `BasicShapeMaterial`, so tinting the mesh's own material set a parameter it does not have. `ApplyTint` now puts a dynamic instance of the new setting `TintMaterial` (default `BasicShapeMaterial`) on every slot.
- Checked by the user in PIE; merged to `main`.

## Branch `wall-items` (tag `before-wall-items`)

- 2026-10-05. Wall items (paintings, clocks, posters), decisions with the user: a new catalog group (the user filled `Catalogus/WallProps/` with 166 meshes, one category without subcategories); placed on positions along a wall (4 per cell), on the side of the cursor, wide items over several positions and borders, replacing overlapping ones; only on borders with a wall and no opening, and they go with their wall; a height per item (catalog `MountHeight` 150 cm, PageUp/PageDown 10 cm steps); a tilt in the wall plane in 11.25 degree steps (the user's choice); hidden with their wall in Cutaway/Down. A new layer `ECombatPieceLayer::Wall` with `Facing` and `Height` on the level piece (format 7); presentation only, so checksums stay. New test `WallItems`; 61/61 tests; `DA_Setup_Test` `0x2B6741F2` twice, Mixed `0xC4B63228`, `PiecesDemo` `0x9D04F767`.
- The pack's wall props have their pivot on the back with the depth on +Y (142 of 166), so their front faces +Y; `CreatePieceCatalog.py` now turns such meshes half a turn (front -Y is the convention). 24 are centered on their pivot or have no depth (single planes) and keep the guess. The script logs every new item's size and pivot side, and warns for meshes thinnest in Z.
- Checked by the user in PIE; merged to `main`.

## Measurement: navigation on a 3x3 finer grid (phase 6 question)

- 2026-10-05. A temporary benchmark (removed again) ran levels on their own grid and on a copy with every cell split into 3x3 sub-cells (33 cm, blocking and edge walls copied, `PathLookaheadCells` x3), same settings, seeds 1..50 (40x40 stress levels: 10). Cost per tick rose x7.8 to x9.0 everywhere: PiecesDemo 0.017 -> 0.150 ms/tick (6.3 -> 53 ms per fight), Level-01 0.004 -> 0.034, a 40x40 level with a wall and three gaps 10v10 0.11 -> 0.87 and 25v25 0.14 -> 1.10 ms/tick (53 -> 404 ms per fight). Fight lengths stayed about the same (a few percent). So live play stays cheap (about 1 ms per 20 Hz tick at worst), batches get about 8x slower. Only averages were measured, not the cost of the heaviest single tick.
