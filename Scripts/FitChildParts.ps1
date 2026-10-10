# Fits the adult hats and clothing of the Fab packs to the hero skeleton SKEL_Hero (editor must be closed):
#   1. Scripts/ExportAdultParts.py (UE): the parts of the chosen categories, an adult body per pack and the hero to FBX in
#      D:/Unreal/Assets/Blender/Export
#   2. Scripts/Blender/FitToHero.py (Blender): fit and rebind each part, to D:/Unreal/Assets/Blender/Fitted
#   3. Scripts/ImportFittedParts.py (UE): import onto SKEL_Hero in /Game/Characters/Meshes/Child/<category>,
#      with the body zones each part covers
#   4. Scripts/CreateBodyZones.py (UE): zone codes and the masked zone material on the child bodies
#   5. Scripts/CreateColorRegions.py (UE): colour regions and a recolour material instance on every imported part
# -Parts picks the categories (Hats, Outwear, Pants, Facewear, Shoes, Hair, Gloves, Accessories, Costumes; default all),
# -Packs the packs by the start of their name (Creative, Funny, City, City_Teen, ...; default all) and -Names the parts whose
# source name matches a regex (no spaces or commas), e.g. -Parts Outwear,Pants, -Packs City or -Names Costume: the other
# copies are left alone (and keep their thumbnails).
# Each step stops the run when it fails. The log tags are [AdultParts], [FitToHero], [FittedParts], [BodyZones] and
# [ColorRegions].

param([string[]]$Parts = @(), [string[]]$Packs = @(), [string]$Names = "")

$ErrorActionPreference = "Stop"
$UE      = "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$Blender = "C:\Program Files\Blender Foundation\Blender 5.1\blender.exe"
$Root    = Split-Path $PSScriptRoot -Parent
$Project = Join-Path $Root "BattleSystem.uproject"
$Log     = Join-Path $Root "Saved\Logs\BattleSystem.log"
$Export  = "D:/Unreal/Assets/Blender/Export"
$Fitted  = "D:/Unreal/Assets/Blender/Fitted"

# Commandlet mode, or editor mode with a renderer (-WithRenderer): the FBX export needs a rendered mesh (a window opens briefly).
function Invoke-UnrealScript([string]$Script, [string]$Tag, [switch]$WithRenderer, [string]$Arguments = "") {
    $path = "$($Root -replace '\\', '/')/Scripts/$Script"
    if ($WithRenderer) { & $UE $Project -unattended -nosplash -nosound -ExecCmds="py $path $Arguments, QUIT_EDITOR" | Out-Null }
    else { & $UE $Project -run=pythonscript -script="$path $Arguments" -unattended -nullrhi -nosplash | Out-Null }
    $lines = Select-String -Path $Log -Pattern ([regex]::Escape($Tag)) | ForEach-Object { $_.Line }
    $lines | Write-Host
    if (-not ($lines -match "$([regex]::Escape($Tag)) Done")) { throw "$Script failed; see $Log" }
}

if (Get-Process UnrealEditor -ErrorAction SilentlyContinue) { throw "Close the Unreal editor first." }

$exportArguments = @($Parts -split "," | Where-Object { $_ }) + @($Packs -split "," | Where-Object { $_ } | ForEach-Object { "pack=$_" }) +
    @($Names | Where-Object { $_ } | ForEach-Object { "names=$_" })
Invoke-UnrealScript "ExportAdultParts.py" "[AdultParts]" -WithRenderer -Arguments ($exportArguments -join " ")

& $Blender -b --factory-startup --python-exit-code 1 -P "$Root\Scripts\Blender\FitToHero.py" -- $Export $Fitted |
    Where-Object { $_ -match "\[FitToHero\]" } | Write-Host
if ($LASTEXITCODE -ne 0) { throw "FitToHero.py failed" }

Invoke-UnrealScript "ImportFittedParts.py" "[FittedParts]"
Invoke-UnrealScript "CreateBodyZones.py" "[BodyZones]"
Invoke-UnrealScript "CreateColorRegions.py" "[ColorRegions]" -Arguments "fitted"
# The editor's own exit code says nothing about the scripts; their "Done" lines were checked above.
exit 0
