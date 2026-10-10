# Asset pipeline

Headless editor-Python scripts that create or update binary assets. All of them need the editor closed. Most work on local
content (gitignored folders, see `CLAUDE.md`), so they only run where that content exists. Each script logs with its own
tag; grep `Saved/Logs/BattleSystem.log` for it (`[CombatCues]`, `[PieceCatalog]`, `[WallVariants]`, `[CombatAnimSet]`,
`[AdultParts]`, `[FitToHero]`, `[FittedParts]`, `[BodyZones]`, `[ColorRegions]`, `[DesignGhost]`, `[MixamoImport]`).

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

# Fit the hats, tops, pants, facewear, shoes, hair, gloves, accessories and picked costumes of the Creative/Funny Characters packs and the
# City pack (per body type and gender) onto SKEL_Hero in /Game/Characters/Meshes/Child/<category>
# (categories by name, see ExportAdultParts.py; -Parts picks them and -Packs the packs, default all; the rest is left alone): UE exports
# FBX, Blender 5.1 scales, moves and rebinds them (Scripts/Blender/FitToHero.py), UE imports them (local content;
# an editor window opens briefly, because UE's FBX export of skeletal meshes needs a renderer).
# Then Scripts/CreateBodyZones.py writes the hide zones into the child bodies (backup in D:/Unreal/Backups on the first run).
# Headless saves have no thumbnails: afterwards save the hats once from the editor (the line is in ImportFittedParts.py's docstring).
# -Names <regex> takes only the parts whose source name matches (no spaces or commas), e.g. the Creative costumes:
#   -Parts Costumes,Hats,Accessories -Packs Creative -Names Costume
& "D:\Unreal\UnrealProjects\BattleSystem\Scripts\FitChildParts.ps1" -Parts Outwear,Pants -Packs City

# Colour regions and recolour material instances on all child parts (FitChildParts.ps1 does it for what it imports)
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -run=pythonscript -script="D:/Unreal/UnrealProjects/BattleSystem/Scripts/CreateColorRegions.py" -unattended -nullrhi -nosplash

# Create the child animation set /Game/Characters/Animations/DA_AnimSet_Child (local content)
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$P" -run=pythonscript -script="D:/Unreal/UnrealProjects/BattleSystem/Scripts/CreateCharacterAnimSet.py" -unattended -nullrhi -nosplash
```

`Scripts/RestructureCatalog.py` was a one-off migration (catalog folders to `Group/Sub`, piece ids in `Levels/*.json`) and has already run; don't run it again.
