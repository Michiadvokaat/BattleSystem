# Architecture

Mechanics per system, as built. Read the relevant section before changing a system, and keep it current. Planned work for later phases is in `Docs/Ontwerp-Gevecht.md`.

## Combat (phase 1)

Code: `Source/BattleSystem/{Public,Private}/Combat/`. Layers: grid ← simulation ← presentation. Only the presentation layer touches actors during a fight.

### Grid

- `FCombatGridData` (plain struct) holds the size, cell size and per-cell `ECombatCellFlags` (`Blocked`, `BlocksSight`). Positions are **grid-local cm**: (0,0) is the corner of cell (0,0). Out-of-bounds cells are not walkable and block sight.
- `ACombatGrid` (placed in the level) owns a `FCombatGridData`. Its actor location is the grid origin. Rotation and scale are ignored. The floor is the engine plane, scaled in `OnConstruction`.
- Cell flags are built lazily by `GetGridData()` (also in `BeginPlay`). The grid iterates all `ACombatObstacle`s in the world and ORs their flags into its footprint cells. *Deviation from the design:* obstacles don't register themselves; the grid collects them. That makes the result independent of BeginPlay order.
- `ACombatObstacle`: the root is a `UBoxComponent` with no collision, plus a cube mesh scaled to the box. Its footprint is every in-bounds cell whose center lies inside the box's world AABB. If no center lies inside, the footprint is the cell under the actor.
- With `bDrawDebugCells` on, the grid draws persistent debug lines and red boxes for blocked cells at BeginPlay.

### Simulation (`FCombatSimulation`)

- Input is an `FCombatSimConfig`: the grid copy, `FCombatUnitSpawn`s (stats, team, start cell; unit ID = index), seed, tick rate, `MaxTicks` and `MaxFirstAttackDelayTicks`. The simulation never sees UObjects; `UCombatUnitDefinition::ToSimStats` converts seconds to ticks once.
- The constructor places units at their start-cell centers. It draws each unit's `FirstAttackDelayTicks` (0..Max) from the seeded `FRandomStream`, in ID order. This is the only randomness in phase 1.
- `Step()`:
  1. Copy `Position` to `PreviousPosition` for all units. All decisions in this step read `PreviousPosition`, so the processing order cannot matter.
  2. Each living unit, in ID order:
     - Decrease the cooldown.
     - If it is winding up an attack: stand still. When the windup reaches 0, queue a hit if the windup target is still alive.
     - Otherwise, target the nearest living enemy, measured center to center as the crow flies. On a tie, the lowest ID wins.
     - If the edge-to-edge gap is ≤ attack range: stand still. Count down `FirstAttackDelayTicks` first. Then, once the cooldown is 0, start an attack: emit an `Attack` event, set the cooldown, and either start the windup or queue the hit immediately if the windup is 0.
     - Otherwise, move straight at the target by at most `MoveSpeed * FixedDt`, stopping just inside range. Obstacles are ignored in phase 1; the position is clamped to the grid area.
  3. `ApplyPendingHits`: apply all queued hits (`Hit` events), then mark every unit with HP ≤ 0 dead (`Death` events). Units that hit each other in the same step both die.
  4. Outcome: no team left → `Draw`, one team left → `TeamWon`, `Tick >= MaxTicks` → `TimeLimit`.
  5. Checksum: CRC32 over the tick and each unit's position, HP, target, cooldown, first-attack delay, windup and alive flag.
- Events (`GetEvents()`) are valid until the next `Step()`.

### Subsystem (`UCombatSubsystem`, game/PIE worlds only)

- `BuildSimConfig` takes the world's first `ACombatGrid`, or the fallback grid from settings at origin (0,0,0). It skips setup entries without a definition or with a start cell that is out of bounds or blocked, with a warning.
- `StartFight` replaces any running fight. It spawns one actor per unit: the definition's `ActorClass`, or `ACombatUnitActor`.
- `Tick`:
  - Add the engine DeltaTime to the accumulator and run up to `MaxStepsPerFrame` steps. After each step, dispatch its events to the actors.
  - Clamp the accumulator to one step, so a backlog is dropped.
  - Push the interpolated state to the actors with alpha = accumulator / FixedDt. The facing is towards the target, otherwise along the velocity.
  - When the fight ends, log and show the result once.
- Console commands: `Combat.Start`, `Combat.Simulate` (headless; uses the grid of the current world if there is one), `Combat.Stop`. Setups are found by asset name through the Asset Registry, or by object path.

### Presentation (`ACombatUnitActor`)

- Placeholder look: the engine cylinder (diameter = 2 × radius, height `BodyHeight`) with a dynamic material in the team color (made from `BodyMaterialBase`, default `BasicShapeMaterial`, through the `BodyColorParameter` `Color`; the cylinder's own `DefaultMaterial` has no color parameter), and a small cube "nose" for the facing. No collision.
- `OnAttack` lunges towards the target (sine over `LungeDuration`). `OnHit` flashes white and shows the damage as debug text. `OnDeath` shows "X" and hides the actor. Each one also calls a Blueprint event (`On Unit Attack/Hit/Death`) for subclasses.

### Data and settings

- `UCombatUnitDefinition`: HP, speed (cm/s), radius, `Attacks`, and `ActorClass`. Phase 1 uses the first `Attack.Melee` attack (range, cooldown, windup, damage).
- `UCombatSetup`: entries of (definition, team, start cell).
- Test assets (`/Game/Combat/DA_Krijger`, `DA_Brute`, `DA_Setup_Test`) are created by `Scripts/CreateCombatTestAssets.py`. Re-running the script overwrites their values.
- `UCombatSettings` (`[/Script/BattleSystem.CombatSettings]` in `DefaultGame.ini`) holds the tick rate, max steps per frame, fight time limit, max first-attack delay, auto-start, default setup and seed, fallback grid, and team colors.
- Native tags (`CombatTags`): `Attack.Melee/Ranged/AoE/Taunt`.
- `ACombatGameMode`: players start as spectators. In `StartPlay`, after all actors have begun play, it auto-starts the default setup if enabled.

### Tests

`Private/Combat/Tests/CombatSimulationTests.cpp` (`BattleSystem.Combat.*`) builds its own stats in code: GridData, Determinism (per-step checksums), SeedChangesFight, StrongerTeamWins, SimultaneousHits (two-phase damage → Draw), TimeLimit.
