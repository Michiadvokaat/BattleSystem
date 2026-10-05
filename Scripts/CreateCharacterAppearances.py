"""Creates character looks (UCombatAppearance) in /Game/Characters/Looks from the modular child meshes,
and the child animation set (UCombatAnimSet) in /Game/Characters/Animations.

Run headless with the editor closed:
    UnrealEditor-Cmd.exe BattleSystem.uproject -run=pythonscript -script="<abs path>/Scripts/CreateCharacterAppearances.py" -unattended -nullrhi

The meshes are local content (/Game/Characters is not in git), so the looks are local too.
Only looks that do not exist yet are created and filled, so looks edited in the editor are kept.
Set FORCE_UPDATE = True to overwrite existing looks with the values below.
The animation set gets the pack's locomotion blend space and idle breaks (montages made from the pack's sequences),
and ABP_Combat as AnimClass once it exists. Looks without an AnimSet get the child set.
"""

import unreal

LOG_TAG = "[CombatLooks]"
ASSET_PATH = "/Game/Characters/Looks"
FORCE_UPDATE = False

HEROES = "/Game/Characters/Heroes"
MALE = f"{HEROES}/Meshes/Child/Male/SKM_Child_Male_"
FEMALE = f"{HEROES}/Meshes/Child/Female/SKM_Child_Female_"

ANIM_PATH = "/Game/Characters/Animations"
PACK_ANIMS = "/Game/ZZ_FAB/City_Characters/Animations/Child"
ANIM_SET = "DA_AnimSet_Child"
ANIM_CLASS = f"{ANIM_PATH}/ABP_Combat"
LOCOMOTION = f"{PACK_ANIMS}/BS_Child_Idle_Run"
IDLE_BREAKS = ["ANIM_Child_IdleLookAround", "ANIM_Child_WaveHello", "ANIM_Child_IdleLookPhone"]


def numbered(prefix, part, count):
    return [f"{prefix}{part}_{index:02d}" for index in range(1, count + 1)]


# Per look: slots of (slot tag, mesh paths, empty chance, swappable), the overrides of
# (while tag, slot tag, mesh path or None) and the shape. Hats, glasses and backpacks are swappable.
LOOKS = {
    # The same parts as BP_Melee.
    "DA_Look_Melee": dict(slots=[
        ("Slot.Body", [f"{MALE}Body_02"], 0.0, False),
        ("Slot.Shirt", [f"{MALE}Shirt_02"], 0.0, False),
        ("Slot.Pants", [f"{MALE}Shorts_02"], 0.0, False),
        ("Slot.Shoes", [f"{MALE}Shoes_06"], 0.0, False),
    ]),
    # The same parts as BP_Ranger.
    "DA_Look_Ranger": dict(slots=[
        ("Slot.Body", [f"{HEROES}/Meshes/SKM_Hero"], 0.0, False),
        ("Slot.Face", [f"{HEROES}/Ranger/SKM_Ranger_Face"], 0.0, False),
        ("Slot.Hair", [f"{HEROES}/Ranger/SKM_Ranger_Hair"], 0.0, False),
        ("Slot.Shirt", [f"{HEROES}/Ranger/SKM_Ranger_Shirt"], 0.0, False),
        ("Slot.Pants", [f"{HEROES}/Ranger/SKM_Ranger_Pants"], 0.0, False),
        ("Slot.Shoes", [f"{HEROES}/Ranger/SKM_Ranger_Shoes"], 0.0, False),
        ("Slot.Gloves", [f"{HEROES}/Ranger/SKM_Ranger_Gloves"], 0.0, False),
        ("Slot.Hat", [f"{HEROES}/Ranger/SKM_Ranger_Hat"], 0.0, True),
    ]),
    # The same parts as BP_Tank, a bit wider.
    "DA_Look_Tank": dict(width=1.15, slots=[
        ("Slot.Body", [f"{FEMALE}Body_04"], 0.0, False),
        ("Slot.Face", [f"{HEROES}/Tank/SKM_Tank_Face"], 0.0, False),
        ("Slot.Hair", [f"{HEROES}/Tank/SKM_Tank_Hair"], 0.0, False),
        ("Slot.Outwear", [f"{HEROES}/Tank/SKM_Tank_Jacket"], 0.0, False),
        ("Slot.Pants", [f"{HEROES}/Tank/SKM_Tank_Pants"], 0.0, False),
        ("Slot.Shoes", [f"{HEROES}/Tank/SKM_Tank_Shoes"], 0.0, False),
        ("Slot.Hat", [f"{HEROES}/Tank/SKM_Tank_Hat"], 0.0, True),
        ("Slot.Glasses", [f"{HEROES}/Tank/SKM_Tank_Glasses"], 0.0, True),
    ]),
    # A random boy per unit; while taunted he wears a bicycle helmet.
    "DA_Look_Child_Male_Random": dict(slots=[
        ("Slot.Body", numbered(MALE, "Body", 4), 0.0, False),
        ("Slot.Face", [f"{MALE}Face_{name}" for name in ("Usual", "Neutral", "Happy", "Angry", "Evil")], 0.0, False),
        ("Slot.Hair", numbered(MALE, "Hairstyle", 9), 0.1, False),
        ("Slot.Shirt", numbered(MALE, "Shirt", 5), 0.0, False),
        ("Slot.Outwear", numbered(MALE, "Outwear", 5), 0.6, False),
        ("Slot.Pants", numbered(MALE, "Pants", 5) + numbered(MALE, "Shorts", 2), 0.0, False),
        ("Slot.Shoes", numbered(MALE, "Shoes", 8), 0.0, False),
        ("Slot.Hat", numbered(MALE, "Hat", 5), 0.6, True),
        ("Slot.Glasses", numbered(MALE, "Glasses", 2), 0.8, True),
        ("Slot.Backpack", [f"{MALE}Backpack_01"], 0.7, True),
    ], overrides=[
        ("Status.Taunted", "Slot.Hat", f"{MALE}Bicyclist_Hat"),
    ]),
}


def log(message):
    unreal.log(f"{LOG_TAG} {message}")


def make_tag(tag_name):
    tag = unreal.GameplayTag()
    tag.import_text(f'(TagName="{tag_name}")')
    return tag


def load_mesh(path):
    mesh = unreal.load_asset(path)
    if not isinstance(mesh, unreal.SkeletalMesh):
        unreal.log_warning(f"{LOG_TAG} Missing skeletal mesh {path}, skipped")
        return None
    return mesh


def load_or_create(name, asset_class=unreal.CombatAppearance, path=ASSET_PATH):
    """Returns (asset, should_fill)."""
    full_path = f"{path}/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        log(f"{'Updating' if FORCE_UPDATE else 'Keeping'} {full_path}")
        return unreal.load_asset(full_path), FORCE_UPDATE

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, path, asset_class, factory)
    if not asset:
        raise RuntimeError(f"{LOG_TAG} Could not create {full_path}")
    log(f"Created {full_path}")
    return asset, True


def fill(look, values):
    slots = []
    for slot_tag, paths, empty_chance, swappable in values["slots"]:
        slot = unreal.CombatAppearanceSlot()
        slot.set_editor_property("slot_tag", make_tag(slot_tag))
        slot.set_editor_property("options", [mesh for mesh in (load_mesh(path) for path in paths) if mesh])
        slot.set_editor_property("empty_chance", empty_chance)
        slot.set_editor_property("swappable", swappable)
        slots.append(slot)
    look.set_editor_property("slots", slots)

    overrides = []
    for while_tag, slot_tag, path in values.get("overrides", []):
        override = unreal.CombatAppearanceOverride()
        override.set_editor_property("while_tag", make_tag(while_tag))
        override.set_editor_property("slot_tag", make_tag(slot_tag))
        override.set_editor_property("mesh", load_mesh(path) if path else None)
        overrides.append(override)
    look.set_editor_property("overrides", overrides)

    look.set_editor_property("uniform_scale", values.get("scale", 1.0))
    look.set_editor_property("width_scale", values.get("width", 1.0))
    look.set_editor_property("height_scale", values.get("height", 1.0))


def make_montage(sequence_name):
    """A montage (DefaultSlot) of a pack sequence in ANIM_PATH/Child; an existing one is kept."""
    name = sequence_name.replace("ANIM_", "AM_")
    full_path = f"{ANIM_PATH}/Child/{name}"
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        return unreal.load_asset(full_path)
    sequence = unreal.load_asset(f"{PACK_ANIMS}/{sequence_name}")
    if not sequence:
        unreal.log_warning(f"{LOG_TAG} Missing animation {PACK_ANIMS}/{sequence_name}, skipped")
        return None
    factory = unreal.AnimMontageFactory()
    factory.set_editor_property("source_animation", sequence)
    montage = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, f"{ANIM_PATH}/Child", unreal.AnimMontage, factory)
    if not montage or not unreal.EditorAssetLibrary.save_loaded_asset(montage, only_if_is_dirty=False):
        raise RuntimeError(f"{LOG_TAG} Could not create {full_path}")
    log(f"Created {full_path}")
    return montage


def make_anim_set():
    anim_set, should_fill = load_or_create(ANIM_SET, unreal.CombatAnimSet, ANIM_PATH)
    changed = should_fill
    if should_fill:
        anim_set.set_editor_property("locomotion", unreal.load_asset(LOCOMOTION))
        anim_set.set_editor_property("idle_breaks", [m for m in (make_montage(name) for name in IDLE_BREAKS) if m])
    # The AnimBP is made in the editor; fill it in as soon as it exists.
    if not anim_set.get_editor_property("anim_class") and unreal.EditorAssetLibrary.does_asset_exist(ANIM_CLASS):
        anim_set.set_editor_property("anim_class", unreal.EditorAssetLibrary.load_blueprint_class(ANIM_CLASS))
        log(f"AnimClass = {ANIM_CLASS}")
        changed = True
    if changed and not unreal.EditorAssetLibrary.save_loaded_asset(anim_set, only_if_is_dirty=False):
        raise RuntimeError(f"{LOG_TAG} Could not save {anim_set.get_path_name()}")
    return anim_set


def main():
    anim_set = make_anim_set()
    for name, values in LOOKS.items():
        look, should_fill = load_or_create(name)
        if should_fill:
            fill(look, values)
        # Also for kept looks: an AnimSet is added only where none is set.
        needs_anim_set = not look.get_editor_property("anim_set")
        if needs_anim_set:
            look.set_editor_property("anim_set", anim_set)
            log(f"{name}: AnimSet = {ANIM_SET}")
        if not should_fill and not needs_anim_set:
            continue
        if not unreal.EditorAssetLibrary.save_loaded_asset(look, only_if_is_dirty=False):
            raise RuntimeError(f"{LOG_TAG} Could not save {look.get_path_name()}")
        log(f"Saved {look.get_path_name()}")

    log("Done")


main()
