# Status

_Last updated: 2026-10-03_

## Current state

- Combat phases 1–4 are done. Phase 4 adds threat (half-life or linear decay), taunt, target hysteresis, own A* routes and the effect model. The tests pass (22/22, `BattleSystem.Combat.*`).
- Deterministic in `Arena-01`, seed 42: `DA_Setup_Taunt` `0x7CE33AAB`, `DA_Setup_Mixed` `0x32F7C942` (each twice the same); `DA_Setup_Test` `0x36198AA1`.
- In-game control panel (setup, seed, start/stop, pause, speed, debug, taunt range slider, taunt range circle); auto-start is off. Taunted units show a magenta T, and a taunt flashes a circle. Ranged units are cubes, melee units cylinders.
- Repo on GitHub (`main`), with Git LFS for binary assets. `Content/ZZ_FAB/` (Fab packs) is local only.

## Open work

- Phase 5 of the design: AoE attacks (with telegraphs), cue tags, replays, and `Combat.Batch` balance runs.
- Balance: the Brutes win `DA_Setup_Test` and `DA_Setup_Taunt`; `DA_Setup_Mixed` depends on the seed.
- Performance: ~2.5–3 ms per 5–6 unit fight headless; look at it before `Combat.Batch` in phase 5.
- List the Fab packs used in `Content/ZZ_FAB/` in `Docs/Licenses/README.md`.
