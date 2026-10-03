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

Unreal Engine **5.8** C++ project (`BattleSystem.uproject`, single runtime module `BattleSystem`). Third-party asset packs from Fab go in `Content/ZZ_FAB/`, which is gitignored. Repo content that references those packs only resolves when they are installed locally.

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

# Automation tests (all combat tests; a single one: BattleSystem.Combat.Determinism)
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -unattended -nullrhi -nosplash -nosound -ExecCmds="Automation RunTests BattleSystem.Combat; Quit" -TestExit="Automation Test Queue Empty" -log

# (Re)create the combat test data assets in /Game/Combat (editor must be closed)
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -run=pythonscript -script="D:/Unreal/UnrealProjects/BattleSystem/Scripts/CreateCombatTestAssets.py" -unattended -nullrhi -nosplash
```

Output goes to `Saved/Logs/BattleSystem.log`, which each run overwrites. Grep it for `LogCombat`, `Test Completed` or the script's log tag.

Console commands (in PIE or headless): `Combat.Start <seed> [setup]`, `Combat.Simulate <seed> [setup]`, `Combat.Stop`, `Combat.Batch <count> [setup] [startseed] [csv]` (headless statistics; CSV to `Saved/CombatBatch/`), `Combat.SaveReplay`, `Combat.Replay <file>` (`Saved/Replays/`), and the player commands `Combat.Move <unit> <x> <y>` and `Combat.Ability <unit> [index]`. Simulate and Batch accept `script=<name>` (a `UCombatCommandScript`). Start, Simulate and Batch accept `level=<name>` (a LevelDesigner level from `Levels/<name>.json`) instead of a setup. The console variable `Combat.Debug 1|2` draws targets, steer points and the distance map. In PIE the same controls (plus setup, seed, pause and speed) are on the in-game control panel (`ACombatHUD`). `setup` is an asset name (`DA_Setup_Test`) or an object path. Without it, the default setup from Project Settings > Game > Combat is used.

New UCLASS/USTRUCT types and header changes need a full build plus an editor restart. Live Coding only covers changes to function bodies in .cpp files.

## Combat architecture

The mechanics of built systems are in `Docs/Architecture.md`; later phases are in the design doc. All combat code goes in `Source/BattleSystem/Public|Private/Combat/`. There are three layers with a strict dependency direction: **grid ← simulation ← presentation**.

- **Grid**: `ACombatGrid`, one per level. `CellSize` defaults to 100 cm, the XY plane, and the origin is the actor location. Each cell has a walkable flag and a blocks-sight flag. `ACombatObstacle` registers its cell footprint with the grid when the level begins. The grid does not change during a fight. Pathfinding is A\* with 8 directions, no corner cutting, and deterministic tie-breaks. Later phases add per-team multi-source Dijkstra distance maps.
- **Simulation (the source of truth)**: `FCombatSimulation` is a plain C++ class with **no `UWorld`, actors, or timers**, so it can run headless. It runs a fixed 20 Hz `Step()`. Units are `FCombatUnit` structs in a `TArray`. Each step produces an event buffer (`Attack`, `Hit`, `Death`, `ProjectileSpawned`), and a state checksum is computed after every step.
- **Glue**: `UCombatSubsystem` (`UTickableWorldSubsystem`) builds the simulation from the grid plus a `UCombatSetup` and a seed. It runs 0..N steps per frame using an accumulator capped per frame, passes events on, and supplies the interpolation alpha.
- **Presentation (read-only)**: `ACombatUnitActor` interpolates between simulation steps and reacts to events. It contains no gameplay logic and is never moved by physics.
- **Data**: `UCombatUnitDefinition` and `UCombatSetup` are Data Assets. All tuning values live in `UCombatSettings` (`UDeveloperSettings`, saved to `DefaultGame.ini`), never hardcoded. Attack types and statuses are GameplayTags.
- **GAS is deliberately not used** at runtime. Only GameplayTags and GAS's data model are used, reimplemented as tick-based structs (`FCombatEffectDefinition`, `FCombatActiveEffect`).

### Determinism rules (hard constraints in all simulation code)

- Process units in ID order. Only use `TArray`s, never iterate over `TMap`/`TSet`, and never sort or compare by pointer.
- All randomness comes from one seeded `FRandomStream`. Never use `FMath::Rand`/`FRand`.
- No `DeltaTime`, wall-clock time, NavMesh, Detour, CharacterMovement, physics, or overlap events in the logic. UE collision is only allowed for mouse picking.
- Apply damage and effects in two passes: collect everything in the step first, then apply it all at once.
- Player input reaches the simulation only as `FCombatCommand`s queued for a future tick (the command log). Never change simulation state directly from input or presentation.
- Acceptance check: running `Combat.Simulate 42` twice must print the same checksum. `BattleSystem.Combat.Determinism` checks the checksum after every step.
