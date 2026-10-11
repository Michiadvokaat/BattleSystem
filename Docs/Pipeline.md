# Asset pipeline

Headless editor-Python scripts that create or update binary assets. All of them need the editor closed. Most work on local
content (gitignored folders, see `CLAUDE.md`), so they only run where that content exists. Each script logs with its own
tag; grep `Saved/Logs/BattleSystem.log` for it (`[CombatCues]`, `[PieceCatalog]`, `[WallVariants]`, `[CombatAnimSet]`,
`[DesignGhost]`, `[MixamoImport]`; the plugin's fitting pipeline: `[AdultParts]`, `[FitToHero]`, `[FittedParts]`,
`[BodyZones]`, `[ColorRegions]`, `[LocalizeDeps]`).

The unit and skill tables (`ImportCombatData.py`) are in `CLAUDE.md`, because they are part of the normal code workflow.

```powershell
$UE = "C:\Program Files\Epic Games\UE_5.8"
$P  = "D:\Unreal\UnrealProjects\BattleSystem\BattleSystem.uproject"

# (Re)create the cue table /Game/Data/DA_CueTable
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -run=pythonscript -script="D:/Unreal/UnrealProjects/BattleSystem/Scripts/CreateCueTable.py" -unattended -nullrhi -nosplash

# Create or update the LevelDesigner piece catalog from /Game/Environment/Catalogus (local content)
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -run=pythonscript -script="D:/Unreal/UnrealProjects/BattleSystem/Scripts/CreatePieceCatalog.py" -unattended -nullrhi -nosplash

# Make taller/lower copies of catalog meshes (editor mode, not -run=pythonscript), then rerun CreatePieceCatalog.py
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -unattended -nullrhi -nosplash -nosound -ExecCmds="py D:/Unreal/UnrealProjects/BattleSystem/Scripts/MakeWallVariants.py, QUIT_EDITOR"

# (Re)create the LevelDesigner unit ghost material /Game/Materials/M_DesignGhost
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -run=pythonscript -script="D:/Unreal/UnrealProjects/BattleSystem/Scripts/CreateDesignGhostMaterial.py" -unattended -nullrhi -nosplash

# Import Mixamo FBX animations from D:/Unreal/Assets/Animations (subfolders mirrored) and retarget them to SKEL_Hero
# in /Game/Characters/Animations/Heroes; existing ones are skipped (local content)
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -run=pythonscript -script="D:/Unreal/UnrealProjects/BattleSystem/Scripts/ImportMixamoAnimations.py" -unattended -nullrhi -nosplash

# Fitting clothing parts is the ModularCharacter3D plugin's pipeline (Plugins/ModularCharacter3D/Scripts; its README.md):
# the hats, tops, pants, facewear, shoes, hair, gloves, accessories and picked costumes of the Creative/Funny Characters packs
# and the City pack (per body type and gender) onto the bodies of a target: -Target Child (default) onto SKM_Hero (SKEL_Hero)
# in /ModularCharacter3D/Child/<category>, -Target Adult onto SKM_Adult_Male and SKM_Adult_Female (SKEL_Adult_Skeleton) in
# /ModularCharacter3D/Adult/<category> with the suffix _M / _F (every part on both)
# (categories by name, see ExportAdultParts.py; -Parts picks them and -Packs the packs, default all; the rest is left alone): UE exports
# FBX, Blender 5.1 scales, moves and rebinds them (Blender/FitToHero.py), UE imports them (an editor window opens briefly,
# because UE's FBX export of skeletal meshes needs a renderer), CreateBodyZones.py writes the hide zones into the target's
# bodies (a backup of each body the first time), CreateColorRegions.py gives the parts colour regions and
# LocalizeDependencies.py copies the pack materials they use into /ModularCharacter3D/Source.
# Paths and programs: the plugin's Scripts/Pipeline.json (this project uses the defaults: packs in /Game/ZZ_FAB).
# Headless saves have no thumbnails: afterwards save the parts once from the editor (the line is in ImportFittedParts.py's docstring).
# -Names <regex> takes only the parts whose source name matches (no spaces or commas), e.g. the Creative costumes:
#   -Parts Costumes,Hats,Accessories -Packs Creative -Names Costume
& "D:\Unreal\UnrealProjects\BattleSystem\Plugins\ModularCharacter3D\Scripts\FitParts.ps1" -Parts Outwear,Pants -Packs City
& "D:\Unreal\UnrealProjects\BattleSystem\Plugins\ModularCharacter3D\Scripts\FitParts.ps1" -Target Adult

# Colour regions and recolour material instances on all child and adult parts (FitParts.ps1 does it for what it imports)
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -run=pythonscript -script="D:/Unreal/UnrealProjects/BattleSystem/Plugins/ModularCharacter3D/Scripts/CreateColorRegions.py" -unattended -nullrhi -nosplash

# Create the child animation set /Game/Characters/Animations/DA_AnimSet_Child (local content)
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -run=pythonscript -script="D:/Unreal/UnrealProjects/BattleSystem/Scripts/CreateCharacterAnimSet.py" -unattended -nullrhi -nosplash
```

`Scripts/RestructureCatalog.py` was a one-off migration (catalog folders to `Group/Sub`, piece ids in `Levels/*.json`) and has already run; don't run it again.
