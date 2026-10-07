"""Creates the child animation set (UCombatAnimSet) /Game/Characters/Animations/DA_AnimSet_Child: the pack's locomotion
blend space and idle breaks (montages made from the pack's sequences), and ABP_Combat as AnimClass once it exists.
Unit looks point to it (FCombatLook::AnimSet in Data/Units.json).

Run headless with the editor closed:
    UnrealEditor-Cmd.exe BattleSystem.uproject -run=pythonscript -script="<abs path>/Scripts/CreateCharacterAnimSet.py" -unattended -nullrhi

The animations are local content (/Game/Characters is not in git), so the set is local too.
An existing set is kept (only its AnimClass is filled in when empty); FORCE_UPDATE = True fills it again.
"""

import unreal

LOG_TAG = "[CombatAnimSet]"
FORCE_UPDATE = False

ANIM_PATH = "/Game/Characters/Animations"
PACK_ANIMS = "/Game/ZZ_FAB/City_Characters/Animations/Child"
ANIM_SET = "DA_AnimSet_Child"
ANIM_CLASS = f"{ANIM_PATH}/ABP_Combat"
LOCOMOTION = f"{PACK_ANIMS}/BS_Child_Idle_Run"
IDLE_BREAKS = ["ANIM_Child_IdleLookAround", "ANIM_Child_WaveHello", "ANIM_Child_IdleLookPhone"]


def log(message):
    unreal.log(f"{LOG_TAG} {message}")


def load_or_create(name, asset_class, path):
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


make_anim_set()
log("Done")
