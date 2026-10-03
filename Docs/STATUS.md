# Status

_Last updated: 2026-10-03_

## Current state

- Combat phases 1–3 are done: melee and ranged units (with homing projectiles and line of sight) find each other by walking distance and fight deterministically. The tests pass (16/16, `BattleSystem.Combat.*`). Ranged units are cubes, melee units cylinders.
- Deterministic in `Arena-01` (wall at x = 6), seed 42: `DA_Setup_Test` `0x8C92F44D`, `DA_Setup_Archer` `0xB4A9FBB5`, `DA_Setup_Mixed` `0x5B05E540` (each twice the same).
- In-game control panel (setup, seed, start/stop, pause, speed, debug); auto-start is off.
- Repo on GitHub (`main`), with Git LFS for binary assets. `Content/ZZ_FAB/` (Fab packs) is local only.

## Open work

- Phase 4 of the design: aggro (threat, taunt, hysteresis) and the effect model.
- Balance: the Brutes win `DA_Setup_Test`; `DA_Setup_Mixed` depends on the seed (seed 42: team 0, seed 7: team 1).
- Performance: ~3.5 ms per 5–6 unit fight headless; look at it before `Combat.Batch` in phase 5.
- List the Fab packs used in `Content/ZZ_FAB/` in `Docs/Licenses/README.md`.
