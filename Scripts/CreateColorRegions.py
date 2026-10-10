"""Gives the child's parts colour regions that a look can recolour (FCombatLookSlot::Colors, applied by ACombatUnitActor).

Step 5 of Scripts/FitChildParts.ps1 (the fitted parts only, argument "fitted"); run headless with the editor closed. Without
arguments it does every part in PART_FOLDERS; asset paths as arguments do just those:
    UnrealEditor-Cmd.exe BattleSystem.uproject -run=pythonscript -script="<abs path>/Scripts/CreateColorRegions.py [fitted]" -unattended -nullrhi -nosplash

The packs colour their parts through one texture each (PALETTES): City and Creative a continuous gradient (hue across,
lightness down), Funny a grid of swatches; every face's UVs sit on one small spot of it. So a part's colours are the clusters
of its faces' UV spots:
- per triangle the centre of its UVs, weighted by its area; grouped on a GRID x GRID raster into connected clusters (a
  gradient strip of light and dark shades of one colour is one cluster); the clusters of at least MIN_SHARE of the area,
  largest first, up to REGIONS, are the colour regions (region 1 = the main colour); the rest keeps its colour;
- per region its UV box (Region<N>_Rect: u min, v min, u max, v max) and a reference spot (Region<N>_Ref: the area-weighted
  mean of its UVs, the region's own mid colour).
Every pack texture gets a recolour material (RECOLOR_PATH/<pack>_<material>_Recolor, made once; delete it to make it again
after changing recolor_code or the graph, with the editor closed): a copy of the
pack material whose base colour, inside the box of a region with Use<N> = 1, becomes Color<N> times the texel's lightness
over the reference spot's lightness, so the shades of the region follow the new colour. Every part gets its own material
instance of it (<part folder>/Materials/MI_<part>, with its regions; Use<N> = 0 keeps the colour), because merging
(FSkeletalMeshMerge) joins sections with the same material and each part must stay its own section to get its own colours.
Material slots with other materials (glass, chains, special fabrics) are left as they are.
"""

import json
import sys
import time

import unreal

LOG_TAG = "[ColorRegions]"
FITTED_JSON = "D:/Unreal/Assets/Blender/Fitted/fitted.json"
PART_FOLDERS = ["/Game/Characters/Meshes/Child/" + f for f in
                ("Hats", "Outwear", "Pants", "Facewear", "Shoes", "Hair", "Gloves", "Accessories", "Male", "Female")]
RECOLOR_PATH = "/Game/Characters/Materials"
# The pack materials whose texture is a colour palette -> the name of their recolour material.
PALETTES = {
    "/Game/ZZ_FAB/City_Characters/Materials/M_Color": "City_M_Color_Recolor",
    "/Game/ZZ_FAB/Creative_Characters/Materials/M_Color": "Creative_M_Color_Recolor",
    "/Game/ZZ_FAB/Funny_Characters/Materials/M_Material": "Funny_M_Material_Recolor",
}
REGIONS = 4
GRID = 64
MIN_SHARE = 0.02
RECT_PADDING = 0.002
SAVE_ATTEMPTS = 4
# Parts that are bodies (they carry the hide zone material) are skipped.
SKIP = ("_Body_",)


def log(message):
    unreal.log(f"{LOG_TAG} {message}")


def save(asset, path):
    """Saves, retrying a few times: another program (a virus scanner, the indexer) can hold the file for a moment."""
    for attempt in range(SAVE_ATTEMPTS):
        if unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            return
        time.sleep(1.0 + attempt)
    raise RuntimeError(f"{LOG_TAG} Could not save {path}")


def pick(result, kind):
    """Geometry Script returns tuples of out parameters; the first of the given type."""
    return next(r for r in (result if isinstance(result, tuple) else (result,)) if isinstance(r, kind))


def base_material(material):
    while isinstance(material, unreal.MaterialInstance):
        material = material.get_editor_property("parent")
    return material


def recolor_code():
    lines = ["float3 w = float3(0.2126, 0.7152, 0.0722);", "float l = dot(Orig, w);"]
    for i in range(REGIONS):
        lines.append(f"if (U{i} > 0.5 && UV.x >= R{i}.x && UV.y >= R{i}.y && UV.x <= R{i}.z && UV.y <= R{i}.w) "
                     f"return C{i} * min(l / max(dot(B{i}, w), 0.0001), 4.0);")
    lines.append("return Orig;")
    return "\n".join(lines)


def make_recolor_material(base_path, name):
    """A copy of the pack material with the recolour in front of its base colour (an existing one is used as it is: the part
    instances refer to it)."""
    lib = unreal.EditorAssetLibrary
    edit = unreal.MaterialEditingLibrary
    target = f"{RECOLOR_PATH}/{name}"
    if lib.does_asset_exist(target):
        return unreal.load_asset(target)
    material = lib.duplicate_asset(base_path, target)
    texture_node = edit.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
    texture_output = edit.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_BASE_COLOR)
    texture = texture_node.get_editor_property("texture")

    custom = edit.create_material_expression(material, unreal.MaterialExpressionCustom, -400, 0)
    custom.set_editor_property("code", recolor_code())
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("description", "Recolour")
    names = ["Orig", "UV"] + [f"{k}{i}" for i in range(REGIONS) for k in ("R", "B", "C", "U")]
    inputs = []
    for input_name in names:
        custom_input = unreal.CustomInput()
        custom_input.set_editor_property("input_name", input_name)
        inputs.append(custom_input)
    custom.set_editor_property("inputs", inputs)

    def connect(source, output, pin):
        if not edit.connect_material_expressions(source, output, custom, pin):
            raise RuntimeError(f"{LOG_TAG} {name}: could not connect {pin}")

    connect(texture_node, texture_output, "Orig")
    coords = edit.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1600, 0)
    connect(coords, "", "UV")
    for i in range(REGIONS):
        y = 200 + 450 * i

        def parameter(kind, parameter_name, row):
            node = edit.create_material_expression(material, kind, -1600, y + 100 * row)
            node.set_editor_property("parameter_name", parameter_name)
            return node

        rect = parameter(unreal.MaterialExpressionVectorParameter, f"Region{i}_Rect", 0)
        rect.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 0.0, 0.0))  # empty box
        connect(rect, "RGBA" if "RGBA" in edit.get_material_expression_output_names(rect) else "", f"R{i}")
        ref = parameter(unreal.MaterialExpressionVectorParameter, f"Region{i}_Ref", 1)
        mask = edit.create_material_expression(material, unreal.MaterialExpressionComponentMask, -1300, y + 100)
        mask.set_editor_property("r", True)
        mask.set_editor_property("g", True)
        mask.set_editor_property("b", False)
        mask.set_editor_property("a", False)
        edit.connect_material_expressions(ref, "", mask, "")
        sample = edit.create_material_expression(material, unreal.MaterialExpressionTextureSample, -1000, y + 100)
        sample.set_editor_property("texture", texture)
        sample.set_editor_property("sampler_type", texture_node.get_editor_property("sampler_type"))
        if not edit.connect_material_expressions(mask, "", sample, "UVs"):
            raise RuntimeError(f"{LOG_TAG} {name}: could not connect the reference UVs")
        connect(sample, "RGB", f"B{i}")
        color = parameter(unreal.MaterialExpressionVectorParameter, f"Color{i}", 2)
        color.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
        connect(color, "", f"C{i}")
        use = parameter(unreal.MaterialExpressionScalarParameter, f"Use{i}", 3)
        use.set_editor_property("default_value", 0.0)
        connect(use, "", f"U{i}")
    if not edit.connect_material_property(custom, "", unreal.MaterialProperty.MP_BASE_COLOR):
        raise RuntimeError(f"{LOG_TAG} {name}: could not connect the base colour")
    edit.recompile_material(material)
    if not lib.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f"{LOG_TAG} Could not save {target}")
    log(f"{target}: recolour of {base_path}")
    return material


def color_regions(mesh, slots):
    """The part's colour regions on the triangles of the given material slots: [(rect, ref, share)], largest first."""
    read = unreal.GeometryScriptMeshReadLOD()
    read.set_editor_property("lod_index", 0)
    dyn, outcome = unreal.GeometryScript_AssetUtils.copy_mesh_from_skeletal_mesh(
        mesh, unreal.DynamicMesh(), unreal.GeometryScriptCopyMeshFromAssetOptions(), read)
    if outcome != unreal.GeometryScriptOutcomePins.SUCCESS:
        raise RuntimeError(f"{LOG_TAG} Could not read {mesh.get_path_name()}")
    queries = unreal.GeometryScript_MeshQueries
    ids = unreal.GeometryScript_List.convert_index_list_to_array(pick(queries.get_all_triangle_i_ds(dyn), unreal.GeometryScriptIndexList))
    material_ids = unreal.GeometryScript_List.convert_index_list_to_array(
        pick(unreal.GeometryScript_Materials.get_all_triangle_material_i_ds(dyn), unreal.GeometryScriptIndexList))
    bins = {}
    for t, material_id in zip(ids, material_ids):
        if material_id not in slots:
            continue
        uvs = [r for r in queries.get_triangle_u_vs(dyn, 0, t) if isinstance(r, unreal.Vector2D)]
        p = [r for r in queries.get_triangle_positions(dyn, t) if isinstance(r, unreal.Vector)]
        area = 0.5 * (p[1] - p[0]).cross(p[2] - p[0]).length()
        cu, cv = sum(u.x for u in uvs) / 3.0, sum(u.y for u in uvs) / 3.0
        key = (min(GRID - 1, max(0, int(cu * GRID))), min(GRID - 1, max(0, int(cv * GRID))))
        b = bins.setdefault(key, [0.0, 0.0, 0.0, [1.0, 1.0, 0.0, 0.0]])
        b[0] += area
        b[1] += area * cu
        b[2] += area * cv
        box = b[3]
        box[0] = min(box[0], *(u.x for u in uvs))
        box[1] = min(box[1], *(u.y for u in uvs))
        box[2] = max(box[2], *(u.x for u in uvs))
        box[3] = max(box[3], *(u.y for u in uvs))
    total = sum(b[0] for b in bins.values())
    if total <= 0.0:
        return []
    # Connected clusters of occupied cells (8 neighbours).
    clusters, seen = [], set()
    for start in bins:
        if start in seen:
            continue
        stack, cells = [start], []
        seen.add(start)
        while stack:
            cell = stack.pop()
            cells.append(cell)
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    n = (cell[0] + dx, cell[1] + dy)
                    if n in bins and n not in seen:
                        seen.add(n)
                        stack.append(n)
        area = sum(bins[c][0] for c in cells)
        ref = (sum(bins[c][1] for c in cells) / area, sum(bins[c][2] for c in cells) / area)
        rect = [min(bins[c][3][0] for c in cells) - RECT_PADDING, min(bins[c][3][1] for c in cells) - RECT_PADDING,
                max(bins[c][3][2] for c in cells) + RECT_PADDING, max(bins[c][3][3] for c in cells) + RECT_PADDING]
        clusters.append((rect, ref, area / total))
    clusters.sort(key=lambda c: -c[2])
    return [c for c in clusters if c[2] >= MIN_SHARE][:REGIONS]


def part_instance(mesh, parent, regions):
    """MI_<part> under <part folder>/Materials, parented to the recolour material, with the part's regions."""
    lib = unreal.EditorAssetLibrary
    edit = unreal.MaterialEditingLibrary
    folder = mesh.get_path_name().rsplit("/", 1)[0] + "/Materials"
    name = f"MI_{mesh.get_name()}"
    path = f"{folder}/{name}"
    instance = unreal.load_asset(path) if lib.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, folder, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    edit.set_material_instance_parent(instance, parent)
    edit.clear_all_material_instance_parameters(instance)
    for i, (rect, ref, _share) in enumerate(regions):
        edit.set_material_instance_vector_parameter_value(instance, f"Region{i}_Rect", unreal.LinearColor(*rect))
        edit.set_material_instance_vector_parameter_value(instance, f"Region{i}_Ref", unreal.LinearColor(ref[0], ref[1], 0.0, 0.0))
    edit.update_material_instance(instance)
    save(instance, path)
    return instance


def parts_to_do():
    lib = unreal.EditorAssetLibrary
    if sys.argv[1:] == ["fitted"]:
        with open(FITTED_JSON, encoding="utf-8") as f:
            return [p["target"] for p in json.load(f)["parts"]]
    if sys.argv[1:]:
        return sys.argv[1:]
    paths = []
    for folder in PART_FOLDERS:
        if lib.does_directory_exist(folder):
            paths += [p.split(".")[0] for p in lib.list_assets(folder, recursive=False)]
    return paths


def main():
    recolors = {}
    count, regions_total = 0, 0
    for path in parts_to_do():
        if any(s in path for s in SKIP):
            continue
        mesh = unreal.load_asset(path)
        if not isinstance(mesh, unreal.SkeletalMesh):
            continue
        materials = mesh.get_editor_property("materials")
        slots = {}
        for index, slot in enumerate(materials):
            base = base_material(slot.get_editor_property("material_interface"))
            base_path = base.get_path_name().split(".")[0] if base else None
            if base_path in PALETTES:
                slots[index] = base_path
        if not slots:
            continue
        base_path = next(iter(slots.values()))
        if base_path not in recolors:
            recolors[base_path] = make_recolor_material(base_path, PALETTES[base_path])
        regions = color_regions(mesh, set(slots))
        instance = part_instance(mesh, recolors[base_path], regions)
        # A new list: an element read from an unreal.Array is a copy, so changing it in place changes nothing.
        new_materials = []
        for index, slot in enumerate(materials):
            if index in slots:
                slot.set_editor_property("material_interface", instance)
            new_materials.append(slot)
        mesh.set_editor_property("materials", new_materials)
        save(mesh, path)
        count += 1
        regions_total += len(regions)
        if count % 100 == 0:
            log(f"{count} parts ...")
    log(f"{count} parts with colour regions ({regions_total / max(1, count):.1f} regions on average)")
    log("Done")


main()
