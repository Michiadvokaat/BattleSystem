"""Makes taller or lower copies of catalog meshes (for example a 3 m version of a 2 m wall).

Run headless in editor mode (the static mesh editor functions need an editor; the -run=pythonscript commandlet has
none), with the editor closed:
    UnrealEditor-Cmd.exe BattleSystem.uproject -unattended -nullrhi -nosplash -nosound -ExecCmds="py <abs path>/Scripts/MakeWallVariants.py, QUIT_EDITOR"

Each variant is a duplicate of the source mesh, next to it in the catalog folder, named "<mesh>_<height>cm"
(height in cm), with a build scale in Z so that its bounds are that tall (X and Y stay; the texture stretches with
it). An existing variant of the right height is kept; one of another height (for example a copy whose scaling
failed) is scaled to it. Run
CreatePieceCatalog.py afterwards to add the new meshes to the piece catalog.
The meshes are local content (/Game/Environment is not in git).
"""

import unreal

LOG_TAG = "[WallVariants]"
CATALOG_PATH = "/Game/Environment/Catalogus"
# Source mesh (relative to CATALOG_PATH) -> heights in cm of the variants to make.
VARIANTS = {
    "Walls/SM_Walls_007": [300],
}


def log(message):
    unreal.log(f"{LOG_TAG} {message}")


def mesh_height(mesh):
    box = mesh.get_bounding_box()
    return box.max.z - box.min.z


def make_variant(source_path, height):
    source = unreal.load_asset(source_path)
    if not isinstance(source, unreal.StaticMesh):
        unreal.log_warning(f"{LOG_TAG} {source_path} is not a static mesh, skipped")
        return
    variant_path = f"{source_path}_{height}cm"
    if unreal.EditorAssetLibrary.does_asset_exist(variant_path):
        variant = unreal.load_asset(variant_path)
        if abs(mesh_height(variant) - height) < 1.0:
            log(f"Keeping {variant_path}")
            return
        log(f"{variant_path} is {mesh_height(variant):.0f} cm tall, scaling it to {height} cm")
    else:
        variant = unreal.EditorAssetLibrary.duplicate_asset(source_path, variant_path)
        if not variant:
            raise RuntimeError(f"{LOG_TAG} Could not duplicate {source_path}")

    current_height = mesh_height(variant)
    if current_height <= 0.0:
        unreal.log_warning(f"{LOG_TAG} {variant_path} has no height, skipped")
        return

    # The build scale multiplies the existing one, on every LOD, so the result is exactly `height` tall.
    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    if not subsystem or subsystem.get_lod_count(variant) < 1:
        raise RuntimeError(f"{LOG_TAG} No static mesh editor: run this script in editor mode (see the top of the file)")
    factor = height / current_height
    for lod in range(subsystem.get_lod_count(variant)):
        settings = subsystem.get_lod_build_settings(variant, lod)
        scale = settings.get_editor_property("build_scale3d")
        settings.set_editor_property("build_scale3d", unreal.Vector(scale.x, scale.y, scale.z * factor))
        subsystem.set_lod_build_settings(variant, lod, settings)

    if not unreal.EditorAssetLibrary.save_loaded_asset(variant, only_if_is_dirty=False):
        raise RuntimeError(f"{LOG_TAG} Could not save {variant_path}")
    box = variant.get_bounding_box()
    size = box.max - box.min
    log(f"Saved {variant_path}: {size.x:.0f} x {size.y:.0f} x {size.z:.0f} cm")


def main():
    for source, heights in VARIANTS.items():
        for height in heights:
            make_variant(f"{CATALOG_PATH}/{source}", height)
    log("Done")


main()
