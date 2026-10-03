# Status

_Last updated: 2026-10-03_

## Current state

- Combat phase 1 is done. It covers the grid, obstacles, the deterministic simulation (melee with windup), the subsystem, the unit actors in team colors, the game mode, settings, console commands, test assets (`/Game/Combat`) and the arena `Content/Maps/Arena-01`, which is the game and editor startup map.
- The tests pass (6/6, `BattleSystem.Combat.*`). `Combat.Simulate 42` gives the same checksum twice (`0x897D1995`).
- Repo on GitHub (`main`), with Git LFS for binary assets. `Content/ZZ_FAB/` (Fab packs) is local only.

## Open work

- Phase 2 of the design: pathfinding (distance maps), path smoothing, separation steering, and "nearest by walking distance".
- Balance: with the current test values the Brutes win (seeds 7 and 42).
- List the Fab packs used in `Content/ZZ_FAB/` in `Docs/Licenses/README.md`.
