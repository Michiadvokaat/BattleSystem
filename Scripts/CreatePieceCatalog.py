"""Creates or updates the LevelDesigner piece catalog (UCombatPieceCatalog) from the catalog folder.

Run headless with the editor closed:
    UnrealEditor-Cmd.exe BattleSystem.uproject -run=pythonscript -script="<abs path>/Scripts/CreatePieceCatalog.py" -unattended -nullrhi

Every static mesh in CATALOG_PATH/<Category>/ becomes a piece with id "<Category>/<MeshName>". Pieces already in the
catalog are kept as they are (tuned in the editor); new meshes are added with defaults from their bounds:
- Details*: detail layer (small objects on a DETAIL_GRID x DETAIL_GRID grid per cell), never block by default.
- Floors: floor layer, footprint = size in cells, never blocks.
- Walls, Windows, Doors thinner than THIN_LIMIT: border pieces along the long side, length in cells; Doors do not block.
- Everything else: cell pieces, footprint = size in cells (at least 1); Walls block walking and sight, others walking.
Pieces whose mesh is gone are reported, not removed. The meshes are local content, so the catalog is local too.
VARIANTS adds shorter versions of border pieces ("<id>_<n>m", Size n, Scale To Fit) for openings next to doors.
"""

import unreal

LOG_TAG = "[PieceCatalog]"
CATALOG_PATH = "/Game/Environment/Catalogus"
ASSET_PATH = "/Game/Environment"
ASSET_NAME = "DA_PieceCatalog"
CELL_SIZE = 100.0
# Border pieces are thinner than this (cm); thicker ones fill cells (pillars, blocks).
THIN_LIMIT = 50.0
BORDER_CATEGORIES = ("Walls", "Windows", "Doors")
VISUAL_ONLY_CATEGORIES = ("Doors",)
DETAIL_PREFIX = "Details"
DETAIL_GRID = 3
# Border piece id -> lengths in cells of its scaled-to-fit variants.
VARIANTS = {"Walls/SM_Walls_008": (1, 2, 3)}


def log(message):
    unreal.log(f"{LOG_TAG} {message}")


def cells(length):
    return max(1, int(round(length / CELL_SIZE)))


def make_definition(category, mesh):
    box = mesh.get_bounding_box()
    size = box.max - box.min
    definition = unreal.CombatPieceDefinition()
    definition.set_editor_property("id", f"{category}/{mesh.get_name()}")
    definition.set_editor_property("category", category)
    definition.set_editor_property("mesh", mesh)

    if category.startswith(DETAIL_PREFIX):
        layer, footprint, walk, sight, yaw = unreal.CombatPieceLayer.DETAIL, (1, 1), False, False, 0.0
        definition.set_editor_property("detail_grid", DETAIL_GRID)
    elif category == "Floors":
        layer, footprint, walk, sight, yaw = unreal.CombatPieceLayer.FLOOR, (cells(size.x), cells(size.y)), False, False, 0.0
    elif category in BORDER_CATEGORIES and min(size.x, size.y) < THIN_LIMIT:
        # The length goes along X; a mesh long along Y is turned 90.
        long_along_x = size.x >= size.y
        blocks = category not in VISUAL_ONLY_CATEGORIES
        layer, footprint, walk, sight = unreal.CombatPieceLayer.EDGE, (cells(max(size.x, size.y)), 1), blocks, blocks
        yaw = 0.0 if long_along_x else 90.0
    else:
        layer, footprint, walk, sight, yaw = unreal.CombatPieceLayer.CELL, (cells(size.x), cells(size.y)), True, category == "Walls", 0.0

    definition.set_editor_property("layer", layer)
    definition.set_editor_property("size", unreal.IntPoint(*footprint))
    definition.set_editor_property("blocks_walking", walk)
    definition.set_editor_property("blocks_sight", sight)
    definition.set_editor_property("mesh_yaw", yaw)
    log(f"  + {category}/{mesh.get_name()}: {layer.name} {footprint[0]}x{footprint[1]} walk={walk} sight={sight} yaw={yaw}")
    return definition


def make_variant(definition, length):
    variant = unreal.CombatPieceDefinition()
    for name in ("category", "mesh", "layer", "blocks_walking", "blocks_sight", "mesh_yaw", "offset", "detail_grid"):
        variant.set_editor_property(name, definition.get_editor_property(name))
    variant.set_editor_property("id", f"{definition.get_editor_property('id')}_{length}m")
    variant.set_editor_property("size", unreal.IntPoint(length, 1))
    variant.set_editor_property("scale_to_fit", True)
    log(f"  + {variant.get_editor_property('id')}: {length} long, scale to fit")
    return variant


def load_or_create_catalog():
    full_path = f"{ASSET_PATH}/{ASSET_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        return unreal.load_asset(full_path)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.CombatPieceCatalog)
    catalog = unreal.AssetToolsHelpers.get_asset_tools().create_asset(ASSET_NAME, ASSET_PATH, unreal.CombatPieceCatalog, factory)
    if not catalog:
        raise RuntimeError(f"{LOG_TAG} Could not create {full_path}")
    log(f"Created {full_path}")
    return catalog


def main():
    catalog = load_or_create_catalog()
    pieces = list(catalog.get_editor_property("pieces"))
    known = {piece.get_editor_property("id") for piece in pieces}
    found = set()

    for path in sorted(unreal.EditorAssetLibrary.list_assets(CATALOG_PATH, recursive=True)):
        relative = path[len(CATALOG_PATH) + 1:].split("/")
        if len(relative) < 2:
            continue
        mesh = unreal.load_asset(path)
        if not isinstance(mesh, unreal.StaticMesh):
            continue
        category = relative[0]
        piece_id = f"{category}/{mesh.get_name()}"
        found.add(piece_id)
        if piece_id not in known:
            pieces.append(make_definition(category, mesh))

    for base_id, lengths in VARIANTS.items():
        base = next((piece for piece in pieces if piece.get_editor_property("id") == base_id), None)
        for length in lengths:
            variant_id = f"{base_id}_{length}m"
            found.add(variant_id)
            if base and variant_id not in known:
                pieces.append(make_variant(base, length))

    for piece_id in sorted(known - found):
        unreal.log_warning(f"{LOG_TAG} {piece_id} is in the catalog but its mesh is no longer in {CATALOG_PATH}; kept")

    catalog.set_editor_property("pieces", pieces)
    if not unreal.EditorAssetLibrary.save_loaded_asset(catalog, only_if_is_dirty=False):
        raise RuntimeError(f"{LOG_TAG} Could not save {catalog.get_path_name()}")
    log(f"Saved {catalog.get_path_name()}: {len(pieces)} pieces ({len(found - known)} new)")


main()
