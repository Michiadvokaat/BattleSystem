# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Working agreements

Follow the working agreements in @Docs/Werkafspraken.md (language, feature workflow, git, debugging, docs, Unreal build rules). They take precedence over defaults elsewhere in this file.

## Repo docs

- `Docs/STATUS.md`: current state and open work. Read it at the start of a session.
- `Docs/Ontwerp-Gevecht.md`: the design for the autobattle combat system (phases 1–6, in Dutch). When a phase is done or a decision changes, update its "Status" line and the "Besluiten" table.
- `Docs/Architecture.md`: the mechanics of each built system.
- `Docs/DEVLOG.md`: dated history. Search it; don't read it in full.
- `Docs/Licenses/README.md`: licenses for all third-party content.

## Project

Unreal Engine **5.8** C++ project (`BattleSystem.uproject`, single runtime module `BattleSystem`). Third-party asset packs from Fab go in `Content/ZZ_FAB/`. That folder and the other local content folders (`Characters`, `Environment`, `Meshes`, `Materials`, `Textures`) are gitignored (except `Materials/M_DesignGhost`), so repo content that references them only resolves when they exist locally.

## Build & run

Before building or running the editor headless, check whether the editor is running (`tasklist | grep -i unrealeditor`) and follow the Live Coding / close-editor rules in the working agreements.

```powershell
$UE = "C:\Program Files\Epic Games\UE_5.8"
$P  = "D:\Unreal\UnrealProjects\BattleSystem\BattleSystem.uproject"

# Build the editor target
& "$UE\Engine\Build\BatchFiles\Build.bat" BattleSystemEditor Win64 Development -Project="$P" -WaitMutex

# Regenerate project files (after adding/removing source files)
& "$UE\Engine\Build\BatchFiles\Build.bat" -ProjectFiles -Project="$P" -Game

# Headless fight(s). ExecCmds are comma-separated; in editor mode only QUIT_EDITOR exits (Quit hangs).
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -unattended -nullrhi -nosplash -nosound -ExecCmds="Combat.Simulate 42, Combat.Simulate 42, QUIT_EDITOR" -log

# Automation tests (all combat tests; a single one: BattleSystem.Combat.Determinism). -TestExit ends the process; the Quit is never reached.
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -unattended -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests BattleSystem.Combat; Quit" -TestExit="Automation Test Queue Empty" -log

# Fill the unit and skill tables (/Game/Combat/DT_Units, DT_Skills) from Data/Units.json and Data/Skills.json (editor must be closed).
# Append " export" inside -script="..." to write the tables back to the JSON files after editing them in the editor.
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -run=pythonscript -script="D:/Unreal/UnrealProjects/BattleSystem/Scripts/ImportCombatData.py" -unattended -nullrhi -nosplash

# (Re)create the cue table /Game/Combat/DA_CueTable (editor must be closed)
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -run=pythonscript -script="D:/Unreal/UnrealProjects/BattleSystem/Scripts/CreateCueTable.py" -unattended -nullrhi -nosplash

# Create or update the LevelDesigner piece catalog from /Game/Environment/Catalogus (local content; editor must be closed)
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -run=pythonscript -script="D:/Unreal/UnrealProjects/BattleSystem/Scripts/CreatePieceCatalog.py" -unattended -nullrhi -nosplash

# Make taller/lower copies of catalog meshes (editor mode, not -run=pythonscript; editor must be closed), then rerun CreatePieceCatalog.py
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -unattended -nullrhi -nosplash -nosound -ExecCmds="py D:/Unreal/UnrealProjects/BattleSystem/Scripts/MakeWallVariants.py, QUIT_EDITOR"

# (Re)create the LevelDesigner unit ghost material /Game/Materials/M_DesignGhost (editor must be closed)
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -run=pythonscript -script="D:/Unreal/UnrealProjects/BattleSystem/Scripts/CreateDesignGhostMaterial.py" -unattended -nullrhi -nosplash

# Import Mixamo FBX animations from D:/Unreal/Assets/Animations (subfolders mirrored) and retarget them to SKEL_Hero
# in /Game/Characters/Animations/Heroes; existing ones are skipped (local content; editor must be closed)
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -run=pythonscript -script="D:/Unreal/UnrealProjects/BattleSystem/Scripts/ImportMixamoAnimations.py" -unattended -nullrhi -nosplash

# Create the child animation set /Game/Characters/Animations/DA_AnimSet_Child (local content; editor must be closed)
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -run=pythonscript -script="D:/Unreal/UnrealProjects/BattleSystem/Scripts/CreateCharacterAnimSet.py" -unattended -nullrhi -nosplash
```

`Scripts/RestructureCatalog.py` was a one-off migration (catalog folders to `Group/Sub`, piece ids in `Levels/*.json`) and has already run; don't run it again.

Output goes to `Saved/Logs/BattleSystem.log`, which each run overwrites. Grep it for `LogCombat`, `Test Completed` or the script's log tag (`[CombatData]`, `[CombatCues]`, `[PieceCatalog]`, `[WallVariants]`, `[CombatAnimSet]`, `[DesignGhost]`, `[MixamoImport]`).

Fights always come from a LevelDesigner level. Console commands (in PIE or headless): `Combat.Start [seed]`, `Combat.Simulate [seed]`, `Combat.Stop`, `Combat.Batch <count> [startseed] [csv]` (headless statistics; CSV to `Saved/CombatBatch/`), `Combat.SaveReplay`, `Combat.Replay <file>` (`Saved/Replays/`), the player commands `Combat.Move <unit> <x> <y>`, `Combat.Ability <unit> [index]` and `Combat.CallWave` (start the next wave of a level now), and `Combat.SetSlot <unit> <slot> [mesh]` (presentation debug: change a swappable part of a unit's look). Simulate and Batch accept `script=<name>` (a `UCombatCommandScript`). Start, Simulate and Batch accept `level=<name>` (a LevelDesigner level from `Levels/<name>.json`, name without `.json`, e.g. `Combat.Simulate 42 level=WallWindows`); without it they use `DefaultLevel` from Project Settings > Game > Combat. The console variable `Combat.Debug 1|2` draws targets, steer points and the distance map, and `Combat.ShowRanges 1` draws the range of area attacks (taunt). In PIE the fight controls (seed, start, pause, speed, debug, replay, batch) are on the in-game control panel (`ACombatHUD`); it always plays the level in the LevelDesigner (at the start of play `DefaultLevel`), and the camera moves like the editor viewport (RMB look + WASD, MMB pan, LMB+RMB pan sideways/up-down, wheel zoom, Alt+LMB orbit, F overview; `ACombatPlayerController`).

All automation tests are in `Source/BattleSystem/Private/Combat/Tests/CombatSimulationTests.cpp`. They build their stats and grids in code and use no assets.

New UCLASS/USTRUCT types and header changes need a full build plus an editor restart. Live Coding only covers changes to function bodies in .cpp files.

## Combat architecture

The mechanics of built systems are in `Docs/Architecture.md`; later phases are in the design doc. All combat code goes in `Source/BattleSystem/Public|Private/Combat/`. There are three layers with a strict dependency direction: **grid ← simulation ← presentation**.

Unit and skill data (both layers: the functional part becomes simulation stats, the rest is presentation): `CombatUnitData`. Files per layer: grid `CombatGrid`, `CombatGridData`, `CombatNavigation`, `CombatDistanceMap`, `CombatPathfinding`, `CombatLevel`, `CombatPieces`; simulation `CombatSimulation`, `CombatEffects`, `CombatTypes`; glue `CombatSubsystem`, `CombatReplay`, `CombatBatch`; presentation `CombatUnitActor`, `CombatProjectileActor`, `CombatMeshMergeCache`, `CombatAnimation`, `CombatCueTable`, `CombatAnimPreview`, `CombatHUD`/`SCombat*`, `CombatPlayerController`, `CombatCamera`.

- **Grid**: `ACombatGrid`, one per level. `CellSize` defaults to 100 cm, the XY plane, and the origin is the actor location. Each cell has a walkable flag and a blocks-sight flag, and the borders between cells can hold edge walls (block walking and sight). The grid of a fight comes from its level (`FCombatLevel::ToGridData`); `ACombatGrid` only gives the origin and shows the level. The grid does not change during a fight. Routes run on a navigation layer (`CombatNavigation`): each cell split into 3x3 sub-cells, one nav grid per clearance class (unit radius, capped so every unit fits through a one-cell door). Units move along per-team, per-class multi-source Dijkstra distance maps (`FCombatDistanceMap`) towards their enemies. A separate A\* (`CombatPathfinding::FindPath`) uses 8 directions, no corner cutting, and deterministic tie-breaks. Movement and line checks stay on the cell grid.
- **Simulation (the source of truth)**: `FCombatSimulation` is a plain C++ class with **no `UWorld`, actors, or timers**, so it can run headless. It runs a fixed 20 Hz `Step()`. Units are `FCombatUnit` structs in a `TArray`. Each step produces an event buffer (`ECombatEventType` in `CombatSimulation.h`), and a state checksum is computed after every step.
- **Glue**: `UCombatSubsystem` (`UTickableWorldSubsystem`) builds the simulation from a level (`FCombatFightSource`) and a seed. It runs 0..N steps per frame using an accumulator capped per frame, passes events on, and supplies the interpolation alpha.
- **Fight settings**: `FCombatSimSettings` (`CombatReplay.h`) holds every setting that changes a fight, in simulation units. Live fights, replays and `Combat.Batch` all go through it. A new tuning value that affects the fight must be added there, or replays stop reproducing.
- **Levels**: LevelDesigner levels are JSON files in `Levels/` at the repo root (`FCombatLevel`, `CombatLevel.h`): grid size, starting units, enemy waves and placed pieces (walls on cell borders, floors, furniture, props, items hung on walls; their footprint and blocking are stored in the level, the `UCombatPieceCatalog` only gives the mesh). Every cell is open; only pieces block. A level is copied into the replays that use it, so a replay does not depend on the level file. The files in `Levels/` are always at the current `FCombatLevel::FormatVersion`: a format change raises it and converts the files (load and save again) in the same commit, with no conversion code left in `CombatLevels::FromJson`; missing fields just load as their defaults. Wave units are appended to the unit array when they spawn; code that needs a unit's type uses `FCombatUnit::SourceIndex`, not its unit ID.
- **Presentation (read-only)**: `ACombatUnitActor` and `ACombatProjectileActor` interpolate between simulation steps and react to events. They contain no gameplay logic and are never moved by physics. VFX, sound and debug colors come from `UCombatCueTable` (`DA_CueTable`), which maps the cue tag on an event (`FCombatEvent::Cue`) to its assets. A unit's look is the `FCombatLook` in its unit row (modular skeletal mesh slots as soft references merged into one mesh, swappable slots with Leader Pose, props, scale, and tag overrides); its random picks use their own stream, never the simulation's. Its `UCombatAnimSet` drives the animation: the AnimBP (parent `UCombatAnimInstance`, which only supplies inputs such as `Speed`, `bIsMoving` and `LocomotionPlayRate`) decides locomotion in its AnimGraph, montages per `Anim.*`/attack tag are timed so the `Impact` notify lands on the simulation's hit, hit reactions and a held death pose. `ACombatAnimPreview` shows units walking in the editor viewport without PIE, for animation tuning (`LocomotionRate` on the unit row).
- **UI is Slate, not UMG**: `ACombatHUD` hosts the `SCombat*` widgets in `Private/Combat/` (control panel, unit list, level designer with its spawn list). Player input goes through `ACombatPlayerController` → `UCombatSubsystem::IssueCommand`, which queues an `FCombatCommand` a few ticks ahead.
- **Units and skills**: rows of two DataTables, `DT_Units` (`FCombatUnitRow`: stats, skill row names, skill multipliers, look) and `DT_Skills` (`FCombatSkillRow`: an attack the AI uses, or one the player triggers with `bPlayerActivated`), set in Project Settings > Game > Combat > Units. Units (heroes and enemies) own skills by row name; several units can share one. The source is `Data/Units.json` and `Data/Skills.json`: change those and run `ImportCombatData.py`, or edit the tables in the editor and export them back (both, so the JSON in git stays the truth). Levels name units by row name (`"type": "Tank"`). A fight copies the rows it uses into an `FCombatUnitCatalog` (`FCombatUnitType::ToSimStats` applies the multipliers), which replays store, so tuning the tables does not change old replays. A unit naming a missing skill stops the fight.
- **Data**: `UCombatCommandScript` and `UCombatCueTable` are Data Assets. All tuning values live in `UCombatSettings` (`UDeveloperSettings`, saved to `DefaultGame.ini`), never hardcoded. Skill types (`ECombatSkillType`) and look slots (`ECombatLookSlot`) are enums; statuses, effects, cues and animation actions are GameplayTags (open sets, defined in data, with hierarchy).
- **GAS is deliberately not used** at runtime. Only GameplayTags and GAS's data model are used, reimplemented as tick-based structs (`FCombatEffectDefinition`, `FCombatActiveEffect`).

### Determinism rules (hard constraints in all simulation code)

- Process units in ID order. Only use `TArray`s, never iterate over `TMap`/`TSet`, and never sort or compare by pointer.
- All randomness comes from one seeded `FRandomStream`. Never use `FMath::Rand`/`FRand`.
- No `DeltaTime`, wall-clock time, NavMesh, Detour, CharacterMovement, physics, or overlap events in the logic. UE collision is only allowed for mouse picking.
- Apply damage and effects in two passes: collect everything in the step first, then apply it all at once.
- Player input reaches the simulation only as `FCombatCommand`s queued for a future tick (the command log). Never change simulation state directly from input or presentation.
- Acceptance check: running `Combat.Simulate 42` twice must print the same checksum. `BattleSystem.Combat.Determinism` checks the checksum after every step. The expected seed-42 checksum of the reference level is in `Docs/STATUS.md`; a change that is meant to alter fights changes them, so update them there (a change that is not meant to must leave them as they are).
