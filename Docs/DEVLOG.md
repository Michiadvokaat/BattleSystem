# Devlog

Dated history: decisions, bugs, rejected approaches. Newest entries at the bottom.

## 2026-10-03

- Created the UE 5.8 C++ project `BattleSystem` and the combat system design (`Docs/Ontwerp-Gevecht.md`): a deterministic, headless-capable fixed-step simulation on a custom grid. GAS, NavMesh and physics are rejected for combat logic (see the design's "Besluiten" table).
- Set up the git repo with Git LFS for binary assets. Third-party Fab packs live in `Content/ZZ_FAB/` and are gitignored to keep the repo small.
- Adopted the working agreements (`Docs/Werkafspraken.md`), and added `STATUS.md`, `Architecture.md`, `DEVLOG.md` and `Licenses/README.md`.
