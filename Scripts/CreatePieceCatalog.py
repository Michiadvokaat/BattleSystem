"""Creates or updates the LevelDesigner piece catalog (UCombatPieceCatalog) from the catalog folder.

Run headless with the editor closed:
    UnrealEditor-Cmd.exe BattleSystem.uproject -run=pythonscript -script="<abs path>/Scripts/CreatePieceCatalog.py" -unattended -nullrhi

Every static mesh in CATALOG_PATH/<Group>/<Sub>/ becomes a piece with category "<Group>/<Sub>" and id
"<Group>/<Sub>/<MeshName>"; the LevelDesigner shows the groups (Building, Furniture, Props) in one row and their
subcategories in a second. Pieces already in the catalog are kept as they are (tuned in the editor); new meshes are
added with defaults from their bounds, by their subcategory (the last folder):
- Everything under DETAIL_GROUP (Props): detail layer (small objects on a DETAIL_GRID x DETAIL_GRID grid per cell),
  never block by default.
- Floors: floor layer, footprint = size in cells, never blocks.
- Walls, Windows, Doors, DoorLeaves thinner than THIN_LIMIT: border pieces along the long side, length in cells;
  Windows and Doors (frames) get the slot "Opening" (they share a border with a wall and cut it; frames do not block);
  DoorLeaves get the slot "Leaf" (they share a border with a frame) and block.
The slot per subcategory (SLOT_CATEGORIES) is also set on existing entries, so an old catalog gets the slots.
- Everything else: cell pieces, footprint = size in cells (at least 1); Walls block walking and sight, others walking.
An existing piece whose mesh moved to another folder of the catalog keeps its tuning and gets that folder's category
and id (the mesh name and any variant suffix stay); levels that use the old id lose that piece's look until the id is
changed (RestructureCatalog.py did this for the move to groups). Pieces whose mesh is gone from the catalog folder are
removed. The pieces are ordered by CATEGORY_ORDER (the LevelDesigner shows categories in catalog order; it sorts the
pieces of a category by size itself). The meshes are local content, so the catalog is local too.
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
BORDER_CATEGORIES = ("Walls", "Windows", "Doors", "DoorLeaves")
# Category -> slot: pieces only replace pieces of the same layer and slot.
SLOT_CATEGORIES = {"DoorLeaves": "Leaf", "Windows": "Opening", "Doors": "Opening"}
VISUAL_ONLY_CATEGORIES = ("Doors",)
DETAIL_GROUP = "Props"
DETAIL_GRID = 3
# Border piece id -> lengths in cells of its scaled-to-fit variants.
VARIANTS = {"Building/Walls/SM_Walls_008": (1, 2, 3)}
# Catalog (and so LevelDesigner) order of the categories; others follow alphabetically.
CATEGORY_ORDER = (
    "Building/Walls", "Building/Windows", "Building/Doors", "Building/DoorLeaves", "Building/Floors",
    "Furniture/Chairs", "Furniture/Tables",
    "Props/Food", "Props/OfficeSupplies", "Props/Toys",
)


def log(message):
    unreal.log(f"{LOG_TAG} {message}")


def cells(length):
    return max(1, int(round(length / CELL_SIZE)))


def sub_category(category):
    return category.rsplit("/", 1)[-1]


def category_of(mesh):
    """The catalog category of a mesh: its folder relative to CATALOG_PATH, or None outside it."""
    package = mesh.get_path_name().split(".")[0]
    if not package.startswith(CATALOG_PATH + "/"):
        return None
    folder = package[len(CATALOG_PATH) + 1:].rsplit("/", 1)
    return folder[0] if len(folder) == 2 else None


def category_rank(category):
    return (CATEGORY_ORDER.index(category), "") if category in CATEGORY_ORDER else (len(CATEGORY_ORDER), category)


def make_definition(category, mesh):
    sub = sub_category(category)
    box = mesh.get_bounding_box()
    size = box.max - box.min
    definition = unreal.CombatPieceDefinition()
    definition.set_editor_property("id", f"{category}/{mesh.get_name()}")
    definition.set_editor_property("category", category)
    definition.set_editor_property("mesh", mesh)

    if category.split("/")[0] == DETAIL_GROUP:
        layer, footprint, walk, sight, yaw = unreal.CombatPieceLayer.DETAIL, (1, 1), False, False, 0.0
        definition.set_editor_property("detail_grid", DETAIL_GRID)
    elif sub == "Floors":
        layer, footprint, walk, sight, yaw = unreal.CombatPieceLayer.FLOOR, (cells(size.x), cells(size.y)), False, False, 0.0
    elif sub in BORDER_CATEGORIES and min(size.x, size.y) < THIN_LIMIT:
        # The length goes along X; a mesh long along Y is turned 90.
        long_along_x = size.x >= size.y
        blocks = sub not in VISUAL_ONLY_CATEGORIES
        layer, footprint, walk, sight = unreal.CombatPieceLayer.EDGE, (cells(max(size.x, size.y)), 1), blocks, blocks
        yaw = 0.0 if long_along_x else 90.0
    else:
        layer, footprint, walk, sight, yaw = unreal.CombatPieceLayer.CELL, (cells(size.x), cells(size.y)), True, sub == "Walls", 0.0

    definition.set_editor_property("slot", SLOT_CATEGORIES.get(sub, ""))
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

    # Pieces whose mesh moved to another folder take its category; the id keeps the part after the old category.
    for piece in pieces:
        mesh = piece.get_editor_property("mesh")
        category = category_of(mesh) if mesh else None
        old_category = piece.get_editor_property("category")
        if category and category != old_category:
            old_id = piece.get_editor_property("id")
            name = old_id[len(old_category) + 1:] if old_id.startswith(old_category + "/") else old_id.rsplit("/", 1)[-1]
            piece.set_editor_property("category", category)
            piece.set_editor_property("id", f"{category}/{name}")
            log(f"  moved {old_id} -> {category}/{name}")

    known = {piece.get_editor_property("id") for piece in pieces}
    found = set()

    for path in sorted(unreal.EditorAssetLibrary.list_assets(CATALOG_PATH, recursive=True)):
        mesh = unreal.load_asset(path)
        if not isinstance(mesh, unreal.StaticMesh):
            continue
        category = category_of(mesh)
        if not category:
            continue
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

    for piece in pieces:
        wanted = SLOT_CATEGORIES.get(sub_category(piece.get_editor_property("category")), "")
        if piece.get_editor_property("slot") != wanted:
            piece.set_editor_property("slot", wanted)
            log(f"  slot {piece.get_editor_property('id')} = '{wanted}'")

    gone = known - found
    for piece_id in sorted(gone):
        unreal.log_warning(f"{LOG_TAG} {piece_id}: its mesh is no longer in {CATALOG_PATH}/<group>/<sub>; removed")
    pieces = [piece for piece in pieces if piece.get_editor_property("id") not in gone]
    pieces.sort(key=lambda piece: category_rank(piece.get_editor_property("category")))

    catalog.set_editor_property("pieces", pieces)
    if not unreal.EditorAssetLibrary.save_loaded_asset(catalog, only_if_is_dirty=False):
        raise RuntimeError(f"{LOG_TAG} Could not save {catalog.get_path_name()}")
    log(f"Saved {catalog.get_path_name()}: {len(pieces)} pieces ({len(found - known)} new, {len(gone)} removed)")


main()
