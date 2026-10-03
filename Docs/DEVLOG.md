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
