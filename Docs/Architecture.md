# Architecture

Mechanics per system, as built. Read the relevant section before changing a system, and keep it current. Planned work for later phases is in `Docs/Ontwerp-Gevecht.md`.

## Combat (phases 1–3)

Code: `Source/BattleSystem/{Public,Private}/Combat/`. Layers: grid ← simulation ← presentation. Only the presentation layer touches actors during a fight.

### Grid

- `FCombatGridData` (plain struct) holds the size, cell size and per-cell `ECombatCellFlags` (`Blocked`, `BlocksSight`). Positions are **grid-local cm**: (0,0) is the corner of cell (0,0). Out-of-bounds cells are not walkable and block sight.
- `ACombatGrid` (placed in the level) owns a `FCombatGridData`. Its actor location is the grid origin. Rotation and scale are ignored. The floor is the engine plane, scaled in `OnConstruction`.
- Cell flags are built lazily by `GetGridData()` (also in `BeginPlay`). The grid iterates all `ACombatObstacle`s in the world and ORs their flags into its footprint cells. *Deviation from the design:* obstacles don't register themselves; the grid collects them. That makes the result independent of BeginPlay order.
- `ACombatObstacle`: the root is a `UBoxComponent` with no collision, plus a cube mesh scaled to the box. Its footprint is every in-bounds cell whose center lies inside the box's world AABB. If no center lies inside, the footprint is the cell under the actor.
- `CanStep(Cell, Offset)`: the target cell must be walkable, and a diagonal step also needs both orthogonal neighbors walkable (no corner cutting). `NeighborOffsets` fixes the neighbor order for all searches.
- `IsLineClear(From, To, IsCellClear)`: a grid traversal (Amanatides & Woo) over every cell the segment crosses. Passing exactly through a corner needs both side cells clear. It checks the center line only; unit radius is ignored until the clearance work in phase 6. `IsLineWalkable` uses it with walkable cells (melee, movement), and `HasLineOfSight` uses it with cells that don't block sight (ranged, projectiles). A sight-only obstacle (`bBlocksWalkability` off) therefore blocks shots but not walking.
- With `bDrawDebugCells` on, the grid draws persistent debug lines and red boxes for blocked cells at BeginPlay.

### Distance map (`FCombatDistanceMap`)

- Multi-source Dijkstra over the grid, using `CanStep` with integer costs 10 (straight) and 14 (diagonal). Each cell stores the distance and the ID of the nearest source.
- The heap order is (distance, unit ID, cell index), and a cell is improved on a shorter distance or an equal distance with a lower ID. Equal routes therefore always go to the lowest ID.
- `GetNextCell` returns the neighbor (reachable with `CanStep`) with the lowest distance below the current cell's. The first in neighbor order wins ties.

### Simulation (`FCombatSimulation`)

- Input is an `FCombatSimConfig`: the grid copy, `FCombatUnitSpawn`s (stats, team, start cell; unit ID = index), seed, tick rate, `MaxTicks`, `MaxFirstAttackDelayTicks`, `RetargetIntervalTicks`, `PathLookaheadCells` and `SeparationStrength`. The simulation never sees UObjects; `UCombatUnitDefinition::ToSimStats` converts seconds to ticks once.
- Stats: `FCombatUnitStats` has `Attacks` (`FCombatAttackStats`: range, damage, cooldown and windup in ticks, `bNeedsWalkableLine` for melee, `bNeedsLineOfSight`, `ProjectileSpeed` (0 = direct hit), and `SourceIndex` into the definition). `IsRanged()` = it doesn't need a walking line. Each unit has one cooldown per attack (`AttackCooldowns`).
- The constructor places units at their start-cell centers. It draws each unit's `FirstAttackDelayTicks` (0..Max) from the seeded `FRandomStream`, in ID order. This is the only randomness so far. It also collects the team values in order of first appearance; each team has one distance map towards its enemies.
- `Step()`:
  1. Copy `Position` to `PreviousPosition` for all units and projectiles. All decisions in this step read `PreviousPosition`, so the processing order cannot matter.
  2. Rebuild the distance maps if `(Tick - 1) % RetargetIntervalTicks == 0` or a unit died in the previous step. The sources are the cells of all living enemies.
  3. Each living unit, in ID order (`UpdateUnit`):
     - Decrease every attack cooldown.
     - `UpdateCombat` decides the desired move:
       - If the unit is winding up an attack: no move. When the windup reaches 0 and the windup target is still alive, `FireAttack` either queues the hit (no projectile) or spawns a projectile at the unit (`ProjectileSpawned`).
       - Otherwise, choose a target (`ChooseTarget`):
         1. A unit with a ranged attack first takes the nearest enemy (as the crow flies, lowest ID on ties) that its longest ranged attack can hit now: in range and, if needed, in line of sight (`FindVisibleEnemyInRange`).
         2. Else the nearest enemy recorded in the unit's cell of its team's distance map.
         3. If no enemy is reachable, the nearest enemy as the crow flies.
       - `FindUsableAttack`: of the attacks that can reach the target now (gap ≤ range, walking line if melee, line of sight if needed), take the one with the smallest range, regardless of its cooldown. If there is one: no move, and `TryStartAttack` counts down `FirstAttackDelayTicks` first. Then, once that attack's cooldown is 0, it emits `Attack` (with `AttackIndex`), sets the cooldown, and starts the windup (or fires right away if the windup is 0). Close up a mixed unit thus uses melee and waits for it, and further away it shoots.
       - Clear walking line but no usable attack: move straight at the target by at most `MoveSpeed * FixedDt`, stopping just inside the longest range that will work from there (ranged only counts with line of sight). So archers stop at range and don't kite.
       - No clear line: `FindRouteSteerPoint` follows `GetNextCell` from the unit's cell for up to `PathLookaheadCells` cells. It steers to the farthest of those cell centers that is in a clear line (the first step is always taken), so routes are smoothed. Without a route it steers straight at the target.
     - `ComputeSeparation`: each living unit, ally or enemy, that overlaps (center distance < sum of radii) pushes by `overlap * 0.5 * SeparationStrength`. Units exactly on top of each other split along X: the lower ID goes to -X.
     - `ResolveMove`: the new position is previous + move + push. If it lands in an unwalkable cell (blocked or out of bounds), the unit tries X only, then Y only, else stays. So units never end in a blocked cell.
     - `SteerPoint` stores what the unit steered at, for debugging.
  4. `UpdateProjectiles`, in spawn order:
     - If the target is dead, the projectile ends.
     - Otherwise it flies at the target's new position (homing) by `Speed * FixedDt`. If it reaches the target (distance − radius ≤ step) with line of sight, it queues a hit and ends.
     - If the step crosses a sight-blocking cell, it ends without a hit.
     - Each end emits `ProjectileEnded`; ended projectiles are removed.
  5. `ApplyPendingHits`: apply all queued hits (`Hit` events), then mark every unit with HP ≤ 0 dead (`Death` events, and the distance maps are marked dirty). Units that hit each other in the same step both die.
  6. Outcome: no team left → `Draw`, one team left → `TeamWon`, `Tick >= MaxTicks` → `TimeLimit`.
  7. Checksum: CRC32 over the tick and each unit's position, HP, target, attack cooldowns, windup attack, first-attack delay, windup and alive flag, plus each projectile's ID, position and target.
- Events (`GetEvents()`) are valid until the next `Step()`.

### Subsystem (`UCombatSubsystem`, game/PIE worlds only)

- `BuildSimConfig` takes the world's first `ACombatGrid`, or the fallback grid from settings at origin (0,0,0). It skips setup entries without a definition or with a start cell that is out of bounds or blocked, with a warning.
- `StartFight` replaces any running fight. It spawns one actor per unit: the definition's `ActorClass`, or `ACombatUnitActor`. It keeps the definitions (`UnitDefinitions`) for per-attack presentation settings.
- Projectiles: on `ProjectileSpawned` it spawns the attack definition's `ProjectileActorClass` (or `ACombatProjectileActor`), found through `FCombatAttackStats::SourceIndex`, into `ProjectileActors` (by projectile ID). On `ProjectileEnded` it destroys that actor. Every frame, projectiles are interpolated like units.
- `Tick`:
  - Add the engine DeltaTime × `TimeScale` to the accumulator, unless the fight is paused (`SetPaused`, `SetTimeScale`), and run up to `MaxStepsPerFrame` steps. Pause and speed only change when steps run, so the fight itself is unchanged. After each step, dispatch its events to the actors.
  - Clamp the accumulator to one step, so a backlog is dropped.
  - Push the interpolated state to the actors with alpha = accumulator / FixedDt. The facing is towards the target, otherwise along the velocity.
  - When the fight ends, log and show the result once.
  - `Combat.Debug` (console variable): `1` draws a line from each unit to its target in the team color, and a yellow line to its steer point when that is not the target. `2` also prints team 0's distance map per cell (in cells).
- Console commands: `Combat.Start`, `Combat.Simulate` (headless; uses the grid of the current world if there is one), `Combat.Stop`. Setups are found by asset name through the Asset Registry, or by object path.

### Presentation (`ACombatUnitActor`)

- Placeholder look: a body mesh (diameter = 2 × radius, height `BodyHeight`). It is `RangedBodyMesh` (default the engine cube) for units with a ranged attack and `MeleeBodyMesh` (default the engine cylinder) otherwise; the subsystem passes `bRanged` to `InitUnit`. The body has a dynamic material in the team color (made from `BodyMaterialBase`, default `BasicShapeMaterial`, through the `BodyColorParameter` `Color`; the cylinder's own `DefaultMaterial` has no color parameter), and a small cube "nose" for the facing. No collision.
- `OnAttack` lunges towards the target (sine over `LungeDuration`). `OnHit` flashes white and shows the damage as debug text. `OnDeath` shows "X" and hides the actor. Each one also calls a Blueprint event (`On Unit Attack/Hit/Death`) for subclasses.

### Control panel (`ACombatHUD`, `SCombatControlPanel`)

- `ACombatGameMode` uses `ACombatHUD`. At BeginPlay the HUD adds a Slate panel at the top left of the game viewport, inside a full-screen box that lets clicks elsewhere through. It shows the mouse cursor and sets input to Game+UI, so the console still works.
- Panel controls (built in code, English labels):
  - Setup dropdown (`UCombatSubsystem::GetAllSetupNames`, default selection from settings), seed field, and a Random seed button.
  - Start / Restart (`StartFight`), Stop, and Pause/Resume.
  - Speed 0.5× / 1× / 2× / 4×.
  - Debug Off / Targets / + Distance map, which sets the `Combat.Debug` console variable.
  - A status line with setup, seed, tick and running/paused, or after the fight the outcome and checksum.
- The active speed and debug level are tinted green. Nothing is saved: the panel starts from the defaults each time.

### Presentation (`ACombatProjectileActor`)

- A small engine sphere (`Diameter`) at `FlightHeight` in the team color (`BasicShapeMaterial`), facing the flight direction. No collision, no shadow.

### Data and settings

- `UCombatUnitDefinition`: HP, speed (cm/s), radius, `Attacks`, and `ActorClass`. `FCombatAttackDefinition` has type, range, cooldown, windup and damage, plus for ranged: `ProjectileSpeed`, `bRequiresLineOfSight` and `ProjectileActorClass`. `ToSimStats` keeps the `Attack.Melee` and `Attack.Ranged` entries (other types are skipped until their phase): melee needs a walking line, ranged needs sight if required.
- `UCombatSetup`: entries of (definition, team, start cell).
- Test assets (`/Game/Combat/DA_Krijger`, `DA_Brute`, `DA_Boogschutter` (ranged + dagger), `DA_Doelpop` (stands still, no attacks), `DA_Setup_Test`, `DA_Setup_Wall`, `DA_Setup_Archer`, `DA_Setup_Mixed`) are created by `Scripts/CreateCombatTestAssets.py`. The script only fills assets it creates, so values tuned in the editor are kept; `FORCE_UPDATE = True` overwrites them.
- `UCombatSettings` (`[/Script/BattleSystem.CombatSettings]` in `DefaultGame.ini`) holds the tick rate, max steps per frame, fight time limit, max first-attack delay, retarget interval, path lookahead, separation strength, auto-start (off: fights start from the control panel), default setup and seed, fallback grid, and team colors.
- Native tags (`CombatTags`): `Attack.Melee/Ranged/AoE/Taunt`.
- `ACombatGameMode`: players start as spectators. In `StartPlay`, after all actors have begun play, it auto-starts the default setup if enabled (`bAutoStartFight`, off by default).

### Tests

`Private/Combat/Tests/CombatSimulationTests.cpp` (`BattleSystem.Combat.*`) builds its own stats in code: GridData, Determinism (per-step checksums), SeedChangesFight, StrongerTeamWins, SimultaneousHits (two-phase damage → Draw), TimeLimit, LineWalkable, DistanceMap, TargetNearestByWalking, PathAroundWall (never in a blocked cell), Separation, LineOfSight, ArcherWalksAroundWall, ArcherPrefersVisibleTarget, BestAttackPerSituation, ProjectileEndsWhenTargetDies.

Performance: `Combat.Simulate` takes ~3.5 ms with `DA_Setup_Test` (5 units, 331 ticks, in Arena-01 with the wall) and with `DA_Setup_Mixed` (6 units). Phase 1 took 0.07 ms; the difference is the distance maps and line checks.
