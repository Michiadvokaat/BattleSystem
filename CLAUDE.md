# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project state

Unreal Engine **5.8** C++ project (`BattleSystem.uproject`, single runtime module `BattleSystem`). The source is still the empty template: no gameplay code, no maps, no assets. All planned work is in **`Docs/Ontwerp-Gevecht.md`** (in Dutch), the design for a deterministic autobattle combat system that will be built in phases 1–6. Read it before you implement anything. Keep it current: when a phase is done or a decision changes, update its "Status" line and the "Besluiten" table.

The project is not a git repository.

## Build & run

Engine install: `C:\Program Files\Epic Games\UE_5.8`. Run these from PowerShell (`$UE` = engine root):

```powershell
$UE = "C:\Program Files\Epic Games\UE_5.8"
$P  = "D:\Unreal\UnrealProjects\BattleSystem\BattleSystem.uproject"

# Build the editor target (the main dev build)
& "$UE\Engine\Build\BatchFiles\Build.bat" BattleSystemEditor Win64 Development -Project="$P" -WaitMutex

# Regenerate project files (needed after adding/removing source files)
& "$UE\Engine\Build\BatchFiles\Build.bat" -ProjectFiles -Project="$P" -Game

# Headless run, e.g. for the planned Combat.Simulate / Combat.Batch console commands
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -game -nullrhi -unattended -nosplash -ExecCmds="Combat.Simulate 42, Quit" -log

# Automation tests (once tests exist under e.g. "BattleSystem.Combat")
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -unattended -nullrhi -nosplash -ExecCmds="Automation RunTests BattleSystem.Combat; Quit" -log
```

From the design doc: **new C++ classes (UCLASS/USTRUCT) need a full build**, meaning regenerate the project files, build, and restart the editor. Live Coding is only suitable for changes to the bodies of existing functions. A build fails if the editor is open with Live Coding active, so close the editor first or use Live Coding (Ctrl+Alt+F11).

## Planned architecture (from the design doc)

All combat code goes in `Source/BattleSystem/Public|Private/Combat/`. There are three layers with a strict dependency direction: **grid ← simulation ← presentation**.

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
- Acceptance check: running `Combat.Simulate 42` twice must print the same checksum.

## Module dependencies

`BattleSystem.Build.cs` currently has Core, CoreUObject, Engine, InputCore, and EnhancedInput. Phase 1 adds `GameplayTags` (and `DeveloperSettings` for `UCombatSettings`).
