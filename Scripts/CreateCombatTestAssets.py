"""Creates (or updates) the phase 1 combat test assets in /Game/Combat.

Run headless with the editor closed:
    UnrealEditor-Cmd.exe BattleSystem.uproject -run=pythonscript -script="<abs path>/Scripts/CreateCombatTestAssets.py" -unattended -nullrhi

Re-running overwrites the values below; tune in the editor afterwards only if you do not re-run this script.
"""

import unreal

LOG_TAG = "[CombatAssets]"
ASSET_PATH = "/Game/Combat"

UNITS = {
    "DA_Krijger": dict(name="Krijger", max_hp=100.0, move_speed=350.0, radius=40.0,
                       attack=dict(range=60.0, cooldown=0.8, windup=0.25, damage=12.0)),
    "DA_Brute": dict(name="Brute", max_hp=220.0, move_speed=220.0, radius=60.0,
                     attack=dict(range=80.0, cooldown=1.6, windup=0.5, damage=30.0)),
}

# (unit asset, team, start cell) on a 20x12 grid.
SETUP = [
    ("DA_Krijger", 0, (2, 3)),
    ("DA_Krijger", 0, (2, 6)),
    ("DA_Krijger", 0, (2, 9)),
    ("DA_Brute", 1, (17, 4)),
    ("DA_Brute", 1, (17, 8)),
]


def log(message):
    unreal.log(f"{LOG_TAG} {message}")


def load_or_create(name, asset_class):
    full_path = f"{ASSET_PATH}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        log(f"Updating {full_path}")
        return unreal.load_asset(full_path)

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, ASSET_PATH, asset_class, factory)
    if not asset:
        raise RuntimeError(f"{LOG_TAG} Could not create {full_path}")
    log(f"Created {full_path}")
    return asset


def make_tag(tag_name):
    tag = unreal.GameplayTag()
    tag.import_text(f'(TagName="{tag_name}")')
    return tag


def main():
    definitions = {}
    for asset_name, values in UNITS.items():
        definition = load_or_create(asset_name, unreal.CombatUnitDefinition)
        definition.set_editor_property("display_name", values["name"])
        definition.set_editor_property("max_hp", values["max_hp"])
        definition.set_editor_property("move_speed", values["move_speed"])
        definition.set_editor_property("radius", values["radius"])

        attack = unreal.CombatAttackDefinition()
        attack.set_editor_property("type", make_tag("Attack.Melee"))
        for key in ("range", "cooldown", "windup", "damage"):
            attack.set_editor_property(key, values["attack"][key])
        definition.set_editor_property("attacks", [attack])

        definitions[asset_name] = definition

    setup = load_or_create("DA_Setup_Test", unreal.CombatSetup)
    entries = []
    for asset_name, team, (x, y) in SETUP:
        entry = unreal.CombatSetupEntry()
        entry.set_editor_property("definition", definitions[asset_name])
        entry.set_editor_property("team", team)
        entry.set_editor_property("start_cell", unreal.IntPoint(x, y))
        entries.append(entry)
    setup.set_editor_property("units", entries)

    to_save = list(definitions.values()) + [setup]
    for asset in to_save:
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError(f"{LOG_TAG} Could not save {asset.get_path_name()}")
        log(f"Saved {asset.get_path_name()}")

    log("Done")


main()
