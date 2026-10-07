"""Creates the cue table /Game/Combat/DA_CueTable (debug colors per cue tag; VFX and sound are set in the editor).

Run headless with the editor closed:
    UnrealEditor-Cmd.exe BattleSystem.uproject -run=pythonscript -script="<abs path>/Scripts/CreateCueTable.py" -unattended -nullrhi

An existing table is kept, so cues edited in the editor stay. Set FORCE_UPDATE = True to overwrite it with the values below.
"""

import unreal

LOG_TAG = "[CombatCues]"
CUE_PATH = "/Game/Combat"
FORCE_UPDATE = False

# Cue tag -> debug color (R, G, B).
CUES = {
    "Cue.Fire": (1.0, 0.45, 0.0),
    "Cue.Cleave": (1.0, 0.1, 0.1),
    "Cue.Rally": (1.0, 0.85, 0.1),
    "Cue.Taunt": (1.0, 0.0, 1.0),
}


def log(message):
    unreal.log(f"{LOG_TAG} {message}")


def make_tag(tag_name):
    tag = unreal.GameplayTag()
    tag.import_text(f'(TagName="{tag_name}")')
    return tag


def main():
    full_path = f"{CUE_PATH}/DA_CueTable"
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        log(f"{'Updating' if FORCE_UPDATE else 'Keeping'} {full_path}")
        if not FORCE_UPDATE:
            return
        table = unreal.load_asset(full_path)
    else:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property("data_asset_class", unreal.CombatCueTable)
        table = unreal.AssetToolsHelpers.get_asset_tools().create_asset("DA_CueTable", CUE_PATH, unreal.CombatCueTable, factory)
        if not table:
            raise RuntimeError(f"{LOG_TAG} Could not create {full_path}")
        log(f"Created {full_path}")

    cues = []
    for tag_name, (r, g, b) in CUES.items():
        cue = unreal.CombatCue()
        cue.set_editor_property("cue_tag", make_tag(tag_name))
        cue.set_editor_property("debug_color", unreal.LinearColor(r, g, b, 1.0))
        cues.append(cue)
    table.set_editor_property("cues", cues)
    if not unreal.EditorAssetLibrary.save_loaded_asset(table, only_if_is_dirty=False):
        raise RuntimeError(f"{LOG_TAG} Could not save {full_path}")
    log(f"Saved {full_path}")


main()
log("Done")
