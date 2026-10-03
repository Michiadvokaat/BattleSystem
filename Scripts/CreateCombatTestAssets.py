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
    # Melee with 3x threat, plus a taunt around itself (6 m) that makes enemies target it for 4 s.
    "DA_Tank": dict(name="Tank", max_hp=400.0, move_speed=250.0, radius=55.0, attacks=[
        dict(type="Attack.Melee", range=70.0, cooldown=1.2, windup=0.3, damage=12.0, threat_multiplier=3.0),
        dict(type="Attack.Taunt", range=600.0, cooldown=6.0, windup=0.2, damage=0.0, effects=[
            dict(effect_tag="Effect.Taunt", duration=4.0, stacking="REFRESH", granted_tags=["Status.Taunted"]),
        ]),
    ]),
    # Fireball on the target (telegraphed 0.6 s) that also slows, plus a weak staff hit.
    "DA_Magier": dict(name="Magier", max_hp=80.0, move_speed=280.0, radius=35.0, attacks=[
        dict(type="Attack.AoE", area_shape="CIRCLE_AT_TARGET", range=500.0, area_radius=150.0, telegraph_delay=0.6,
             cooldown=3.0, windup=0.4, damage=25.0, requires_line_of_sight=True, impact_cue="Cue.Fire", effects=[
                 dict(effect_tag="Effect.Slow", duration=2.0, stacking="REFRESH", move_speed_multiplier=0.5),
             ]),
        dict(type="Attack.Melee", range=50.0, cooldown=1.0, windup=0.2, damage=4.0),
    ]),
    # 100 degree cleave in front of it.
    "DA_Bijlman": dict(name="Bijlman", max_hp=180.0, move_speed=240.0, radius=50.0, attacks=[
        dict(type="Attack.AoE", area_shape="CONE", range=70.0, area_radius=160.0, cone_angle=100.0,
             cooldown=1.5, windup=0.4, damage=20.0, requires_line_of_sight=False, impact_cue="Cue.Cleave"),
    ]),
    # Allies-only aura: +25% damage for 3 s; and a light melee hit.
    "DA_Vaandeldrager": dict(name="Vaandeldrager", max_hp=120.0, move_speed=280.0, radius=40.0, attacks=[
        dict(type="Attack.AoE", area_shape="CIRCLE_AROUND_SELF", area_radius=400.0, cooldown=4.0, windup=0.2,
             damage=0.0, affects_enemies=False, affects_allies=True, requires_line_of_sight=False, impact_cue="Cue.Rally",
             effects=[dict(effect_tag="Effect.Rally", duration=3.0, stacking="REFRESH", damage_dealt_multiplier=1.25)]),
        dict(type="Attack.Melee", range=50.0, cooldown=1.0, windup=0.2, damage=8.0),
    ]),
}

# Player abilities (no cooldown, the player triggers them). Added to these units if they have none yet,
# also when the asset already exists, so tuned values elsewhere in the asset are kept.
PLAYER_ABILITIES = {
    "DA_Tank": [
        dict(type="Attack.Taunt", range=400.0, cooldown=0.0, windup=0.0, damage=0.0, impact_cue="Cue.Taunt", effects=[
            dict(effect_tag="Effect.Taunt", duration=3.0, stacking="REFRESH", granted_tags=["Status.Taunted"]),
        ]),
    ],
    "DA_Krijger": [
        dict(type="Attack.Taunt", range=300.0, cooldown=0.0, windup=0.0, damage=0.0, impact_cue="Cue.Taunt", effects=[
            dict(effect_tag="Effect.Taunt", duration=2.0, stacking="REFRESH", granted_tags=["Status.Taunted"]),
        ]),
    ],
}

# Command scripts: (tick, unit ID, "MOVE", (x, y)) or (tick, unit ID, "ABILITY", ability index).
# Unit IDs are the indices of the setup's entries (DA_Setup_Taunt: 0 tank, 1-2 archers, 3-5 Brutes).
COMMAND_SCRIPTS = {
    "DA_Script_TauntDemo": [
        (20, 1, "MOVE", (10, 2)),
        (20, 2, "MOVE", (10, 10)),
        (40, 0, "ABILITY", 0),
        (100, 0, "ABILITY", 0),
    ],
}

# Cue tag -> debug color (R, G, B). VFX and sound can be set in the editor later.
CUES = {
    "Cue.Fire": (1.0, 0.45, 0.0),
    "Cue.Cleave": (1.0, 0.1, 0.1),
    "Cue.Rally": (1.0, 0.85, 0.1),
    "Cue.Taunt": (1.0, 0.0, 1.0),
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
    # Phase 4 check: the outer Brutes go for the archers on the flanks until the tank taunts them.
    "DA_Setup_Taunt": [
        ("DA_Tank", 0, (10, 6)),
        ("DA_Boogschutter", 0, (13, 1)),
        ("DA_Boogschutter", 0, (13, 11)),
        ("DA_Brute", 1, (17, 2)),
        ("DA_Brute", 1, (17, 6)),
        ("DA_Brute", 1, (17, 10)),
    ],
    # Phase 5 check: a mage's telegraphed fireball, a cleaving axeman and a rallying banner bearer.
    "DA_Setup_AoE": [
        ("DA_Krijger", 0, (11, 4)),
        ("DA_Krijger", 0, (11, 6)),
        ("DA_Krijger", 0, (11, 8)),
        ("DA_Vaandeldrager", 0, (9, 6)),
        ("DA_Boogschutter", 0, (8, 3)),
        ("DA_Magier", 1, (18, 6)),
        ("DA_Bijlman", 1, (16, 5)),
        ("DA_Brute", 1, (16, 8)),
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


def make_tag_container(tag_names):
    container = unreal.GameplayTagContainer()
    tags = ",".join(f'(TagName="{name}")' for name in tag_names)
    container.import_text(f"(GameplayTags=({tags}))")
    return container


def make_effect(values):
    effect = unreal.CombatEffectDefinition()
    for key, value in values.items():
        if key == "effect_tag":
            value = make_tag(value)
        elif key in ("granted_tags", "blocked_by_tags"):
            value = make_tag_container(value)
        elif key == "stacking":
            value = getattr(unreal.CombatEffectStacking, value)
        effect.set_editor_property(key, value)
    return effect


def make_cue_table(should_fill, table):
    if not should_fill:
        return
    cues = []
    for tag_name, (r, g, b) in CUES.items():
        cue = unreal.CombatCue()
        cue.set_editor_property("cue_tag", make_tag(tag_name))
        cue.set_editor_property("debug_color", unreal.LinearColor(r, g, b, 1.0))
        cues.append(cue)
    table.set_editor_property("cues", cues)


def make_attack(attack_values):
    attack = unreal.CombatAttackDefinition()
    attack.set_editor_property("type", make_tag(attack_values["type"]))
    for key, value in attack_values.items():
        if key == "effects":
            attack.set_editor_property("effects", [make_effect(effect) for effect in value])
        elif key == "impact_cue":
            attack.set_editor_property(key, make_tag(value))
        elif key == "area_shape":
            attack.set_editor_property(key, getattr(unreal.CombatAreaShape, value))
        elif key != "type":
            attack.set_editor_property(key, value)
    return attack


def make_command(tick, unit_id, kind, argument):
    command = unreal.CombatCommand()
    command.set_editor_property("tick", tick)
    command.set_editor_property("unit_id", unit_id)
    command.set_editor_property("type", getattr(unreal.CombatCommandType, kind))
    if kind == "MOVE":
        command.set_editor_property("target_cell", unreal.IntPoint(*argument))
    else:
        command.set_editor_property("ability_index", argument)
    return command


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

        definition.set_editor_property("attacks", [make_attack(values) for values in values["attacks"]])
        to_save.append(definition)

    for asset_name, abilities in PLAYER_ABILITIES.items():
        definition = definitions[asset_name]
        if len(definition.get_editor_property("player_abilities")) == 0:
            definition.set_editor_property("player_abilities", [make_attack(values) for values in abilities])
            log(f"Added player abilities to {asset_name}")
            if definition not in to_save:
                to_save.append(definition)

    for script_name, commands in COMMAND_SCRIPTS.items():
        script, should_fill = load_or_create(script_name, unreal.CombatCommandScript)
        if should_fill:
            script.set_editor_property("commands", [make_command(*command) for command in commands])
            to_save.append(script)

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

    cue_table, should_fill = load_or_create("DA_CueTable", unreal.CombatCueTable)
    make_cue_table(should_fill, cue_table)
    if should_fill:
        to_save.append(cue_table)

    for asset in to_save:
        if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
            raise RuntimeError(f"{LOG_TAG} Could not save {asset.get_path_name()}")
        log(f"Saved {asset.get_path_name()}")

    log("Done")


main()
