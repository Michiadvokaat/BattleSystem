# Architecture

Mechanics per system, as built. Read the relevant section before changing a system, and keep it current. Planned work for later phases is in `Docs/Ontwerp-Gevecht.md`.

## Combat (phases 1–2)

Code: `Source/BattleSystem/{Public,Private}/Combat/`. Layers: grid ← simulation ← presentation. Only the presentation layer touches actors during a fight.

### Grid

- `FCombatGridData` (plain struct) holds the size, cell size and per-cell `ECombatCellFlags` (`Blocked`, `BlocksSight`). Positions are **grid-local cm**: (0,0) is the corner of cell (0,0). Out-of-bounds cells are not walkable and block sight.
- `ACombatGrid` (placed in the level) owns a `FCombatGridData`. Its actor location is the grid origin. Rotation and scale are ignored. The floor is the engine plane, scaled in `OnConstruction`.
- Cell flags are built lazily by `GetGridData()` (also in `BeginPlay`). The grid iterates all `ACombatObstacle`s in the world and ORs their flags into its footprint cells. *Deviation from the design:* obstacles don't register themselves; the grid collects them. That makes the result independent of BeginPlay order.
- `ACombatObstacle`: the root is a `UBoxComponent` with no collision, plus a cube mesh scaled to the box. Its footprint is every in-bounds cell whose center lies inside the box's world AABB. If no center lies inside, the footprint is the cell under the actor.
- `CanStep(Cell, Offset)`: the target cell must be walkable, and a diagonal step also needs both orthogonal neighbors walkable (no corner cutting). `NeighborOffsets` fixes the neighbor order for all searches.
- `IsLineWalkable(From, To)`: a grid traversal (Amanatides & Woo) over every cell the segment crosses. Passing exactly through a corner needs both side cells walkable. It checks the center line only; unit radius is ignored until the clearance work in phase 6.
- With `bDrawDebugCells` on, the grid draws persistent debug lines and red boxes for blocked cells at BeginPlay.

### Distance map (`FCombatDistanceMap`)

- Multi-source Dijkstra over the grid, using `CanStep` with integer costs 10 (straight) and 14 (diagonal). Each cell stores the distance and the ID of the nearest source.
- The heap order is (distance, unit ID, cell index), and a cell is improved on a shorter distance or an equal distance with a lower ID. Equal routes therefore always go to the lowest ID.
- `GetNextCell` returns the neighbor (reachable with `CanStep`) with the lowest distance below the current cell's. The first in neighbor order wins ties.

### Simulation (`FCombatSimulation`)

- Input is an `FCombatSimConfig`: the grid copy, `FCombatUnitSpawn`s (stats, team, start cell; unit ID = index), seed, tick rate, `MaxTicks`, `MaxFirstAttackDelayTicks`, `RetargetIntervalTicks`, `PathLookaheadCells` and `SeparationStrength`. The simulation never sees UObjects; `UCombatUnitDefinition::ToSimStats` converts seconds to ticks once.
- The constructor places units at their start-cell centers. It draws each unit's `FirstAttackDelayTicks` (0..Max) from the seeded `FRandomStream`, in ID order. This is the only randomness so far. It also collects the team values in order of first appearance; each team has one distance map towards its enemies.
- `Step()`:
  1. Copy `Position` to `PreviousPosition` for all units. All decisions in this step read `PreviousPosition`, so the processing order cannot matter.
  2. Rebuild the distance maps if `(Tick - 1) % RetargetIntervalTicks == 0` or a unit died in the previous step. The sources are the cells of all living enemies.
  3. Each living unit, in ID order (`UpdateUnit`):
     - Decrease the cooldown.
     - `UpdateCombat` decides the desired move:
       - If the unit is winding up an attack: no move. When the windup reaches 0, queue a hit if the windup target is still alive.
       - Otherwise, choose a target (`ChooseTarget`): the nearest enemy recorded in the unit's cell of its team's distance map. If no enemy is reachable, it falls back to the nearest enemy as the crow flies (lowest ID on ties).
       - Clear line (`IsLineWalkable`) to the target and the edge-to-edge gap ≤ attack range: no move. `TryStartAttack` counts down `FirstAttackDelayTicks` first; then, once the cooldown is 0, it emits `Attack`, sets the cooldown, and starts the windup (or queues the hit if the windup is 0). Melee needs a clear line, so it never hits through a wall.
       - Clear line but out of range: move straight at the target by at most `MoveSpeed * FixedDt`, stopping just inside range.
       - No clear line: `FindRouteSteerPoint` follows `GetNextCell` from the unit's cell for up to `PathLookaheadCells` cells. It steers to the farthest of those cell centers that is in a clear line (the first step is always taken), so routes are smoothed. Without a route it steers straight at the target.
     - `ComputeSeparation`: each living unit, ally or enemy, that overlaps (center distance < sum of radii) pushes by `overlap * 0.5 * SeparationStrength`. Units exactly on top of each other split along X: the lower ID goes to -X.
     - `ResolveMove`: the new position is previous + move + push. If it lands in an unwalkable cell (blocked or out of bounds), the unit tries X only, then Y only, else stays. So units never end in a blocked cell.
     - `SteerPoint` stores what the unit steered at, for debugging.
  4. `ApplyPendingHits`: apply all queued hits (`Hit` events), then mark every unit with HP ≤ 0 dead (`Death` events, and the distance maps are marked dirty). Units that hit each other in the same step both die.
  5. Outcome: no team left → `Draw`, one team left → `TeamWon`, `Tick >= MaxTicks` → `TimeLimit`.
  6. Checksum: CRC32 over the tick and each unit's position, HP, target, cooldown, first-attack delay, windup and alive flag.
- Events (`GetEvents()`) are valid until the next `Step()`.

### Subsystem (`UCombatSubsystem`, game/PIE worlds only)

- `BuildSimConfig` takes the world's first `ACombatGrid`, or the fallback grid from settings at origin (0,0,0). It skips setup entries without a definition or with a start cell that is out of bounds or blocked, with a warning.
- `StartFight` replaces any running fight. It spawns one actor per unit: the definition's `ActorClass`, or `ACombatUnitActor`.
- `Tick`:
  - Add the engine DeltaTime to the accumulator and run up to `MaxStepsPerFrame` steps. After each step, dispatch its events to the actors.
  - Clamp the accumulator to one step, so a backlog is dropped.
  - Push the interpolated state to the actors with alpha = accumulator / FixedDt. The facing is towards the target, otherwise along the velocity.
  - When the fight ends, log and show the result once.
  - `Combat.Debug` (console variable): `1` draws a line from each unit to its target in the team color, and a yellow line to its steer point when that is not the target. `2` also prints team 0's distance map per cell (in cells).
- Console commands: `Combat.Start`, `Combat.Simulate` (headless; uses the grid of the current world if there is one), `Combat.Stop`. Setups are found by asset name through the Asset Registry, or by object path.

### Presentation (`ACombatUnitActor`)

- Placeholder look: the engine cylinder (diameter = 2 × radius, height `BodyHeight`) with a dynamic material in the team color (made from `BodyMaterialBase`, default `BasicShapeMaterial`, through the `BodyColorParameter` `Color`; the cylinder's own `DefaultMaterial` has no color parameter), and a small cube "nose" for the facing. No collision.
- `OnAttack` lunges towards the target (sine over `LungeDuration`). `OnHit` flashes white and shows the damage as debug text. `OnDeath` shows "X" and hides the actor. Each one also calls a Blueprint event (`On Unit Attack/Hit/Death`) for subclasses.

### Data and settings

- `UCombatUnitDefinition`: HP, speed (cm/s), radius, `Attacks`, and `ActorClass`. Phase 1 uses the first `Attack.Melee` attack (range, cooldown, windup, damage).
- `UCombatSetup`: entries of (definition, team, start cell).
- Test assets (`/Game/Combat/DA_Krijger`, `DA_Brute`, `DA_Setup_Test`, `DA_Setup_Wall`) are created by `Scripts/CreateCombatTestAssets.py`. The script only fills assets it creates, so values tuned in the editor are kept; `FORCE_UPDATE = True` overwrites them.
- `UCombatSettings` (`[/Script/BattleSystem.CombatSettings]` in `DefaultGame.ini`) holds the tick rate, max steps per frame, fight time limit, max first-attack delay, retarget interval, path lookahead, separation strength, auto-start, default setup and seed, fallback grid, and team colors.
- Native tags (`CombatTags`): `Attack.Melee/Ranged/AoE/Taunt`.
- `ACombatGameMode`: players start as spectators. In `StartPlay`, after all actors have begun play, it auto-starts the default setup if enabled.

### Tests

`Private/Combat/Tests/CombatSimulationTests.cpp` (`BattleSystem.Combat.*`) builds its own stats in code: GridData, Determinism (per-step checksums), SeedChangesFight, StrongerTeamWins, SimultaneousHits (two-phase damage → Draw), TimeLimit, LineWalkable, DistanceMap, TargetNearestByWalking, PathAroundWall (never in a blocked cell), Separation.

Performance: `Combat.Simulate` with `DA_Setup_Test` (5 units, ~240 ticks) takes ~2.4 ms. Phase 1 took 0.07 ms; the difference is the distance maps and line checks.
