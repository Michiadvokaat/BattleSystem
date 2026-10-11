"""Exports the adult hats, clothing and other parts of the Fab packs, an adult body per pack and the target bodies to FBX, for
Blender.

Step 1 of Scripts/FitParts.ps1; run with the editor closed, in editor mode WITH a renderer (no -run=pythonscript, no
-nullrhi): the FBX exporter of skeletal meshes needs a rendered mesh and asserts on "MeshObject" without one. A window opens
briefly. The arguments are target=<Child|Adult> (TARGETS; default Child), the categories to export (CATEGORIES; none = all), pack=<prefix> to take only the packs whose
name starts with it (none = all packs) and names=<regex> to take only the parts whose source name matches it (no spaces or
commas: the arguments are split on spaces and ExecCmds on commas):
    UnrealEditor-Cmd.exe BattleSystem.uproject -unattended -nosplash -nosound -ExecCmds="py <abs path>/Scripts/ExportAdultParts.py target=Adult Outwear Pants pack=City, QUIT_EDITOR"

A pack is a folder of parts made for one adult body (PACKS): the Creative and Funny packs, and the City pack once per body
type and gender (CITY_GROUPS: Adult, Plus-Size, Senior and Teen as female and male, Pumped male), each measured on its own
body. A part's category comes from its name through the pack's rules (FAB_RULES, CITY_RULES; the first match wins): hats,
tops, pants, facewear (faces, glasses, facial hair, eyebrows, small face pieces), shoes, hair, for the City pack gloves
and body accessories, and a picked list of the Creative costumes (whole suits, their hats, shoulder pads, a bow tie and wings
that FitToHero.py fits rigidly on the back, kind "back"). Copies are named SK_Hat_<Pack>_* (Fab hats, also the costume hats), SK_<Pack>_* (other Fab parts) or SK_City_<Type>_<Gender>_*,
plus the target body's suffix.
Of a pack with colour variants (COLOR_VARIANTS) only one colour of the listed categories is taken. For facewear the export
also holds a neutral face of the pack and of each target body (the pack's "face", the target's "face"), which FitToHero.py
lines up to place the face parts.

A target is one or more bodies that every part is fitted onto (TARGETS): Child is the hero SKM_Hero (the child bodies share
its shape), into /Game/Characters/Meshes/Child/<category>; Adult is SKM_Adult_Male and SKM_Adult_Female (copies of the City
pack's adult Body_01 on SKEL_Adult_Skeleton), into /Game/Characters/Meshes/Adult/<category> with the suffix _M or _F, so
every part is fitted twice.

Writes EXPORT_DIR/<pack>/*.fbx, EXPORT_DIR/_Target_<body>.fbx and EXPORT_DIR/manifest.json, which Scripts/Blender/FitToHero.py
reads. Only LOD 0 is exported (Blender's FBX import does not keep LOD groups). The FBX files are local, outside the repo.
"""

import json
import os
import re
import shutil
import sys

import unreal

LOG_TAG = "[AdultParts]"
EXPORT_DIR = "D:/Unreal/Assets/Blender/Export"
# Category -> kind for FitToHero.py (rigid hats, skinned clothing, face parts or hair); the copies go to <target root>/<category>.
CATEGORIES = {
    "Hats": "hat",
    "Outwear": "clothing",
    "Pants": "clothing",
    "Facewear": "face",
    "Shoes": "clothing",
    "Hair": "hair",
    "Gloves": "clothing",
    "Accessories": "clothing",
    "Costumes": "clothing",
}
FACEWEAR = r"emotion|Glasses|Mustache|Beard|Eyebrow|Clown_nose|Mask|Piercing|Earrings|Pacifier|Bandage"
# (category, name pattern[, kind]): the first that matches a part's name wins; no match = not exported (bodies, ...). A kind
# overrides the category's.
FAB_RULES = [
    # The Creative costumes the user picked: whole suits (trunk and legs), their hats, shoulder pads (11_003), a bow tie
    # (14_003) and wings worn like a backpack (7_003, rigid on the back).
    ("Costumes", r"^SK_Costume_(1|2|3|4|5|6|7|9|11|12|13|14)_001$"),
    ("Hats", r"^SK_Costume_((1|2|3|4|5|7|11|12|13|14)_002|8_004|9_003)$"),
    ("Accessories", r"^SK_Costume_7_003$", "back"),
    ("Accessories", r"^SK_Costume_(11|14)_003$"),
    ("Hats", r"^SK_Hat_"),
    ("Hair", r"^SK_Hair(style)?_"),
    ("Outwear", r"Outwear|Outerwear|Outfit"),
    ("Pants", r"Pants|Shorts"),
    ("Facewear", FACEWEAR),
    ("Shoes", r"Shoe|Socks"),
]
# The City names have the profession first (SKM_Adult_Female_Chef_Outwear) and a few typos (Otwear, Putwear).
CITY_RULES = [
    ("Hair", r"Hairstyle"),
    ("Facewear", r"Hat_Beard|Hat_Mustache"),
    ("Accessories", r"Protection|Bracelet|Chain|Necklace|Watch|Belt|Stetoscope|Backpack"),
    ("Gloves", r"Gloves"),
    ("Facewear", r"Glasses|Face_|Beard|Mustache|Mask"),
    ("Hats", r"(^|_)Hat(_\d+)?$"),
    ("Shoes", r"Shoes"),
    ("Pants", r"Pants|Shorts|Skirt"),
    ("Outwear", r"Outwear|Otwear|Putwear|Shirt|Jacket|Dress|Overall|Suit|Apron|Costume|Swimsuit|Vest"),
]
CITY = "/Game/ZZ_FAB/City_Characters/Skeleton_Meshes"
# Target -> its bodies: name (unique over all targets), body mesh, the neutral face FitToHero.py lines the packs' faces up
# with, root folder of the copies and the suffix of their names.
TARGETS = {
    "Child": [
        {"name": "Hero", "mesh": "/Game/Characters/Meshes/Child/SKM_Hero",
         "face": "/Game/Characters/Meshes/Child/Male/SKM_Child_Male_Face_Neutral",
         "root": "/Game/Characters/Meshes/Child", "suffix": ""},
    ],
    "Adult": [
        {"name": "AdultMale", "mesh": "/Game/Characters/Meshes/Adult/SKM_Adult_Male",
         "face": f"{CITY}/Adult/Mail/SKM_Adult_Male_Face_Neutral",
         "root": "/Game/Characters/Meshes/Adult", "suffix": "_M"},
        {"name": "AdultFemale", "mesh": "/Game/Characters/Meshes/Adult/SKM_Adult_Female",
         "face": f"{CITY}/Adult/Femail/SKM_Adult_Female_Face_Neutral",
         "root": "/Game/Characters/Meshes/Adult", "suffix": "_F"},
    ],
}


def fab_copy_name(pack_name):
    # SK_Hat_001 -> SK_Hat_Creative_001, SK_Costume_1_002 (a hat) -> SK_Hat_Creative_Costume_1_002,
    # SK_Outwear_001 -> SK_Creative_Outwear_001
    def copy_name(name, category):
        if name.startswith("SK_Hat_"):
            return name.replace("SK_Hat_", f"SK_Hat_{pack_name}_", 1)
        return f"SK_Hat_{pack_name}_{name[3:]}" if category == "Hats" else f"SK_{pack_name}_{name[3:]}"
    return copy_name


# (type folder, type in the names, gender folder, gender in the names)
CITY_GROUPS = [
    ("Adult", "Adult", "Femail", "Female"), ("Adult", "Adult", "Mail", "Male"),
    ("Plus-Size", "PlusSize", "Femail", "Female"), ("Plus-Size", "PlusSize", "Mail", "Male"),
    ("Pumped", "Pumped", "Mail", "Male"),
    ("Senior", "Senior", "Femail", "Female"), ("Senior", "Senior", "Mail", "Male"),
    ("Teen", "Teen", "Femail", "Female"), ("Teen", "Teen", "Mail", "Male"),
]
# name, folder, body to measure, neutral face (None: the pack has none), naming rules, copy name, scale multiplier for hats
PACKS = [
    {"name": "Creative", "folder": "/Game/ZZ_FAB/Creative_Characters/Skeleton_Meshes",
     "body": "/Game/ZZ_FAB/Creative_Characters/Skeleton_Meshes/SK_Body_001",
     "face": "/Game/ZZ_FAB/Creative_Characters/Skeleton_Meshes/SK_Male_emotion_neutral_001",
     "rules": FAB_RULES, "copy_name": fab_copy_name("Creative"), "tune": 1.0},
    {"name": "Funny", "folder": "/Game/ZZ_FAB/Funny_Characters/Meshes",
     "body": "/Game/ZZ_FAB/Funny_Characters/Meshes/SK_Body_Blue_001", "face": None,
     "rules": FAB_RULES, "copy_name": fab_copy_name("Funny"), "tune": 1.0},
] + [
    {"name": f"City_{kind}_{gender}", "folder": f"{CITY}/{folder}/{gender_folder}",
     "body": f"{CITY}/{folder}/{gender_folder}/SKM_{kind}_{gender}_Body_01",
     "face": f"{CITY}/{folder}/{gender_folder}/SKM_{kind}_{gender}_Face_Neutral",
     # SKM_Adult_Female_Chef_Outwear -> SK_City_Adult_Female_Chef_Outwear
     "rules": CITY_RULES, "copy_name": lambda name, _category: f"SK_City_{name[4:]}", "tune": 1.0}
    for folder, kind, gender_folder, gender in CITY_GROUPS
]
# Packs whose clothing comes in colours (SK_<Type>_<Colour>_<Number>) -> (the one colour that is fitted, the categories it
# applies to; the Funny mustaches keep their three colours, which are hair colours).
COLOR_VARIANTS = {"Funny": ("White", ("Outwear", "Pants"))}


def log(message):
    unreal.log(f"{LOG_TAG} {message}")


def export_fbx(mesh, path):
    options = unreal.FbxExportOption()
    options.set_editor_property("ascii", False)
    options.set_editor_property("level_of_detail", False)
    options.set_editor_property("export_source_mesh", True)
    options.set_editor_property("collision", False)
    options.set_editor_property("export_morph_targets", False)
    task = unreal.AssetExportTask()
    task.set_editor_property("object", mesh)
    task.set_editor_property("filename", path)
    task.set_editor_property("exporter", unreal.SkeletalMeshExporterFBX())
    task.set_editor_property("options", options)
    task.set_editor_property("automated", True)
    task.set_editor_property("prompt", False)
    task.set_editor_property("replace_identical", True)
    if not unreal.Exporter.run_asset_export_task(task) or not os.path.isfile(path):
        raise RuntimeError(f"{LOG_TAG} Could not export {mesh.get_path_name()} to {path}")


def category_of(name, rules):
    """(category, kind override or None) of the first rule that matches the name; (None, None) without a match."""
    return next(((rule[0], rule[2] if len(rule) > 2 else None) for rule in rules if re.search(rule[1], name)), (None, None))


def wanted_colour(name, pack_name, category):
    """False for a colour variant other than the pack's chosen colour (COLOR_VARIANTS) in the categories it applies to."""
    colour, categories = COLOR_VARIANTS.get(pack_name, (None, ()))
    match = re.match(r"^SK_[A-Za-z]+_(.+)_\d+$", name)
    return colour is None or category not in categories or not match or match.group(1) == colour


def main():
    arguments = [a for a in sys.argv[1:] if a]
    target_names = [a[len("target="):] for a in arguments if a.startswith("target=")] or ["Child"]
    if len(target_names) != 1 or target_names[0] not in TARGETS:
        raise RuntimeError(f"{LOG_TAG} Unknown target {target_names}; known: {list(TARGETS)}")
    bodies = TARGETS[target_names[0]]
    prefixes = [a[len("pack="):] for a in arguments if a.startswith("pack=")]
    names = [a[len("names="):] for a in arguments if a.startswith("names=")]
    categories = [a for a in arguments if not a.startswith(("target=", "pack=", "names="))] or list(CATEGORIES)
    unknown = [c for c in categories if c not in CATEGORIES]
    if unknown:
        raise RuntimeError(f"{LOG_TAG} Unknown categories {unknown}; known: {list(CATEGORIES)}")
    packs_wanted = [p for p in PACKS if not prefixes or any(p["name"].startswith(x) for x in prefixes)]
    if not packs_wanted:
        raise RuntimeError(f"{LOG_TAG} No pack starts with {prefixes}; packs: {[p['name'] for p in PACKS]}")
    log(f"Target {target_names[0]} ({', '.join(b['name'] for b in bodies)}); categories: {', '.join(categories)}; "
        f"packs: {', '.join(p['name'] for p in packs_wanted)}"
        + (f"; names matching {', '.join(names)}" if names else ""))
    # Every run starts from an empty folder, so parts removed from a pack do not linger.
    if os.path.isdir(EXPORT_DIR):
        shutil.rmtree(EXPORT_DIR)
    os.makedirs(EXPORT_DIR)

    targets = []
    for body in bodies:
        target = {"name": body["name"], "body": f"_Target_{body['name']}.fbx", "mesh": body["mesh"]}
        export_fbx(unreal.load_asset(body["mesh"]), f"{EXPORT_DIR}/{target['body']}")
        if "Facewear" in categories:
            target["face"] = f"_TargetFace_{body['name']}.fbx"
            export_fbx(unreal.load_asset(body["face"]), f"{EXPORT_DIR}/{target['face']}")
        targets.append(target)
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    lib = unreal.EditorAssetLibrary
    packs = []
    count = 0
    for source in packs_wanted:
        pack_name, folder = source["name"], source["folder"]
        os.makedirs(f"{EXPORT_DIR}/{pack_name}")
        export_fbx(unreal.load_asset(source["body"]), f"{EXPORT_DIR}/{pack_name}/_Body.fbx")
        parts = []
        for data in sorted(registry.get_assets_by_path(folder, recursive=False), key=lambda d: str(d.asset_name)):
            if str(data.asset_class_path.asset_name) != "SkeletalMesh":
                continue
            name = str(data.asset_name)
            category, kind_override = category_of(name, source["rules"])
            if category not in categories or not wanted_colour(name, pack_name, category):
                continue
            if names and not any(re.search(pattern, name) for pattern in names):
                continue
            kind = kind_override or CATEGORIES[category]
            copy_name = source["copy_name"](name, category)
            export_fbx(unreal.load_asset(f"{folder}/{name}"), f"{EXPORT_DIR}/{pack_name}/{name}.fbx")
            # Per target body the asset path of its copy.
            parts.append({"kind": kind, "fbx": f"{pack_name}/{name}.fbx", "source": f"{folder}/{name}",
                          "targets": {b["name"]: f"{b['root']}/{category}/{copy_name}{b['suffix']}" for b in bodies}})
        pack = {"name": pack_name, "body": f"{pack_name}/_Body.fbx", "tune": source["tune"], "parts": parts}
        if "Facewear" in categories and source["face"]:
            if lib.does_asset_exist(source["face"]):
                export_fbx(unreal.load_asset(source["face"]), f"{EXPORT_DIR}/{pack_name}/_Face.fbx")
                pack["face"] = f"{pack_name}/_Face.fbx"
            else:
                log(f"{pack_name}: no neutral face {source['face']}; its face parts are lined up on the heads")
        packs.append(pack)
        count += len(parts)
        log(f"{pack_name}: {len(parts)} parts")

    manifest = {"targets": targets, "packs": packs}
    with open(f"{EXPORT_DIR}/manifest.json", "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent="\t")
    log(f"{count} parts in {EXPORT_DIR}")
    log("Done")


main()
