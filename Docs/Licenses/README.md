# Third-party licenses

One row per third-party asset (packs, fonts, ...). Fab packs live locally in `Content/ZZ_FAB/` and are not in the repo.

| Asset | Source | Location | License |
|---|---|---|---|
| Mixamo animations (Death, HitReact, Jazz_Dancing, Kick_To_The_Groin, Punch01, Push, Throw_Anim; Idle: Backwards_Idle, Breathing_Idle, Catwalk_Idle_To_Twist_R, Drunk_Idle_Variation, Happy_Idle, Idle_Aiming, Injured_Idle, Injured_Stumble_Idle, Offensive_Idle, Relaxed_Idle, Rifle_Idle, Rifle_Idle_1, Sad_Idle, Slap_Idle, Standing_Torch_Idle_01, Tension_Idle, Victory_Idle, Warrior_Idle; Run: Fast_Run, Jogging_Run, Slow_Run; Walk: Aggressive_Walk, Confident_Walk, Drunk_Walk, Female_Walk, Frivolious_Walk, Neutral_Walk, Relaxed_Walk) and the Mixamo character used as retarget source (`SKM_Mixamo`, `SKEL_Mixamo`) | [Mixamo](https://www.mixamo.com) (Adobe) | Local only: `Content/Characters/Animations/Mixamo` (originals, by subfolder) and `Content/Characters/Animations/Heroes/<Combat|Idle|Run|Walk>` (retargeted to `SKEL_Hero` by `Scripts/ImportMixamoAnimations.py`, montages `AM_*`) | Adobe Mixamo terms: free with an Adobe account, royalty-free for personal and commercial projects; the characters and animations may not be redistributed as standalone files. |
