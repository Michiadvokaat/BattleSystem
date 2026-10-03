"""Creates the combat test assets (units and setups) in /Game/Combat.

Run headless with the editor closed:
    UnrealEditor-Cmd.exe BattleSystem.uproject -run=pythonscript -script="<abs path>/Scripts/CreateCombatTestAssets.py" -unattended -nullrhi

Only assets that do not exist yet are created and filled, so values tuned in the editor are kept.
Set FORCE_UPDATE = True to overwrite existing assets with the values below.
"""

import unreal

LOG_TAG = "[CombatAssets]"
ASSET_PATH = "/Game/Combat"
FORCE_UPDATE = False

UNITS = {
    "DA_Krijger": dict(name="Krijger", max_hp=100.0, move_speed=350.0, radius=40.0, attacks=[
        dict(type="Attack.Melee", range=60.0, cooldown=0.8, windup=0.25, damage=12.0),
    ]),
    "DA_Brute": dict(name="Brute", max_hp=220.0, move_speed=220.0, radius=60.0, attacks=[
        dict(type="Attack.Melee", range=80.0, cooldown=1.6, windup=0.5, damage=30.0),
    ]),
    "DA_Boogschutter": dict(name="Boogschutter", max_hp=70.0, move_speed=300.0, radius=35.0, attacks=[
        dict(type="Attack.Ranged", range=600.0, cooldown=1.2, windup=0.3, damage=14.0,
             projectile_speed=1500.0, requires_line_of_sight=True),
        dict(type="Attack.Melee", range=50.0, cooldown=1.0, windup=0.2, damage=5.0),
    ]),
    # Stands still and never attacks: a target for checks.
    "DA_Doelpop": dict(name="Doelpop", max_hp=300.0, move_speed=0.0, radius=40.0, attacks=[]),
}

# Setup asset -> [(unit asset, team, start cell)], on a 20x12 grid.
SETUPS = {
    "DA_Setup_Test": [
        ("DA_Krijger", 0, (2, 3)),
        ("DA_Krijger", 0, (2, 6)),
        ("DA_Krijger", 0, (2, 9)),
        ("DA_Brute", 1, (17, 4)),
        ("DA_Brute", 1, (17, 8)),
    ],
    # Phase 2 check, with a wall at x = 6, y = 0..8 in Arena-01: the Krijger must go for B (3,10),
    # which is farther as the crow flies but reachable sooner than A (9,2) behind the wall.
    "DA_Setup_Wall": [
        ("DA_Krijger", 0, (3, 2)),
        ("DA_Brute", 1, (9, 2)),
        ("DA_Brute", 1, (3, 10)),
    ],
    # Phase 3 check: the archer at (3,2) must walk around the wall before it can see and shoot the dummy.
    "DA_Setup_Archer": [
        ("DA_Boogschutter", 0, (3, 2)),
        ("DA_Doelpop", 1, (9, 2)),
    ],
    "DA_Setup_Mixed": [
        ("DA_Krijger", 0, (2, 3)),
        ("DA_Krijger", 0, (2, 7)),
        ("DA_Boogschutter", 0, (1, 5)),
        ("DA_Boogschutter", 0, (1, 9)),
        ("DA_Brute", 1, (17, 4)),
        ("DA_Brute", 1, (17, 8)),
    ],
}


def log(message):
    unreal.log(f"{LOG_TAG} {message}")


def load_or_create(name, asset_class):
    """Returns (asset, should_fill)."""
    full_path = f"{ASSET_PATH}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        log(f"{'Updating' if FORCE_UPDATE else 'Keeping'} {full_path}")
        return unreal.load_asset(full_path), FORCE_UPDATE

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, ASSET_PATH, asset_class, factory)
    if not asset:
        raise RuntimeError(f"{LOG_TAG} Could not create {full_path}")
    log(f"Created {full_path}")
    return asset, True


def make_tag(tag_name):
    tag = unreal.GameplayTag()
    tag.import_text(f'(TagName="{tag_name}")')
    return tag


def main():
    definitions = {}
    to_save = []
    for asset_name, values in UNITS.items():
        definition, should_fill = load_or_create(asset_name, unreal.CombatUnitDefinition)
        definitions[asset_name] = definition
        if not should_fill:
            continue

        definition.set_editor_property("display_name", values["name"])
        definition.set_editor_property("max_hp", values["max_hp"])
        definition.set_editor_property("move_speed", values["move_speed"])
        definition.set_editor_property("radius", values["radius"])

        attacks = []
        for attack_values in values["attacks"]:
            attack = unreal.CombatAttackDefinition()
            attack.set_editor_property("type", make_tag(attack_values["type"]))
            for key, value in attack_values.items():
                if key != "type":
                    attack.set_editor_property(key, value)
            attacks.append(attack)
        definition.set_editor_property("attacks", attacks)
        to_save.append(definition)

    for setup_name, lineup in SETUPS.items():
        setup, should_fill = load_or_create(setup_name, unreal.CombatSetup)
        if not should_fill:
            continue

        entries = []
        for asset_name, team, (x, y) in lineup:
            entry = unreal.CombatSetupEntry()
            entry.set_editor_property("definition", definitions[asset_name])
            entry.set_editor_property("team", team)
            entry.set_editor_property("start_cell", unreal.IntPoint(x, y))
            entries.append(entry)
        setup.set_editor_property("units", entries)
        to_save.append(setup)

    for asset in to_save:
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError(f"{LOG_TAG} Could not save {asset.get_path_name()}")
        log(f"Saved {asset.get_path_name()}")

    log("Done")


main()
