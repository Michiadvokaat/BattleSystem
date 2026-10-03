# Architecture

Mechanics per system, as built. Read the relevant section before changing a system, and keep it current. Planned work for later phases is in `Docs/Ontwerp-Gevecht.md`.

## Combat (phases 1–5)

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

### A* (`CombatPathfinding::FindPath`)

- Same grid rules and integer costs as the distance map, with an octile heuristic. The open list is ordered by (F, H, cell index), so equal routes always come out the same. It returns start..goal, or false when the goal is unreachable.

### Effects (`FCombatEffectList`, `CombatEffects.h`)

- `FCombatEffectStats`:
  - `EffectTag`: the identity for stacking.
  - `DurationTicks`.
  - `Stacking`: `Refresh` restarts the duration and takes the newest source; `Stack` does the same and adds a stack up to `MaxStacks`; `Ignore` keeps the existing effect.
  - `GrantedTags` and `BlockedByTags`.
- `Apply` refuses an effect if the unit's innate tags or any active effect's granted tags match its `BlockedByTags`. `Tick` counts all effects down and removes those at 0.
- Modifiers: `MoveSpeedMultiplier`, `DamageDealtMultiplier` and `DamageTakenMultiplier`, applied once per stack. `GetMoveSpeedMultiplier()` (and the others) multiply over the effects in order. Movement uses the move-speed factor. Damage is base × the attacker's dealt factor (at the moment it fires) × the target's taken factor (when it lands). Threat uses the final damage.
- `FindSourceOfTag` returns the source of the effect that grants a tag, choosing the most ticks left and then the lowest source ID. The checksum uses the source, attack and effect indices, never tag names (FName indices can differ between runs).

### Simulation (`FCombatSimulation`)

- Input is an `FCombatSimConfig`: the grid copy, `FCombatUnitSpawn`s (stats, team, start cell; unit ID = index), seed, tick rate, `MaxTicks`, `MaxFirstAttackDelayTicks`, `RetargetIntervalTicks`, `PathLookaheadCells`, `SeparationStrength`, and the threat settings: `ThreatDecayFactorPerTick` and `ThreatDecayAmountPerTick` (each tick threat = threat × factor − amount, which covers both half-life and linear decay), `ThreatThreshold`, `ThreatSwitchRatio` and `RetargetDistanceMargin`. The simulation never sees UObjects; `UCombatUnitDefinition::ToSimStats` converts seconds to ticks once.
- Stats: `FCombatUnitStats` has `Attacks` (`FCombatAttackStats`: range, damage, cooldown and windup in ticks, `bNeedsWalkableLine` for melee, `bNeedsLineOfSight`, `ProjectileSpeed` (0 = direct hit), `SourceIndex` into the definition, `ThreatMultiplier`, `Effects`, and for area attacks `AreaShape`, `AreaRadius`, `ConeCosHalfAngle`, `TelegraphTicks`, `bAffectsEnemies`, `bAffectsAllies` and `ImpactCue`). `IsTargeted()` = not `CircleAroundSelf`. `IsRanged()` = targeted and needs no walking line. `IsArea()` = has a shape. Innate `Tags` come from the definition. Each unit has one cooldown per attack (`AttackCooldowns`).
- `FCombatArea::Contains(Position, Radius)`:
  - `CircleAroundSelf`: edge to edge from the attacker, within `Radius`.
  - `CircleAtTarget`: the unit's edge within `Radius` of the center.
  - `Cone`: edge to edge within `Radius`, and the angle to the unit's center at most half the cone angle (a dot product against `ConeCosHalfAngle`).
- The constructor places units at their start-cell centers. It draws each unit's `FirstAttackDelayTicks` (0..Max) from the seeded `FRandomStream`, in ID order. This is the only randomness so far. It also collects the team values in order of first appearance; each team has one distance map towards its enemies.
- `Step()`:
  1. Copy `Position` to `PreviousPosition` for all units and projectiles. All decisions in this step read `PreviousPosition`, so the processing order cannot matter.
  2. For every living unit: tick its effects, and decay its threat. Entries at ≤ 0 or on dead enemies are removed.
  3. Rebuild the distance maps if `(Tick - 1) % RetargetIntervalTicks == 0` or a unit died in the previous step. The sources are the cells of all living enemies.
  4. `UpdatePendingAreas`: count telegraphed areas down. At 0 an area goes off (`ResolveArea`) using the positions at the start of this step. Areas placed during this step only start counting next step, so an area goes off exactly `TelegraphTicks` steps after it was placed.
  5. Each living unit, in ID order (`UpdateUnit`):
     - Decrease every attack cooldown.
     - `UpdateCombat` decides the desired move:
       - If the unit is winding up an attack: no move. When the windup reaches 0 and the windup target is still alive (or the attack is an area attack), `FireAttack` does one of three things:
         - queues the hit (no projectile);
         - spawns a projectile at the unit (`ProjectileSpawned`);
         - for an area attack, `PlaceArea` fixes an `FCombatArea`: the target's position for `CircleAtTarget`, or the attacker's position and the direction to the target for `CircleAroundSelf` and `Cone`. Without a telegraph it goes off right away (`ResolveArea`). With one it becomes an `FCombatPendingArea` (`AreaTelegraphStarted`) and stays where it was placed.
         - `ResolveArea` emits `AreaAttackFired` (with the area and the cue). It then queues a hit for every living unit inside the area (`FCombatArea::Contains`) that the team flags allow (`bAffectsEnemies` / `bAffectsAllies`; allies include the attacker), in ID order. Line of sight from the area's center is checked if the attack needs it.
         Every queued hit carries damage, threat (damage × `ThreatMultiplier`) and the attack index.
       - Areas around the unit first (`FindReadyAreaAttack`, `CircleAroundSelf`: taunt, auras). One starts when it is off cooldown (no target, `TargetId` = `INDEX_NONE`) and an affected unit inside it either is an enemy it would damage or lacks one of its effects from this unit. It ignores the first-attack delay. Targeted AoE (`CircleAtTarget`, `Cone`) goes through `FindUsableAttack` like any other attack: it needs line of sight if `bRequiresLineOfSight` is set, otherwise a walking line like melee.
       - `UpdateTarget`: if the unit has `Status.Taunted`, the source of that effect becomes the target right away (`Taunt`). Otherwise `ChooseTarget` runs on retarget ticks (`(Tick - 1) % RetargetIntervalTicks == 0`), when there is no living target, or when a taunt has just ended. In order of priority, with hysteresis:
         1. **Threat**: the highest threat entry (lowest ID on ties) if it is ≥ `ThreatThreshold`. The current target is kept while its own threat is ≥ the threshold and the best is below current × `ThreatSwitchRatio`.
         2. **Visible** (units with a ranged attack): the nearest enemy that the longest ranged attack can hit now (`CanShootNow`). The current target is kept while it can still be shot.
         3. **Nearest**: the enemy recorded in the unit's cell of its team's distance map, or as the crow flies if none is reachable. The current target is kept unless the new one is more than `RetargetDistanceMargin` closer (as the crow flies).
         `TargetReason` stores which rule chose it.
       - `FindUsableAttack`: of the attacks that can reach the target now (gap ≤ range, walking line if melee, line of sight if needed), take the one with the smallest range, regardless of its cooldown. If there is one: no move, and `TryStartAttack` counts down `FirstAttackDelayTicks` first. Then, once that attack's cooldown is 0, it emits `Attack` (with `AttackIndex`), sets the cooldown, and starts the windup (or fires right away if the windup is 0). Close up a mixed unit thus uses melee and waits for it, and further away it shoots.
       - Clear walking line but no usable attack: move straight at the target by at most `MoveSpeed * FixedDt`, stopping just inside the longest range that will work from there (ranged only counts with line of sight). So archers stop at range and don't kite.
       - No clear line: `FindRouteSteerPoint` steers along a route for up to `PathLookaheadCells` cells. It steers to the farthest route cell center that is in a clear line (the first step is always taken), so routes are smoothed.
         - If the team's distance map leads to this target (its nearest ID in the unit's cell), the route follows `GetNextCell`.
         - Otherwise (threat, taunt, hysteresis), the unit uses its own A* path (`Unit.Path`), recomputed when the unit's or the target's cell changes.
         - Without a route it steers straight at the target.
     - `ComputeSeparation`: each living unit, ally or enemy, that overlaps (center distance < sum of radii) pushes by `overlap * 0.5 * SeparationStrength`. Units exactly on top of each other split along X: the lower ID goes to -X.
     - `ResolveMove`: the new position is previous + move + push. If it lands in an unwalkable cell (blocked or out of bounds), the unit tries X only, then Y only, else stays. So units never end in a blocked cell.
     - `SteerPoint` stores what the unit steered at, for debugging.
  6. `UpdateProjectiles`, in spawn order:
     - If the target is dead, the projectile ends.
     - Otherwise it flies at the target's new position (homing) by `Speed * FixedDt`. If it reaches the target (distance − radius ≤ step) with line of sight, it queues a hit and ends.
     - If the step crosses a sight-blocking cell, it ends without a hit.
     - Each end emits `ProjectileEnded`; ended projectiles are removed.
  7. `ApplyPendingHits`, in three passes so the processing order cannot matter:
     1. Apply all damage (`Hit` events with the attack's cue, damage > 0 only; damage × the target's taken factor), adding threat on the source to the victim's list. The list holds at most 8 entries; when full, the lowest is replaced only by more threat.
     2. Apply the attack's effects to every hit target that is still alive (`EffectApplied` events), in hit order and then effect order.
     3. Mark every unit with HP ≤ 0 dead (`Death` events; its threat, effects and path are cleared, and the distance maps are marked dirty). Units that hit each other in the same step both die.
  8. Outcome: no team left → `Draw`, one team left → `TeamWon`, `Tick >= MaxTicks` → `TimeLimit`.
  9. Checksum: CRC32 over the tick and each unit's position, HP, target, attack cooldowns, windup attack, first-attack delay, windup, alive flag, target reason, threat entries and effects, plus each projectile's ID, position and target, and each pending area's ID, remaining ticks, center and direction.
- Events (`GetEvents()`) are valid until the next `Step()`.

### Levels (`FCombatLevel`, `CombatLevel.h`)

- A level (made with the LevelDesigner) is readable JSON in `<Project>/Levels/<Name>.json` and goes into git.
  - Fields: `Width` and `Height` (5–40), `CellSize` (100), `Rows` (one string per Y, one character per cell: `.` open, `#` wall = blocked + blocks sight, `h` hedge = blocks sight, `~` water = blocked), and `Units` (`Type` = definition asset name, `Team`, `Cell`).
  - `Normalize` clamps the size and pads or cuts the rows; unknown characters count as open. `Resize` removes cells and units outside the new size.
- `CombatLevels::BuildConfig` makes a simulation config: the grid from the rows, and units through a resolver (type → definition). Units of an unknown type or on an unwalkable cell are skipped with a warning; hedge cells are fine.
- `FCombatFightSource` (subsystem) is a setup asset (on the arena's own grid) or a level. `BuildSimConfigFromSource`, `StartFightFromSource`, `RunBatchInWorld` and replays all take it.
- Arena (`ACombatGrid::ApplyLevel`):
  - It resizes the floor and redraws the debug cells for the level.
  - It fills three instanced meshes with blocks: walls grey, hedges green and water blue and flat, with heights `WallHeight` 30, `HedgeHeight` 20 and `WaterHeight` 6 cm (editable on the grid actor).
  - It hides the placed `ACombatObstacle`s. `GetGridData()` stays the arena's own grid (for setups).
  - `ClearLevel` restores everything when a setup fight starts.
  - The subsystem points the view camera straight down with grid X (width) to the right and Y (height) downwards, like the rows of a level file. It fits it above the shown grid with a 12% margin, using its FOV and the viewport aspect, and puts the camera back for setups.
- LevelDesigner (`SCombatLevelDesigner`, bottom left; presentation only, no simulation):
  - **Edit** enters edit mode (`EnterDesignMode`). It stops the fight and edits the level of the fight on screen if it had one, otherwise the last edited level, otherwise an empty 20×12 level. The arena shows it (`ApplyLevel`, camera fit) with preview unit actors on their cells, without AI. Edit again leaves, and the arena is restored.
  - Rows: Name + **Save** (`Levels/<name>.json`; the name is cleaned to letters, digits, - and _), a level dropdown + **Load** + **New**, and Size W/H spin boxes (5–40; shrinking drops what falls outside, and the camera refits).
  - Tool: Wall / Hedge / Water / Unit, with a unit type (all definition assets) and Team 0/1 for Unit.
  - **Play** (or Start in the control panel while editing) starts a fight from the edited level as it is, saved or not, with the default seed (Play) or the seed field (Start).
  - Mouse (`ACombatPlayerController` in edit mode):
    - Left click places with the tool; holding paints a stroke while the cursor moves (`PlayerTick`). Units are only placed on a press, not while dragging.
    - Right click (and drag) erases both the unit and the cell kind.
    - Walls and water remove a unit on their cell, and units cannot be placed on walls or water. Placing on a unit replaces it.
    - The view (blocks, previews) rebuilds only when something changed. Button state is re-checked every tick, because a release over the HUD never reaches the game.
- `Levels/Demo.json` is Arena-01's wall plus `DA_Setup_Taunt`'s units, with a hedge and water added. It gives the same fight as that setup (`0x7CE33AAB`).

### Player commands (`FCombatCommand`, `CombatTypes.h`)

- A command is `Tick`, `UnitId`, `Type`, and either `TargetCell` (for `Move`) or `AbilityIndex` (for `Ability`). With the setup, seed and settings, the command log determines the fight.
- Sources: `FCombatSimConfig::Commands` (a script or replay), queued in the constructor sorted by tick with the given order kept; or `QueueCommand` during the fight, which accepts only `Tick > GetTick()`. Every accepted command goes into `GetCommandLog()`.
- `ExecuteDueCommands` runs at the start of a step, after `UpdatePendingAreas` and before the units, in (tick, given order). `ExecuteCommand` emits `CommandExecuted`, or `CommandRejected` for a dead or unknown unit, a blocked or out-of-bounds move cell, or an unknown or non-area ability. Rejected commands stay in the log.
- `Move`: sets `bHasMoveOrder` and `MoveTargetCell`, and cancels a windup. While the order is active, `UpdateMoveOrder` replaces all AI. There is no targeting and no attacking, and taunts are ignored (the player's command wins). The unit steers straight when the line is clear, else along its own A* path (`SteerAlongPath`). Within one step of the cell center it steps onto it and the order ends; an unreachable goal also ends it.
- `Ability`: uses `FCombatUnitStats::PlayerAbilities[AbilityIndex]` right away, without cooldown or windup, through `PlaceArea` (only `CircleAroundSelf` for now). The AI never uses player abilities. Attack indices run over `Attacks` and then `PlayerAbilities` (`GetAttack`, `GetPlayerAbilityAttackIndex`), so events, hits, effects and cues work the same.
- Checksum: a move order is hashed only while active. A fight without commands therefore has the same checksums as before commands existed, and old replays stay valid.
- Scripts: `UCombatCommandScript` (`Commands`; unit IDs = the indices of the setup's valid entries) for `Combat.Simulate ... script=<name>`, `Combat.Batch ... script=<name>` (the same commands on every seed) and the tests.

### Subsystem (`UCombatSubsystem`, game/PIE worlds only)

- `FCombatSimSettings` (`CombatReplay.h`) is every setting that changes a fight, in simulation units: tick rate, max ticks, first-attack delay, retarget interval, lookahead, separation, the threat factor/amount/threshold/ratio/margin, and the taunt range override. `FromProjectSettings(TauntOverride)` converts the project settings (seconds → ticks, half-life or linear → factor/amount). `ApplyTo(Config)` writes them into a config, including the taunt override on every `Attack.Taunt`.
- `BuildSimConfig(World, Seed, Setup, Settings, ...)` takes the world's first `ACombatGrid`, or the fallback grid from settings at origin (0,0,0). It skips setup entries without a definition or with a start cell that is out of bounds or blocked, with a warning, and then applies the settings. `StartFight` uses `GetCurrentSimSettings()` (the project settings plus the panel's taunt range); `StartFightWithSettings` takes them explicitly (replays).
- `IssueCommand` (player input): only for units of `PlayerTeam`, not during a replay or after the fight. It sets `Tick` = current tick + `CommandDelayTicks` (3) and queues the command. This also works while paused (tactical pause); the command then runs 3 ticks after resuming.
- Checkpoints: after every step on a multiple of `ReplayCheckpointInterval` (20), the checksum is recorded. During a replay it is compared, and the first differing tick is remembered.
- `StartFight` replaces any running fight. It spawns one actor per unit: the definition's `ActorClass`, or `ACombatUnitActor`. It keeps the definitions (`UnitDefinitions`) for per-attack presentation settings.
- Projectiles: on `ProjectileSpawned` it spawns the attack definition's `ProjectileActorClass` (or `ACombatProjectileActor`), found through `FCombatAttackStats::SourceIndex`, into `ProjectileActors` (by projectile ID). On `ProjectileEnded` it destroys that actor. Every frame, projectiles are interpolated like units.
- `Tick`:
  - Add the engine DeltaTime × `TimeScale` to the accumulator, unless the fight is paused (`SetPaused`, `SetTimeScale`), and run up to `MaxStepsPerFrame` steps. Pause and speed only change when steps run, so the fight itself is unchanged. After each step, dispatch its events to the actors.
  - Clamp the accumulator to one step, so a backlog is dropped.
  - Push the interpolated state to the actors with alpha = accumulator / FixedDt. The facing is towards the target, otherwise along the velocity.
  - When the fight ends, log and show the result once.
  - `Combat.Debug` (console variable): `1` draws a line from each unit to its target in the team color, and a yellow line to its steer point when that is not the target. `2` also prints team 0's distance map per cell (in cells). The target line's color is the reason: team color = nearest, cyan = visible, orange = threat, magenta = taunt.
  - An `Attack` event without a target (area attack) gives the unit's lunge no direction.
  - Areas:
    - Telegraphs (`GetPendingAreas`) are drawn every frame as a `TelegraphColor` outline, with an inner outline that grows until the area goes off.
    - `AreaAttackFired` flashes the shape for `AreaPulseDuration`. The color comes from the cue's `DebugColor` in the cue table, else `TauntColor` for taunts, else `DefaultAreaColor`. The cue's Niagara system and sound play at the area's center, and the source actor's `OnAreaAttack` Blueprint hook is called.
    - Circles and cones are drawn from the attacker's center, with its radius added for the edge-to-edge shapes.
  - Cues: on a `Hit` of a non-area attack, the cue table entry for its `ImpactCue` plays at the hit unit. `UCombatCueTable` (`DA_CueTable`, set in settings `CueTable`) holds entries of cue tag, Niagara system, sound and debug color; everything is optional.
  - `Combat.ShowRanges` (console variable, also the panel's "Taunt range" button): a circle around every unit with an area attack, with radius `Range` + the unit's radius. An enemy whose edge is inside the circle is hit.
- Replays (`FCombatReplay`, JSON through `FJsonObjectConverter`, in `Saved/Replays/<timestamp>_<setup>_<seed>.json`):
  - `SaveReplay` (only after the fight is over) stores the build version, map, grid checksum (`FCombatGridData::ComputeChecksum`), setup path, seed, the `FCombatSimSettings` used, and the recorded ticks, outcome and final checksum. Format version 2 also stores the command log, the command delay, and the checkpoints (version 1 files still load, with no commands). Version 3 can hold a full copy of the level (`bHasLevel`, `Level`); such a replay does not depend on the level file and skips the grid check.
  - `PlayReplay` starts the fight with the replay's settings and commands (not the current ones) and warns about another build, map or grid. Player input is ignored while it plays.
  - When the fight ends, `ReportResult` compares ticks, checksum and checkpoints and sets `GetReplayVerdict()` ("identical", or "DIFFERENT" with the first differing checkpoint tick).
  - The float settings survive JSON bit for bit (tested).
- Batch (`CombatBatch::Run`, `CombatBatch.h`):
  - It runs N copies of one config with seeds `StartSeed...` and collects, per fight, the seed, outcome, winner, ticks and checksum.
  - It also collects wins per team (in order of first appearance), draws and time limits, and per unit type (definition name) the units, damage dealt, damage taken and survivors. The simulation tracks `FCombatUnit::DamageDealt/DamageTaken` after the taken multiplier; they are not in the checksum.
  - `ToSummary` gives win rates, duration avg/min/max, and averages per unit. `WriteCsv` writes `<base>_fights.csv` and `<base>_units.csv` to `Saved/CombatBatch/`.
  - `RunBatchInWorld` builds the config once from the world's arena; batch fights are exactly the single runs of those seeds (tested).
  - 1000 fights of `DA_Setup_AoE` (8 units) take ~3.4 s headless.
- Console commands: `Start`, `Simulate` and `Batch` take `level=<name>` instead of a setup (for Batch the start seed then follows the count). The panel's setup dropdown lists the setups and `Level: <name>` (refreshed when opened).
- Console commands: `Combat.Start`, `Combat.Simulate` (headless; uses the grid of the current world if there is one, and the subsystem's taunt override if there is a subsystem), `Combat.Stop`, `Combat.Batch <count> [setup] [startseed] [csv]`, `Combat.SaveReplay`, `Combat.Replay <file>`, and the player commands `Combat.Move <unit> <x> <y>` and `Combat.Ability <unit> [index]` (until the unit list in the HUD exists). Simulate and Batch accept `script=<name>`. Setups are found by asset name through the Asset Registry, or by object path.

### Presentation (`ACombatUnitActor`)

- Placeholder look: a body mesh (diameter = 2 × radius, height `BodyHeight`, 10 cm: flat pieces like on a board). It is `RangedBodyMesh` (default the engine cube) for units with a ranged attack and `MeleeBodyMesh` (default the engine cylinder) otherwise; the subsystem passes `bRanged` to `InitUnit`. The body has a dynamic material in the team color (made from `BodyMaterialBase`, default `BasicShapeMaterial`, through the `BodyColorParameter` `Color`; the cylinder's own `DefaultMaterial` has no color parameter), and a small cube "nose" for the facing. No collision.
- `OnAttack` lunges towards the target (sine over `LungeDuration`). `OnHit` flashes white and shows the damage as debug text. `OnDeath` shows "X" and hides the actor. Two screen-space `UWidgetComponent`s (Slate, built in code: `SCombatUnitWidgets`) show more:
  - **Health bar** (`SCombatHealthBar`), `HealthBarOffset` above the body and `HealthBarSize` pixels. The fill goes from green (full) via yellow (half) to red, and the rim has the team color. It uses its own `FProgressBarStyle` (plain white fill, dark background) because the default style tints the fill. Each frame the subsystem calls `SetHealth(HP / MaxHP)`.
  - **Status labels** (`SCombatStatusIcons`) on the body's center: one label per active effect, in effect order.
    - The label and color come from the `StatusIcons` setting (`Effect.Taunt` T magenta, `Effect.Slow` S cyan, `Effect.Rally` R yellow). An unlisted effect shows the first letter of its tag's last part, in white.
    - Stacks above 1 are added as a number (S2).
    - The row is only rebuilt when the labels change.
  Both are visible in release builds; they replace the old debug-text "T". `OnAreaAttack` is only a Blueprint hook (`On Unit Area Attack`); the subsystem draws the area. Each one also calls a Blueprint event (`On Unit Attack/Hit/Death`) for subclasses.

### Control panel (`ACombatHUD`, `SCombatControlPanel`)

- `ACombatGameMode` uses `ACombatHUD`. At BeginPlay the HUD adds a Slate panel at the top left of the game viewport, inside a full-screen box that lets clicks elsewhere through. It shows the mouse cursor and sets input to Game+UI, so the console still works.
- Panel controls (built in code, English labels):
  - Setup dropdown (`UCombatSubsystem::GetAllSetupNames`, default selection from settings), seed field, and a Random seed button.
  - Start / Restart (`StartFight`), Stop, and Pause/Resume.
  - Speed 0.05× / 0.1× / 0.25× / 0.5× / 1× / 2× / 4×.
  - Debug Off / Targets / + Distance map, which sets the `Combat.Debug` console variable.
  - Show "Taunt range", which toggles `Combat.ShowRanges`.
  - Replay: Save, a dropdown of the replay files (refreshed when opened, newest first), and Play. The status line shows "Replay: ..." and the verdict.
  - Batch: 100 / 1000 (seeds start at the seed field; the screen freezes during the run) and a CSV toggle. The summary appears below the status line.
  - Taunt: "Asset" or a slider of 1–10 m in steps of 0.5 m. It sets `UCombatSubsystem::SetTauntRangeOverride`, which `StartFight` applies to every `Attack.Taunt` before the simulation is created, so it takes effect at the next Start and a running fight never changes. `Combat.Simulate` does not use the override; it always uses the asset.
  - A status line with setup, seed, tick and running/paused, or after the fight the outcome and checksum.
- The active speed and debug level are tinted green. Nothing is saved: the panel starts from the defaults each time.

### Player input (`SCombatUnitList`, `ACombatPlayerController`)

- `ACombatHUD` also shows `SCombatUnitList` at the top right. It has one row per unit of `PlayerTeam`, with the name, a small health bar, the status labels and the order text (`GetOrderText`: "Queued: ..." during the command delay, "Moving to (x,y)", "Dead").
  - The name is the definition's `DisplayName` (or the asset name without `DA_`), numbered when a type appears more than once.
  - Dead units stay as grey, disabled rows.
  - The list rebuilds when `GetFightSerial()` changes (start/stop); health and labels update every frame.
- Clicking a row selects the unit, or deselects it if it was already selected. Under the row its actions appear:
  - **Move**: `BeginMoveTargeting`; the button turns orange until the next arena click.
  - One button per player ability (`GetAbilityName`: the ability's `DisplayName`, or the last part of its type), which sends an `Ability` command.
- `ACombatPlayerController` (set by `ACombatGameMode`):
  - It turns the mouse into a point on the grid plane (deproject plus a plane intersection, no collision needed).
  - Left click goes to `HandleArenaClick`. While targeting it sends a `Move` command to the clicked cell; otherwise it selects the nearest own living unit within its radius + 30 cm, or deselects.
  - Right click goes to `HandleArenaCancel` (cancel targeting, else deselect). Esc is not used, because it ends PIE.
  - Clicks on the HUD panels go to the UI.
- Selection is presentation state in the subsystem (`SelectedUnitId`, `bAwaitingMoveTarget`). A selected unit that dies is deselected. Everything reaches the fight only through `IssueCommand`.
- `ACombatUnitActor`:
  - `SetSelected` shows a ring of `SelectionRingSegments` (16) flat engine cubes around the feet (`SelectionRingOffset` from the edge).
  - `SetMoveTarget` shows a flat disc on the target cell and a thin bar from the unit to it while a move order runs.
  - These use the shape material in 1.5× the team color, are placed in world space, and are visible in release builds.

### Presentation (`ACombatProjectileActor`)

- A small engine sphere (`Diameter`) at `FlightHeight` in the team color (`BasicShapeMaterial`), facing the flight direction. No collision, no shadow.

### Data and settings

- `UCombatUnitDefinition`: HP, speed (cm/s), radius, `Attacks`, `PlayerAbilities` (the same kind of entries, converted the same way and kept at their index), and `ActorClass`. `FCombatAttackDefinition` has type, range, cooldown, windup and damage, plus for ranged: `ProjectileSpeed`, `bRequiresLineOfSight` and `ProjectileActorClass`. `ToSimStats` keeps the `Attack.Melee`, `Attack.Ranged`, `Attack.Taunt` and `Attack.AoE` entries. Melee needs a walking line, and ranged needs sight if required. Taunt becomes `CircleAroundSelf` with `AreaRadius` = `Range`. AoE takes `AreaShape`, `AreaRadius`, `ConeAngle` (full angle, stored as the cosine of half of it) and `TelegraphDelay`; it needs sight with `bRequiresLineOfSight` and a walking line without it. Every attack has `ImpactCue`, `bAffectsEnemies` and `bAffectsAllies`; effects have the three multipliers. Attacks also have `ThreatMultiplier` and `Effects` (`FCombatEffectDefinition`: effect tag, duration in seconds, stacking, max stacks, granted and blocked-by tags). Units have innate `Tags`.
- Native tags also include `Status.Taunted`, `Effect.Taunt/Slow/Rally` and `Cue.Fire/Cleave/Rally/Taunt`.
- `UCombatSetup`: entries of (definition, team, start cell).
- Test assets (`/Game/Combat/DA_Krijger`, `DA_Brute`, `DA_Boogschutter` (ranged + dagger), `DA_Doelpop` (stands still, no attacks), `DA_Tank` (melee with threat ×3, and a 6 m taunt with a 6 s cooldown that applies `Effect.Taunt` → `Status.Taunted` for 4 s), `DA_Magier` (a fireball on the target: 1.5 m radius, 0.6 s telegraph, a slow of ×0.5 for 2 s), `DA_Bijlman` (a 100° cone cleave, 1.6 m), `DA_Vaandeldrager` (an allies-only aura of 4 m: ×1.25 damage for 3 s), `DA_CueTable` (debug colors for the four cues), `DA_Script_TauntDemo` (archers fall back at tick 20; the tank shouts at ticks 40 and 100). `DA_Tank` (4 m, 3 s) and `DA_Krijger` (3 m, 2 s) have a player taunt; the script adds it only when a unit has no player abilities, so tuned values are kept, `DA_Setup_Test`, `DA_Setup_Wall`, `DA_Setup_Archer`, `DA_Setup_Mixed`, `DA_Setup_Taunt`, `DA_Setup_AoE`) are created by `Scripts/CreateCombatTestAssets.py`. The script only fills assets it creates, so values tuned in the editor are kept; `FORCE_UPDATE = True` overwrites them.
- `UCombatSettings` (`[/Script/BattleSystem.CombatSettings]` in `DefaultGame.ini`) holds the tick rate, max steps per frame, fight time limit, max first-attack delay, retarget interval, path lookahead, separation strength, threat decay (`ThreatDecayMode` HalfLife/Linear, with `ThreatHalfLife` or `ThreatDecayPerSecond`, converted to the per-tick factor/amount), threat threshold, threat switch ratio, retarget distance margin, auto-start (off: fights start from the control panel), default setup and seed, fallback grid, team colors, status icons, player team, command delay, replay checkpoint interval, the cue table, the area flash duration, and the telegraph, default-area and taunt colors.
- Native tags (`CombatTags`): `Attack.Melee/Ranged/AoE/Taunt`.
- `ACombatGameMode`: players start as spectators. In `StartPlay`, after all actors have begun play, it auto-starts the default setup if enabled (`bAutoStartFight`, off by default).

### Tests

`Private/Combat/Tests/CombatSimulationTests.cpp` (`BattleSystem.Combat.*`) builds its own stats in code: GridData, Determinism (per-step checksums), SeedChangesFight, StrongerTeamWins, SimultaneousHits (two-phase damage → Draw), TimeLimit, LineWalkable, DistanceMap, TargetNearestByWalking, PathAroundWall (never in a blocked cell), Separation, LineOfSight, ArcherWalksAroundWall, ArcherPrefersVisibleTarget, BestAttackPerSituation, ProjectileEndsWhenTargetDies, AStarPath, EffectStacking, ThreatRedirectsTarget, ThreatDecay (half-life and linear), TargetHysteresis, TauntPullsEnemy, AoECircleAtTarget, AoECone, AoETelegraph, AoEAllyAura, EffectModifiers, ReplayRoundTrip, BatchStatistics, LevelFormat, LevelToConfig, ReplayWithLevel, CommandMove, CommandMoveOverridesTaunt, CommandPlayerAbility, CommandRejected, CommandsReplayIdentically (live commands vs the same log known in advance: identical every step).

Performance: `Combat.Simulate` takes ~3.5 ms with `DA_Setup_Test` (5 units, 331 ticks, in Arena-01 with the wall) and with `DA_Setup_Mixed` (6 units). Phase 1 took 0.07 ms; the difference is the distance maps and line checks.
