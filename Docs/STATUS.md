# Status

_Last updated: 2026-10-03_

## Current state

- Combat phases 1 and 2 are done. Units take the nearest enemy by walking distance, follow smoothed routes around obstacles, push each other apart, and fight melee with windup. The tests pass (11/11, `BattleSystem.Combat.*`).
- `Arena-01` has a wall at x = 6, y = 0..8 (gap at y = 9..11). `DA_Setup_Test` is the default fight and `DA_Setup_Wall` is the walking-distance check (`Combat.Start 42 DA_Setup_Wall`, with `Combat.Debug 1`).
- Repo on GitHub (`main`), with Git LFS for binary assets. `Content/ZZ_FAB/` (Fab packs) is local only.

## Open work

- Phase 3 of the design: ranged attacks, projectiles and line of sight.
- Balance: with the current test values the Brutes win.
- Performance: a 5-unit fight takes ~2.4 ms headless; look at it before `Combat.Batch` in phase 5.
- List the Fab packs used in `Content/ZZ_FAB/` in `Docs/Licenses/README.md`.
