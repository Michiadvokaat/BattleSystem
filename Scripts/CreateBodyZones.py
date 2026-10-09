"""Gives the child bodies their hide zones: a zone code per vertex, and a masked body material that hides zones.

Step 4 of Scripts/FitChildParts.ps1; run headless with the editor closed:
    UnrealEditor-Cmd.exe BattleSystem.uproject -run=pythonscript -script="<abs path>/Scripts/CreateBodyZones.py" -unattended -nullrhi -nosplash

Reads FITTED_DIR/body_zones.json, written by Scripts/Blender/FitToHero.py: the hero's skin vertices (Blender world space,
m) with their zone (index into ECombatBodyZone, -1 for the face). Matches them to the vertices of SKM_Hero (trying the
axis signs of the FBX round trip), then for every body in BODIES:
- writes the zone + 1 into UV channel ZONE_UV_SET (u; 0 = never hidden). A UV channel instead of vertex colors, because
  floats keep the codes exact (vertex colors go through an sRGB conversion);
- gives it <its material>_BodyZones in MATERIAL_PATH: a copy of its material, masked, whose opacity mask hides the zones
  flagged in the vector parameters HideZones0.. (four zones each; set by ACombatUnitActor from the clothing it wears). The
  hide flag is computed per vertex and interpolated (VertexInterpolator), so the cut runs through the triangles between a
  hidden and a shown zone instead of reading wrong codes there.
The bodies share one topology; one whose vertices do not line up with SKM_Hero's gets its zones by nearest position.
The first run copies the bodies to BACKUP_DIR; later runs keep that backup. The materials are made again every run.
"""

import json
import math
import os
import shutil

import unreal

LOG_TAG = "[BodyZones]"
FITTED_DIR = "D:/Unreal/Assets/Blender/Fitted"
BACKUP_DIR = "D:/Unreal/Backups/BattleSystem_2026-10-09_BodyZones"
HERO_MESH = "/Game/Characters/Meshes/SKM_Hero"
BODIES = [HERO_MESH] + [f"/Game/Characters/Meshes/Child/{g}/SKM_Child_{g}_Body_0{i}" for g in ("Male", "Female") for i in range(1, 5)]
MATERIAL_PATH = "/Game/Characters/Materials"
ZONE_UV_SET = 1
# Vertices of a body that lie further than this (cm) from SKM_Hero's on average do not share its vertex order.
SAME_ORDER_DISTANCE = 3.0
GRID = 3.0
# Body materials by name, for bodies that already carry a zone material (a rerun).
BASE_MATERIALS = {
    "M_Color": "/Game/ZZ_FAB/City_Characters/Materials/M_Color",
    "M_ITHappy": "/Game/Materials/M_ITHappy",
}


def log(message):
    unreal.log(f"{LOG_TAG} {message}")


def read_mesh(mesh):
    read = unreal.GeometryScriptMeshReadLOD()
    read.set_editor_property("lod_index", 0)
    dyn, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_from_skeletal_mesh(
        mesh, unreal.DynamicMesh(), unreal.GeometryScriptCopyMeshFromAssetOptions(), read)
    if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
        raise RuntimeError(f"{LOG_TAG} Could not read {mesh.get_path_name()}")
    if unreal.GeometryScript_MeshQueries.get_has_vertex_id_gaps(dyn):
        raise RuntimeError(f"{LOG_TAG} {mesh.get_path_name()} has vertex ID gaps")
    return dyn


def positions(dyn):
    result = unreal.GeometryScript_MeshQueries.get_all_vertex_positions(dyn, False)
    vectors = next(r for r in result if isinstance(r, unreal.GeometryScriptVectorList))
    return [(p.x, p.y, p.z) for p in unreal.GeometryScript_List.convert_vector_list_to_array(vectors)]


class Nearest:
    """Nearest of a set of points (x, y, z, value) through a uniform grid."""

    def __init__(self, points):
        self.cells = {}
        for p in points:
            self.cells.setdefault(self.key(p), []).append(p)

    @staticmethod
    def key(p):
        return (math.floor(p[0] / GRID), math.floor(p[1] / GRID), math.floor(p[2] / GRID))

    def find(self, p):
        kx, ky, kz = self.key(p)
        for reach in range(1, 8):
            best = None
            for dx in range(-reach, reach + 1):
                for dy in range(-reach, reach + 1):
                    for dz in range(-reach, reach + 1):
                        for q in self.cells.get((kx + dx, ky + dy, kz + dz), ()):
                            d = (q[0] - p[0]) ** 2 + (q[1] - p[1]) ** 2 + (q[2] - p[2]) ** 2
                            if best is None or d < best[0]:
                                best = (d, q)
            if best is not None:
                return math.sqrt(best[0]), best[1][3]
        return None, -1


def match_zones(points, vertices):
    """Zones of the vertices (UE cm) from the Blender points: the axis signs with the smallest mean distance win."""
    best = None
    for sx in (1, -1):
        for sy in (1, -1):
            nearest = Nearest([(sx * x * 100.0, sy * y * 100.0, z * 100.0, zone) for x, y, z, zone in points])
            sample = vertices[::max(1, len(vertices) // 300)]
            mean = sum(nearest.find(v)[0] for v in sample) / len(sample)
            if best is None or mean < best[0]:
                best = (mean, sx, sy, nearest)
    mean, sx, sy, nearest = best
    log(f"Blender to UE: x * {sx * 100}, y * {sy * 100}, z * 100 (mean distance {mean:.2f} cm)")
    if mean > 1.0:
        raise RuntimeError(f"{LOG_TAG} The Blender points do not line up with {HERO_MESH} ({mean:.2f} cm)")
    return [nearest.find(v)[1] for v in vertices], nearest


def write_zone_uvs(dyn, zones):
    """UV set ZONE_UV_SET: u = zone + 1 at every corner of every triangle (0 for the face)."""
    unreal.GeometryScript_UVs.set_num_uv_sets(dyn, ZONE_UV_SET + 1)
    for triangle in triangle_ids(dyn):
        result = unreal.GeometryScript_MeshQueries.get_triangle_indices(dyn, triangle)
        corners = next(r for r in (result if isinstance(result, tuple) else (result,)) if isinstance(r, unreal.IntVector))
        uvs = unreal.GeometryScriptUVTriangle()
        uvs.uv0 = unreal.Vector2D(zones[corners.x] + 1.0, 0.0)
        uvs.uv1 = unreal.Vector2D(zones[corners.y] + 1.0, 0.0)
        uvs.uv2 = unreal.Vector2D(zones[corners.z] + 1.0, 0.0)
        unreal.GeometryScript_UVs.set_mesh_triangle_u_vs(dyn, ZONE_UV_SET, triangle, uvs)


def triangle_ids(dyn):
    result = unreal.GeometryScript_MeshQueries.get_all_triangle_i_ds(dyn)
    ids = next(r for r in (result if isinstance(result, tuple) else (result,)) if isinstance(r, unreal.GeometryScriptIndexList))
    return unreal.GeometryScript_List.convert_index_list_to_array(ids)


def zone_code(parameter_count):
    """HLSL of the Custom node: 1 = show, 0 = hide, for the zone code in Zone.x."""
    # z < 4 -> H0, z < 8 -> H1, ...
    chain = f"H{parameter_count - 1}"
    for i in range(parameter_count - 2, -1, -1):
        chain = f"(z < {4.0 * (i + 1):.1f} ? H{i} : {chain})"
    return (
        "float z = round(Zone.x) - 1.0;\n"
        "if (z < 0.0) return 1.0;\n"
        f"float4 v = {chain};\n"
        "float c = z - 4.0 * floor(z / 4.0);\n"
        "float h = c < 0.5 ? v.x : (c < 1.5 ? v.y : (c < 2.5 ? v.z : v.w));\n"
        "return 1.0 - h;"
    )


def zone_material(base, parameter_count):
    """<base>_BodyZones: a masked copy of the body material that hides the flagged zones."""
    lib = unreal.EditorAssetLibrary
    edit = unreal.MaterialEditingLibrary
    target = f"{MATERIAL_PATH}/{base.get_name()}_BodyZones"
    if lib.does_asset_exist(target):
        lib.delete_asset(target)
    material = lib.duplicate_asset(base.get_path_name().split(".")[0], target)
    if not isinstance(material, unreal.Material):
        raise RuntimeError(f"{LOG_TAG} {base.get_path_name()} is not a Material; zone materials need one")
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("opacity_mask_clip_value", 0.5)

    coords = edit.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1200, 600)
    coords.set_editor_property("coordinate_index", ZONE_UV_SET)
    custom = edit.create_material_expression(material, unreal.MaterialExpressionCustom, -700, 600)
    custom.set_editor_property("code", zone_code(parameter_count))
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    custom.set_editor_property("description", "Hide zones")
    inputs = []
    for name in ["Zone"] + [f"H{i}" for i in range(parameter_count)]:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", name)
        inputs.append(custom_input)
    custom.set_editor_property("inputs", inputs)
    if not edit.connect_material_expressions(coords, "", custom, "Zone"):
        raise RuntimeError(f"{LOG_TAG} Could not connect the zone UVs")
    for i in range(parameter_count):
        parameter = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -1200, 750 + 150 * i)
        parameter.set_editor_property("parameter_name", f"HideZones{i}")
        parameter.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
        outputs = edit.get_material_expression_output_names(parameter)
        output = "RGBA" if "RGBA" in outputs else ""
        if not edit.connect_material_expressions(parameter, output, custom, f"H{i}"):
            raise RuntimeError(f"{LOG_TAG} Could not connect HideZones{i} (outputs {outputs})")
    interpolator = edit.create_material_expression(material, unreal.MaterialExpressionVertexInterpolator, -300, 600)
    interpolator_inputs = edit.get_material_expression_input_names(interpolator)
    interpolator_outputs = edit.get_material_expression_output_names(interpolator)
    if not edit.connect_material_expressions(custom, "", interpolator, interpolator_inputs[0]):
        raise RuntimeError(f"{LOG_TAG} Could not connect the vertex interpolator (inputs {interpolator_inputs})")
    if not edit.connect_material_property(interpolator, interpolator_outputs[0] if interpolator_outputs else "",
                                          unreal.MaterialProperty.MP_OPACITY_MASK):
        raise RuntimeError(f"{LOG_TAG} Could not connect the opacity mask (outputs {interpolator_outputs})")
    edit.recompile_material(material)
    if not lib.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"{LOG_TAG} Could not save {target}")
    log(f"{target}: masked copy of {base.get_name()}, {parameter_count} zone parameters")
    return material


def restore_base_materials():
    """The base material of every body (name -> path). A body that already has a zone material (a rerun) gets its base
    material back first, found by the zone material's name (<base>_BodyZones) in BASE_MATERIALS, so the zone materials
    can be deleted and made again without anything still using them."""
    bases = {}
    for body in BODIES:
        mesh = unreal.load_asset(body)
        slots = []
        for slot in mesh.get_editor_property("materials"):
            name = slot.get_editor_property("material_interface").get_name()
            if name.endswith("_BodyZones"):
                name = name[:-len("_BodyZones")]
                if name not in BASE_MATERIALS:
                    raise RuntimeError(f"{LOG_TAG} {body} has {name}_BodyZones but {name} is not in BASE_MATERIALS")
                slot.set_editor_property("material_interface", unreal.load_asset(BASE_MATERIALS[name]))
            slots.append(slot)
            bases[name] = slot.get_editor_property("material_interface").get_path_name().split(".")[0]
        if any(s.get_editor_property("material_interface").get_name() != o.get_editor_property("material_interface").get_name()
               for s, o in zip(slots, mesh.get_editor_property("materials"))):
            mesh.set_editor_property("materials", slots)
            unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
    return bases


def main():
    content = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
    if not os.path.isdir(BACKUP_DIR):
        for body in BODIES:
            source = os.path.join(content, body[len("/Game/"):] + ".uasset")
            target = os.path.join(BACKUP_DIR, body[len("/Game/"):] + ".uasset")
            os.makedirs(os.path.dirname(target), exist_ok=True)
            shutil.copy2(source, target)
        log(f"Backup of {len(BODIES)} bodies in {BACKUP_DIR}")

    with open(f"{FITTED_DIR}/body_zones.json", encoding="utf-8") as f:
        data = json.load(f)
    parameter_count = (len(data["zones"]) + 3) // 4

    hero = unreal.load_asset(HERO_MESH)
    hero_positions = positions(read_mesh(hero))
    hero_zones, nearest = match_zones(data["points"], hero_positions)
    counts = {name: hero_zones.count(i) for i, name in enumerate(data["zones"])}
    log(f"{HERO_MESH}: {len(hero_zones)} vertices, zones {counts}, face {hero_zones.count(-1)}")

    bases = restore_base_materials()
    unreal.SystemLibrary.collect_garbage()
    materials = {name: zone_material(unreal.load_asset(path), parameter_count) for name, path in bases.items()}

    for body in BODIES:
        mesh = unreal.load_asset(body)
        dyn = read_mesh(mesh)
        vertices = positions(dyn)
        if body == HERO_MESH:
            zones = hero_zones
        elif len(vertices) == len(hero_positions) and sum(
                math.dist(a, b) for a, b in zip(vertices, hero_positions)) / len(vertices) < SAME_ORDER_DISTANCE:
            zones = hero_zones
        else:
            found = [nearest.find(v) for v in vertices]
            zones = [zone for _, zone in found]
            log(f"{body}: vertices do not line up with {HERO_MESH}; zones by nearest position "
                f"(mean {sum(d for d, _ in found) / len(found):.2f} cm, max {max(d for d, _ in found):.2f} cm)")
        write_zone_uvs(dyn, zones)
        write = unreal.GeometryScriptMeshWriteLOD()
        write.set_editor_property("lod_index", 0)
        options = unreal.GeometryScriptCopyMeshToAssetOptions()
        options.set_editor_property("replace_materials", False)
        _dyn, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_to_skeletal_mesh(dyn, mesh, options, write)
        if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
            raise RuntimeError(f"{LOG_TAG} Could not write {body}")
        slots = []
        for slot in mesh.get_editor_property("materials"):
            name = slot.get_editor_property("material_interface").get_name()
            name = name[:-len("_BodyZones")] if name.endswith("_BodyZones") else name
            slot.set_editor_property("material_interface", materials[name])
            slots.append(slot)
        mesh.set_editor_property("materials", slots)
        if not unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False):
            raise RuntimeError(f"{LOG_TAG} Could not save {body}")
        log(f"{body}: zones in UV {ZONE_UV_SET}, material {materials[name].get_name()}")
    log("Done")


main()
