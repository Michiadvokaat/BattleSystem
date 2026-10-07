"""Fills the unit and skill tables (/Game/Data/DT_Units, DT_Skills) from Data/Units.json and Data/Skills.json.
The JSON files are the source; the tables are made from them.

Run headless with the editor closed:
    UnrealEditor-Cmd.exe BattleSystem.uproject -run=pythonscript -script="<abs path>/Scripts/ImportCombatData.py" -unattended -nullrhi

After editing the tables in the editor, write them back to the JSON files instead (also editor closed):
    ... -script="<abs path>/Scripts/ImportCombatData.py export" ...
(or in the editor: right-click the table > Export as JSON over the file in Data/).

A table that does not exist yet is created. Every unit's skills must be in the skill table.
"""

import json
import os
import sys
import unreal

LOG_TAG = "[CombatData]"
TABLE_PATH = "/Game/Data"
DATA_DIR = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Data")
TABLES = [
    ("DT_Skills", unreal.CombatSkillRow, "Skills.json"),
    ("DT_Units", unreal.CombatUnitRow, "Units.json"),
]


def log(message):
    unreal.log(f"{LOG_TAG} {message}")


def load_or_create(name, row_struct):
    full_path = f"{TABLE_PATH}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        return unreal.load_asset(full_path)
    factory = unreal.DataTableFactory()
    factory.set_editor_property("struct", row_struct.static_struct())
    table = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, TABLE_PATH, unreal.DataTable, factory)
    if not table:
        raise RuntimeError(f"{LOG_TAG} Could not create {full_path}")
    log(f"Created {full_path}")
    return table


def check_skills():
    """Every skill a unit names must be a row of the skill table."""
    with open(os.path.join(DATA_DIR, "Skills.json"), encoding="utf-8") as file:
        skills = {row["Name"] for row in json.load(file)}
    with open(os.path.join(DATA_DIR, "Units.json"), encoding="utf-8") as file:
        units = json.load(file)
    missing = [f"{unit['Name']}: {skill}" for unit in units for skill in unit.get("Skills", []) if skill not in skills]
    if missing:
        raise RuntimeError(f"{LOG_TAG} Skills not in Skills.json: {', '.join(missing)}")


def import_tables():
    check_skills()
    for name, row_struct, file_name in TABLES:
        table = load_or_create(name, row_struct)
        file_path = os.path.join(DATA_DIR, file_name)
        if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_file(table, file_path):
            raise RuntimeError(f"{LOG_TAG} Could not fill {name} from {file_path}")
        if not unreal.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False):
            raise RuntimeError(f"{LOG_TAG} Could not save {name}")
        log(f"{name}: {len(unreal.DataTableFunctionLibrary.get_data_table_row_names(table))} rows from {file_name}")


def export_tables():
    for name, row_struct, file_name in TABLES:
        table = unreal.load_asset(f"{TABLE_PATH}/{name}")
        if not table:
            raise RuntimeError(f"{LOG_TAG} {TABLE_PATH}/{name} does not exist")
        file_path = os.path.join(DATA_DIR, file_name)
        if not unreal.DataTableFunctionLibrary.export_data_table_to_json_file(table, file_path):
            raise RuntimeError(f"{LOG_TAG} Could not write {file_path}")
        log(f"{name}: written to {file_name}")


if "export" in sys.argv[1:]:
    export_tables()
else:
    import_tables()
log("Done")
