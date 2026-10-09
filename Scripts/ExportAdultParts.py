"""Exports the adult hats and clothing of the Fab packs, an adult body per pack and the hero mesh to FBX, for Blender.

Step 1 of Scripts/FitChildParts.ps1; run with the editor closed, in editor mode WITH a renderer (no -run=pythonscript, no
-nullrhi): the FBX exporter of skeletal meshes needs a rendered mesh and asserts on "MeshObject" without one. A window opens
briefly. The arguments are the categories to export (CATEGORIES; none = all):
    UnrealEditor-Cmd.exe BattleSystem.uproject -unattended -nosplash -nosound -ExecCmds="py <abs path>/Scripts/ExportAdultParts.py Outwear Pants, QUIT_EDITOR"

A part's category comes from its name (category_of): SK_Hat_* are hats (copies SK_Hat_<Pack>_*), names with Outwear,
Outerwear or Outfit tops, names with Pants or Shorts pants (copies SK_<Pack>_*). Of a pack with colour variants
(COLOR_VARIANTS) only one colour is taken.

Writes EXPORT_DIR/<pack>/*.fbx, EXPORT_DIR/SKM_Hero.fbx and EXPORT_DIR/manifest.json, which Scripts/Blender/FitToHero.py
reads. Only LOD 0 is exported (Blender's FBX import does not keep LOD groups). The FBX files are local, outside the repo.
"""

import json
import os
import re
import shutil
import sys

import unreal

LOG_TAG = "[AdultParts]"
EXPORT_DIR = "D:/Unreal/Assets/Blender/Export"
# Category -> (folder of the copies, kind for FitToHero.py: rigid hats or skinned clothing)
CATEGORIES = {
    "Hats": ("/Game/Characters/Meshes/Child/Hats", "hat"),
    "Outwear": ("/Game/Characters/Meshes/Child/Outwear", "clothing"),
    "Pants": ("/Game/Characters/Meshes/Child/Pants", "clothing"),
}
HERO_MESH = "/Game/Characters/Meshes/SKM_Hero.SKM_Hero"

# (pack folder, name in the copy, an adult body of the pack to measure its head, scale multiplier)
PACKS = [
    ("/Game/ZZ_FAB/Creative_Characters/Skeleton_Meshes", "Creative", "/Game/ZZ_FAB/Creative_Characters/Skeleton_Meshes/SK_Body_001.SK_Body_001", 1.0),
    ("/Game/ZZ_FAB/Funny_Characters/Meshes", "Funny", "/Game/ZZ_FAB/Funny_Characters/Meshes/SK_Body_Blue_001.SK_Body_Blue_001", 1.0),
]
# Packs whose clothing comes in colours (SK_<Type>_<Colour>_<Number>) -> the one colour that is fitted.
COLOR_VARIANTS = {"Funny": "White"}


def log(message):
    unreal.log(f"{LOG_TAG} {message}")


def export_fbx(mesh, path):
    options = unreal.FbxExportOption()
    options.set_editor_property("ascii", False)
    options.set_editor_property("level_of_detail", False)
    options.set_editor_property("export_source_mesh", True)
    options.set_editor_property("collision", False)
    options.set_editor_property("export_morph_targets", False)
    task = unreal.AssetExportTask()
    task.set_editor_property("object", mesh)
    task.set_editor_property("filename", path)
    task.set_editor_property("exporter", unreal.SkeletalMeshExporterFBX())
    task.set_editor_property("options", options)
    task.set_editor_property("automated", True)
    task.set_editor_property("prompt", False)
    task.set_editor_property("replace_identical", True)
    if not unreal.Exporter.run_asset_export_task(task) or not os.path.isfile(path):
        raise RuntimeError(f"{LOG_TAG} Could not export {mesh.get_path_name()} to {path}")


def category_of(name):
    if name.startswith("SK_Hat_"):
        return "Hats"
    if re.search(r"Outwear|Outerwear|Outfit", name):
        return "Outwear"
    if re.search(r"Pants|Shorts", name):
        return "Pants"
    return None


def wanted_colour(name, pack_name):
    """False for a colour variant other than the pack's chosen colour (COLOR_VARIANTS)."""
    colour = COLOR_VARIANTS.get(pack_name)
    match = re.match(r"^SK_[A-Za-z]+_(.+)_\d+$", name)
    return colour is None or name.startswith("SK_Hat_") or not match or match.group(1) == colour


def main():
    categories = [a for a in sys.argv[1:] if a] or list(CATEGORIES)
    unknown = [c for c in categories if c not in CATEGORIES]
    if unknown:
        raise RuntimeError(f"{LOG_TAG} Unknown categories {unknown}; known: {list(CATEGORIES)}")
    log(f"Categories: {', '.join(categories)}")
    # Every run starts from an empty folder, so parts removed from a pack do not linger.
    if os.path.isdir(EXPORT_DIR):
        shutil.rmtree(EXPORT_DIR)
    os.makedirs(EXPORT_DIR)

    export_fbx(unreal.load_asset(HERO_MESH), f"{EXPORT_DIR}/SKM_Hero.fbx")
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    packs = []
    count = 0
    for folder, pack_name, adult_body, tune in PACKS:
        os.makedirs(f"{EXPORT_DIR}/{pack_name}")
        export_fbx(unreal.load_asset(adult_body), f"{EXPORT_DIR}/{pack_name}/_Body.fbx")
        parts = []
        for data in sorted(registry.get_assets_by_path(folder, recursive=False), key=lambda d: str(d.asset_name)):
            name = str(data.asset_name)
            category = category_of(name)
            if category not in categories or not wanted_colour(name, pack_name):
                continue
            target_folder, kind = CATEGORIES[category]
            # SK_Hat_001 -> SK_Hat_Creative_001, SK_Outwear_001 -> SK_Creative_Outwear_001
            target_name = name.replace("SK_Hat_", f"SK_Hat_{pack_name}_", 1) if kind == "hat" else f"SK_{pack_name}_{name[3:]}"
            export_fbx(unreal.load_asset(f"{folder}/{name}"), f"{EXPORT_DIR}/{pack_name}/{name}.fbx")
            parts.append({"kind": kind, "name": target_name, "fbx": f"{pack_name}/{name}.fbx",
                          "source": f"{folder}/{name}", "target": f"{target_folder}/{target_name}"})
        packs.append({"name": pack_name, "body": f"{pack_name}/_Body.fbx", "tune": tune, "parts": parts})
        count += len(parts)
        log(f"{pack_name}: {len(parts)} parts")

    with open(f"{EXPORT_DIR}/manifest.json", "w", encoding="utf-8") as f:
        json.dump({"hero": "SKM_Hero.fbx", "packs": packs}, f, indent="\t")
    log(f"{count} parts in {EXPORT_DIR}")
    log("Done")


main()
