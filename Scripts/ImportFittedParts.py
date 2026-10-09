"""Imports the parts fitted by Scripts/Blender/FitToHero.py onto the hero skeleton SKEL_Hero.

Step 3 of Scripts/FitChildHats.ps1; run headless with the editor closed:
    UnrealEditor-Cmd.exe BattleSystem.uproject -run=pythonscript -script="<abs path>/Scripts/ImportFittedParts.py" -unattended -nullrhi -nosplash

Reads FITTED_DIR/fitted.json. Each part replaces the asset at its target path (soft references to it, such as a look in
DT_Units, keep working) and gets the materials of its source asset back by slot index, so no materials are imported.
A headless run (no renderer) saves the parts without thumbnails; to get them, save them once from the running editor
(Output Log, Python):
    import unreal; [unreal.EditorAssetLibrary.save_asset(a, only_if_is_dirty=False) for a in unreal.EditorAssetLibrary.list_assets('/Game/Characters/Meshes/Child/Hats', recursive=False)]
"""

import json

import unreal

LOG_TAG = "[FittedParts]"
FITTED_DIR = "D:/Unreal/Assets/Blender/Fitted"
HERO_MESH = "/Game/Characters/Meshes/SKM_Hero.SKM_Hero"


def log(message):
    unreal.log(f"{LOG_TAG} {message}")


def import_part(fbx_path, folder, name, skeleton):
    options = unreal.FbxImportUI()
    options.set_editor_property("import_mesh", True)
    options.set_editor_property("import_as_skeletal", True)
    options.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_SKELETAL_MESH)
    options.set_editor_property("skeleton", skeleton)
    options.set_editor_property("import_animations", False)
    options.set_editor_property("import_materials", False)
    options.set_editor_property("import_textures", False)
    options.set_editor_property("create_physics_asset", False)
    mesh_data = options.get_editor_property("skeletal_mesh_import_data")
    mesh_data.set_editor_property("import_morph_targets", False)
    mesh_data.set_editor_property("update_skeleton_reference_pose", False)
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", fbx_path)
    task.set_editor_property("destination_path", folder)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    task.set_editor_property("options", options)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    mesh = unreal.load_asset(f"{folder}/{name}")
    if not isinstance(mesh, unreal.SkeletalMesh):
        raise RuntimeError(f"{LOG_TAG} Could not import {fbx_path} as a skeletal mesh")
    return mesh


def copy_materials(source, target):
    source_slots = source.get_editor_property("materials")
    target_slots = target.get_editor_property("materials")
    if len(source_slots) != len(target_slots):
        raise RuntimeError(f"{LOG_TAG} {target.get_path_name()}: {len(target_slots)} material slots, "
                           f"the source has {len(source_slots)}")
    # The whole array at once: an element read from it in Python is a copy, so setting its properties changes nothing.
    target.set_editor_property("materials", source_slots)


def delete_stale(targets, skeleton):
    """Deletes the targets that are not skeletal meshes on the skeleton, then collects garbage.

    A copy on another skeleton cannot be reimported onto SKEL_Hero, and an import into a deleted asset that is still in memory
    reimports it (keeping its old skeleton), so the deleting happens first, with no references left for the collection."""
    lib = unreal.EditorAssetLibrary
    stale = [t for t in targets if lib.does_asset_exist(t) and not on_skeleton(unreal.load_asset(t), skeleton)]
    for target in stale:
        if not lib.delete_asset(target):
            raise RuntimeError(f"{LOG_TAG} Could not delete {target}")
    unreal.SystemLibrary.collect_garbage()
    if stale:
        log(f"Deleted {len(stale)} copies on another skeleton")


def on_skeleton(asset, skeleton):
    return isinstance(asset, unreal.SkeletalMesh) and asset.get_editor_property("skeleton") == skeleton


def main():
    # The classic FBX importer, so FbxImportUI's settings apply (UE 5.8 imports FBX through Interchange by default).
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")
    skeleton = unreal.load_asset(HERO_MESH).get_editor_property("skeleton")
    with open(f"{FITTED_DIR}/fitted.json", encoding="utf-8") as f:
        parts = json.load(f)["parts"]

    delete_stale([part["target"] for part in parts], skeleton)
    for part in parts:
        folder, name = part["target"].rsplit("/", 1)
        mesh = import_part(f"{FITTED_DIR}/{part['fbx']}", folder, name, skeleton)
        if not on_skeleton(mesh, skeleton):
            raise RuntimeError(f"{LOG_TAG} {part['target']} did not land on {skeleton.get_path_name()}")
        copy_materials(unreal.load_asset(part["source"]), mesh)
        if not unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False):
            raise RuntimeError(f"{LOG_TAG} Could not save {part['target']}")
    log(f"{len(parts)} parts on {skeleton.get_path_name()}")
    log("Done")


main()
