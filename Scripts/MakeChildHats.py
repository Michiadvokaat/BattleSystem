"""Copies the adult hats (SK_Hat_*) of the Fab packs to /Game/Characters/Meshes/Child/Hats and fits them to the child head.

Run headless with the editor closed:
    UnrealEditor-Cmd.exe BattleSystem.uproject -run=pythonscript -script="<abs path>/Scripts/MakeChildHats.py" -unattended -nullrhi

The hats keep their own (adult) skeleton: in a swappable look slot they follow the child's Head bone by name (Leader Pose),
relative to the adult Head bone. So every copy is scaled around the adult Head bone, by the ratio of the child's head to the
pack's adult head (width and depth of the vertices above the Neck bone), and then moved so the hat sits as high and as far
forward on the child's head as on the adult one. All LODs; skin weights, UVs and materials stay.

Every run starts again from a fresh copy (an existing copy is replaced), so the fit never adds up. The packs are local
content, and so are the copies. TUNE_SCALE (per pack) multiplies the measured scale, for tuning by eye.
"""

import unreal

LOG_TAG = "[ChildHats]"
TARGET_PATH = "/Game/Characters/Meshes/Child/Hats"
CHILD_BODY = "/Game/Characters/Meshes/SKM_Hero.SKM_Hero"

# (pack folder, name in the copy, an adult body of the pack to measure its head, scale multiplier)
PACKS = [
    ("/Game/ZZ_FAB/Creative_Characters/Skeleton_Meshes", "Creative", "/Game/ZZ_FAB/Creative_Characters/Skeleton_Meshes/SK_Body_001.SK_Body_001", 1.0),
    ("/Game/ZZ_FAB/Funny_Characters/Meshes", "Funny", "/Game/ZZ_FAB/Funny_Characters/Meshes/SK_Body_Blue_001.SK_Body_Blue_001", 1.0),
]
HEAD_BONE = "Head"
NECK_BONE = "Neck"


def log(message):
    unreal.log(f"{LOG_TAG} {message}")


def bone_position(skeleton, bone):
    pose = unreal.AnimPoseExtensions.get_reference_pose(skeleton)
    return unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD).translation


def read_lod(mesh, lod):
    read = unreal.GeometryScriptMeshReadLOD()
    read.set_editor_property("lod_index", lod)
    dyn, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_from_skeletal_mesh(
        mesh, unreal.DynamicMesh(), unreal.GeometryScriptCopyMeshFromAssetOptions(), read)
    if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
        raise RuntimeError(f"{LOG_TAG} Could not read LOD {lod} of {mesh.get_path_name()}")
    return dyn


def vertex_positions(dyn):
    result = unreal.GeometryScript_MeshQueries.get_all_vertex_positions(dyn, True)
    vectors = next(r for r in result if isinstance(r, unreal.GeometryScriptVectorList))
    return unreal.GeometryScript_List.convert_vector_list_to_array(vectors)


def measure_head(body):
    """Head bone, and the box (min, max) of the vertices above the neck."""
    skeleton = body.get_editor_property("skeleton")
    head = bone_position(skeleton, HEAD_BONE)
    neck = bone_position(skeleton, NECK_BONE)
    points = [p for p in vertex_positions(read_lod(body, 0)) if p.z > neck.z]
    low = unreal.Vector(min(p.x for p in points), min(p.y for p in points), min(p.z for p in points))
    high = unreal.Vector(max(p.x for p in points), max(p.y for p in points), max(p.z for p in points))
    return head, low, high


def fit(child, adult, tune):
    """Scale around the adult head bone, and the offset after it, so the adult head's box lands on the child's."""
    (child_head, child_low, child_high) = child
    (adult_head, adult_low, adult_high) = adult
    scale = 0.5 * ((child_high.x - child_low.x) / (adult_high.x - adult_low.x)
                   + (child_high.y - child_low.y) / (adult_high.y - adult_low.y)) * tune
    # Relative to the head bone: the middle of the head across, and its top.
    child_mid_y = 0.5 * (child_low.y + child_high.y) - child_head.y
    adult_mid_y = 0.5 * (adult_low.y + adult_high.y) - adult_head.y
    offset = unreal.Vector(0.0, child_mid_y - scale * adult_mid_y, (child_high.z - child_head.z) - scale * (adult_high.z - adult_head.z))
    return scale, offset


def lod_count(mesh):
    subsystem = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    return subsystem.get_lod_count(mesh) if subsystem else unreal.EditorSkeletalMeshLibrary.get_lod_count(mesh)


def fit_copy(mesh, pivot, scale, offset):
    for lod in range(lod_count(mesh)):
        dyn = read_lod(mesh, lod)
        unreal.GeometryScript_MeshTransforms.scale_mesh(dyn, unreal.Vector(scale, scale, scale), pivot, True)
        unreal.GeometryScript_MeshTransforms.translate_mesh(dyn, offset)
        write = unreal.GeometryScriptMeshWriteLOD()
        write.set_editor_property("lod_index", lod)
        options = unreal.GeometryScriptCopyMeshToAssetOptions()
        options.set_editor_property("replace_materials", False)
        _dyn, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_to_skeletal_mesh(dyn, mesh, options, write)
        if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
            raise RuntimeError(f"{LOG_TAG} Could not write LOD {lod} of {mesh.get_path_name()}")


def main():
    child = measure_head(unreal.load_asset(CHILD_BODY))
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    made = 0
    for folder, pack_name, adult_body, tune in PACKS:
        body = unreal.load_asset(adult_body)
        adult = measure_head(body)
        scale, offset = fit(child, adult, tune)
        pivot = adult[0]
        log(f"{pack_name}: scale {scale:.3f} around the adult head bone ({pivot.x:.2f}, {pivot.y:.2f}, {pivot.z:.2f}), "
            f"then offset ({offset.x:.2f}, {offset.y:.2f}, {offset.z:.2f})")

        for data in sorted(registry.get_assets_by_path(folder, recursive=False), key=lambda d: str(d.asset_name)):
            name = str(data.asset_name)
            if not name.startswith("SK_Hat_"):
                continue
            source = f"{folder}/{name}"
            target = f"{TARGET_PATH}/{name.replace('SK_Hat_', f'SK_Hat_{pack_name}_', 1)}"
            if unreal.EditorAssetLibrary.does_asset_exist(target):
                unreal.EditorAssetLibrary.delete_asset(target)
            copy = unreal.EditorAssetLibrary.duplicate_asset(source, target)
            if not copy:
                raise RuntimeError(f"{LOG_TAG} Could not copy {source} to {target}")
            fit_copy(copy, pivot, scale, offset)
            if not unreal.EditorAssetLibrary.save_loaded_asset(copy, only_if_is_dirty=False):
                raise RuntimeError(f"{LOG_TAG} Could not save {target}")
            made += 1
    log(f"{made} hats in {TARGET_PATH}")
    log("Done")


main()
