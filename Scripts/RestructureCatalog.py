"""One-off: moves the catalog meshes into main categories with subcategories, and renames the piece ids in the levels.

Run headless with the editor closed:
    UnrealEditor-Cmd.exe BattleSystem.uproject -run=pythonscript -script="<abs path>/Scripts/RestructureCatalog.py" -unattended -nullrhi -nosplash

Then run CreatePieceCatalog.py, which moves the existing catalog entries (with their tuning) to the new categories.

Every mesh in CATALOG_PATH/<old>/ goes to CATALOG_PATH/<new>/ (MOVES; for Furniture by a word in its name,
FURNITURE_BY_NAME). The asset rename updates the assets that reference them. Afterwards every piece id
"<old>/<mesh>" in Levels/*.json becomes "<new>/<mesh>", and the levels go to format 4 (no cell rows). Already moved meshes and ids are left alone, so it can run
again. The meshes are local content (/Game/Environment is not in git); the levels are in git.
"""

import glob
import json
import os

import unreal

LOG_TAG = "[CatalogRestructure]"
CATALOG_PATH = "/Game/Environment/Catalogus"
LEVELS_DIR = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Levels")
# Old category folder -> new "Group/Sub" folder.
MOVES = {
    "Walls": "Building/Walls",
    "Windows": "Building/Windows",
    "Doors": "Building/Doors",
    "DoorLeaves": "Building/DoorLeaves",
    "Floors": "Building/Floors",
    "Details_Food": "Props/Food",
    "Details_OfficeSupplies": "Props/OfficeSupplies",
    "Details_Toys": "Props/Toys",
}
# The old Furniture folder is split by a word in the mesh name; other meshes go to FURNITURE_OTHER.
FURNITURE_BY_NAME = {"chair": "Furniture/Chairs", "table": "Furniture/Tables"}
FURNITURE_OTHER = "Furniture/Other"


def log(message):
    unreal.log(f"{LOG_TAG} {message}")


def new_category(old_category, mesh_name):
    if old_category == "Furniture":
        lower = mesh_name.lower()
        return next((category for word, category in FURNITURE_BY_NAME.items() if word in lower), FURNITURE_OTHER)
    return MOVES.get(old_category)


def move_meshes():
    moved = 0
    for path in sorted(unreal.EditorAssetLibrary.list_assets(CATALOG_PATH, recursive=True)):
        package = path.split(".")[0]
        relative = package[len(CATALOG_PATH) + 1:].split("/")
        if len(relative) != 2:
            continue
        old_category, name = relative
        category = new_category(old_category, name)
        if not category:
            continue
        destination = f"{CATALOG_PATH}/{category}/{name}"
        if unreal.EditorAssetLibrary.does_asset_exist(destination):
            unreal.log_warning(f"{LOG_TAG} {destination} already exists; {package} skipped")
            continue
        if unreal.EditorAssetLibrary.rename_asset(package, destination):
            moved += 1
            log(f"  {old_category}/{name} -> {category}/{name}")
        else:
            unreal.log_error(f"{LOG_TAG} Could not move {package} to {destination}")
    # The old Furniture folder is also a new group, so it stays.
    for old_category in MOVES:
        folder = f"{CATALOG_PATH}/{old_category}"
        if unreal.EditorAssetLibrary.does_directory_exist(folder):
            left = unreal.EditorAssetLibrary.list_assets(folder, recursive=True, include_folder=False)
            if left:
                unreal.log_warning(f"{LOG_TAG} {folder} still holds {len(left)} assets (redirectors?): {', '.join(left)}")
            else:
                unreal.EditorAssetLibrary.delete_directory(folder)
    log(f"Moved {moved} meshes")


def rename_level_ids():
    for file in sorted(glob.glob(os.path.join(LEVELS_DIR, "*.json"))):
        with open(file, encoding="utf-8") as handle:
            level = json.load(handle)
        # Format 4 has no cell rows (they were all open in the kept levels).
        changed = 1 if "rows" in level else 0
        level.pop("rows", None)
        level["formatVersion"] = 4
        for piece in level.get("pieces", []):
            parts = piece.get("id", "").split("/")
            if len(parts) != 2:
                continue
            category = new_category(parts[0], parts[1])
            if category:
                piece["id"] = f"{category}/{parts[1]}"
                changed += 1
        if changed:
            with open(file, "w", encoding="utf-8", newline="\r\n") as handle:
                json.dump(level, handle, indent="\t")
            log(f"  {os.path.basename(file)}: updated ({changed} changes)")


def main():
    move_meshes()
    rename_level_ids()


main()
