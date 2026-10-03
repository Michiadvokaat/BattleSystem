# Status

_Last updated: 2026-10-03_

## Current state

- Combat phase 1 is done. Phase 2 (pathfinding, separation, nearest by walking distance) is built and its tests pass (11/11, `BattleSystem.Combat.*`).
- `Combat.Simulate` is deterministic: seed 42 with `DA_Setup_Test` gives `0xACEE20E1` twice, and with `DA_Setup_Wall` it gives `0x3272F721` twice. These values were measured with the old obstacles in Arena-01; they change once the wall is placed.
- Repo on GitHub (`main`), with Git LFS for binary assets. `Content/ZZ_FAB/` (Fab packs) is local only.

## Open work

- The user puts the wall in `Arena-01`: Location (650, 450, 100), Box Extent (50, 450, 100), and removes the old obstacles. Then check visually with `Combat.Debug 1` and `Combat.Start 42 DA_Setup_Wall`. After that, phase 2 is done.
- Balance: with the current test values the Brutes win.
- Performance (~2.4 ms per 5-unit fight) before `Combat.Batch` in phase 5.
- List the Fab packs used in `Content/ZZ_FAB/` in `Docs/Licenses/README.md`.
