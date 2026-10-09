"""Exports the adult hats (SK_Hat_*) of the Fab packs, an adult body per pack and the hero mesh to FBX, for Blender.

Step 1 of Scripts/FitChildHats.ps1; run with the editor closed, in editor mode WITH a renderer (no -run=pythonscript, no
-nullrhi): the FBX exporter of skeletal meshes needs a rendered mesh and asserts on "MeshObject" without one. A window opens
briefly:
    UnrealEditor-Cmd.exe BattleSystem.uproject -unattended -nosplash -nosound -ExecCmds="py <abs path>/Scripts/ExportAdultParts.py, QUIT_EDITOR"

Writes EXPORT_DIR/<pack>/*.fbx, EXPORT_DIR/SKM_Hero.fbx and EXPORT_DIR/manifest.json, which Scripts/Blender/FitToHero.py
reads. Only LOD 0 is exported (Blender's FBX import does not keep LOD groups). The FBX files are local, outside the repo.
"""

import json
import os
import shutil

import unreal

LOG_TAG = "[AdultParts]"
EXPORT_DIR = "D:/Unreal/Assets/Blender/Export"
TARGET_PATH = "/Game/Characters/Meshes/Child/Hats"
HERO_MESH = "/Game/Characters/Meshes/SKM_Hero.SKM_Hero"

# (pack folder, name in the copy, an adult body of the pack to measure its head, scale multiplier)
PACKS = [
    ("/Game/ZZ_FAB/Creative_Characters/Skeleton_Meshes", "Creative", "/Game/ZZ_FAB/Creative_Characters/Skeleton_Meshes/SK_Body_001.SK_Body_001", 1.0),
    ("/Game/ZZ_FAB/Funny_Characters/Meshes", "Funny", "/Game/ZZ_FAB/Funny_Characters/Meshes/SK_Body_Blue_001.SK_Body_Blue_001", 1.0),
]


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


def main():
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
            if not name.startswith("SK_Hat_"):
                continue
            target_name = name.replace("SK_Hat_", f"SK_Hat_{pack_name}_", 1)
            export_fbx(unreal.load_asset(f"{folder}/{name}"), f"{EXPORT_DIR}/{pack_name}/{name}.fbx")
            parts.append({"kind": "hat", "name": target_name, "fbx": f"{pack_name}/{name}.fbx",
                          "source": f"{folder}/{name}", "target": f"{TARGET_PATH}/{target_name}"})
        packs.append({"name": pack_name, "body": f"{pack_name}/_Body.fbx", "tune": tune, "parts": parts})
        count += len(parts)
        log(f"{pack_name}: {len(parts)} parts")

    with open(f"{EXPORT_DIR}/manifest.json", "w", encoding="utf-8") as f:
        json.dump({"hero": "SKM_Hero.fbx", "packs": packs}, f, indent="\t")
    log(f"{count} parts in {EXPORT_DIR}")
    log("Done")


main()
