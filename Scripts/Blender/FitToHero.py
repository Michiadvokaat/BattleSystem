"""Fits adult parts (FBX exported from UE) to the hero (child) skeleton SKEL_Hero, in Blender.

Run headless (step 2 of Scripts/FitChildHats.ps1):
    blender.exe -b --factory-startup -P <abs path>/Scripts/Blender/FitToHero.py -- <export dir> <fitted dir>

Reads <export dir>/manifest.json, written by Scripts/ExportAdultParts.py: the hero mesh, and per pack an adult body and its
parts. Every part is imported, rebound to the hero's armature and written to <fitted dir>/<name>.fbx; the list of written
files goes to <fitted dir>/fitted.json for Scripts/ImportFittedParts.py.

Hats ("kind": "hat") are rigid: all vertices get weight 1 on the Head bone. They are scaled around the adult head bone by the
ratio of the hero's head to the pack's adult head (width and depth of the vertices above the Neck bone), times the pack's
"tune", and moved onto the hero's head so the hat sits as high and as far forward on it as on the adult head. All measuring
happens in Blender's world space after the FBX import, so the units and axes of the import cancel out.
"""

import json
import os
import sys

import bpy
from mathutils import Matrix, Vector

LOG_TAG = "[FitToHero]"
HEAD_BONE = "Head"
NECK_BONE = "Neck"
# The root bone of the UE skeletons; Blender's FBX import makes it the armature object.
ROOT_NAME = "Root"


def log(message):
    print(f"{LOG_TAG} {message}", flush=True)


def import_fbx(path):
    """Imports one FBX; returns (armature, meshes, others) of the new objects."""
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=path, use_anim=False)
    new = [o for o in bpy.data.objects if o not in before]
    armatures = [o for o in new if o.type == "ARMATURE"]
    if len(armatures) != 1:
        raise RuntimeError(f"{LOG_TAG} {path}: expected one armature, found {len(armatures)}")
    meshes = [o for o in new if o.type == "MESH"]
    others = [o for o in new if o.type not in ("ARMATURE", "MESH")]
    return armatures[0], meshes, others


def bone_world(armature, bone):
    return armature.matrix_world @ armature.data.bones[bone].head_local


def measure_head(armature, meshes):
    """Head bone, and the box (low, high) of the vertices above the neck, in world space."""
    head = bone_world(armature, HEAD_BONE)
    neck = bone_world(armature, NECK_BONE)
    points = [m.matrix_world @ v.co for m in meshes for v in m.data.vertices]
    points = [p for p in points if p.z > neck.z]
    low = Vector((min(p.x for p in points), min(p.y for p in points), min(p.z for p in points)))
    high = Vector((max(p.x for p in points), max(p.y for p in points), max(p.z for p in points)))
    return head, low, high


def fit_head(child, adult, tune):
    """Scale, and the offset from the hero's head bone, so the adult head's box lands on the hero's."""
    (child_head, child_low, child_high) = child
    (adult_head, adult_low, adult_high) = adult
    scale = 0.5 * ((child_high.x - child_low.x) / (adult_high.x - adult_low.x)
                   + (child_high.y - child_low.y) / (adult_high.y - adult_low.y)) * tune
    # Relative to the head bone: the middle of the head front to back, and its top.
    child_mid_y = 0.5 * (child_low.y + child_high.y) - child_head.y
    adult_mid_y = 0.5 * (adult_low.y + adult_high.y) - adult_head.y
    offset = Vector((0.0, child_mid_y - scale * adult_mid_y, (child_high.z - child_head.z) - scale * (adult_high.z - adult_head.z)))
    return scale, offset


def delete(objects):
    for o in objects:
        bpy.data.objects.remove(o, do_unlink=True)


def rebind(mesh, armature, place):
    """Parents the mesh to the armature with a fresh Armature modifier; place maps each old world position to the new one."""
    points = [place(mesh.matrix_world @ v.co) for v in mesh.data.vertices]
    mesh.parent = armature
    mesh.matrix_parent_inverse = Matrix.Identity(4)
    mesh.matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()
    to_local = mesh.matrix_world.inverted()
    for v, p in zip(mesh.data.vertices, points):
        v.co = to_local @ p
    mesh.data.update()
    for modifier in list(mesh.modifiers):
        if modifier.type == "ARMATURE":
            mesh.modifiers.remove(modifier)
    mesh.modifiers.new("Armature", "ARMATURE").object = armature


def weight_rigid(mesh, bone):
    mesh.vertex_groups.clear()
    group = mesh.vertex_groups.new(name=bone)
    group.add(range(len(mesh.data.vertices)), 1.0, "REPLACE")


def export_fbx(path, armature, mesh):
    bpy.ops.object.select_all(action="DESELECT")
    armature.select_set(True)
    mesh.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.export_scene.fbx(filepath=path, use_selection=True, object_types={"ARMATURE", "MESH"}, add_leaf_bones=False,
                             bake_anim=False, use_armature_deform_only=False, mesh_smooth_type="FACE")


def import_hero(path):
    armature, meshes, others = import_fbx(path)
    # Same shape as an export of the hero from Blender by hand: the armature on top, named after the root bone.
    world = armature.matrix_world.copy()
    armature.parent = None
    armature.matrix_world = world
    delete(others)
    armature.name = ROOT_NAME
    return armature, meshes


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    export_dir, fitted_dir = argv[0], argv[1]
    with open(os.path.join(export_dir, "manifest.json"), encoding="utf-8") as f:
        manifest = json.load(f)
    os.makedirs(fitted_dir, exist_ok=True)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    hero, hero_meshes = import_hero(os.path.join(export_dir, manifest["hero"]))
    child = measure_head(hero, hero_meshes)
    log(f"Hero head bone {tuple(round(x, 4) for x in child[0])}, head box {tuple(round(x, 4) for x in child[2] - child[1])}")
    for m in hero_meshes:
        m.hide_set(True)

    fitted = []
    for pack in manifest["packs"]:
        body, body_meshes, body_others = import_fbx(os.path.join(export_dir, pack["body"]))
        adult = measure_head(body, body_meshes)
        scale, offset = fit_head(child, adult, pack.get("tune", 1.0))
        delete([body] + body_meshes + body_others)
        log(f"{pack['name']}: scale {scale:.3f} around the adult head bone, then offset {tuple(round(x, 4) for x in offset)} "
            f"from the hero's head bone")

        def place(p, adult_head=adult[0], scale=scale, offset=offset):
            return child[0] + scale * (p - adult_head) + offset

        for part in pack["parts"]:
            if part["kind"] != "hat":
                raise RuntimeError(f"{LOG_TAG} {part['name']}: unknown kind {part['kind']}")
            armature, meshes, others = import_fbx(os.path.join(export_dir, part["fbx"]))
            if len(meshes) != 1:
                raise RuntimeError(f"{LOG_TAG} {part['name']}: expected one mesh, found {len(meshes)}")
            mesh = meshes[0]
            rebind(mesh, hero, place)
            weight_rigid(mesh, HEAD_BONE)
            delete([armature] + others)
            mesh.name = part["name"]
            path = os.path.join(fitted_dir, part["name"] + ".fbx")
            export_fbx(path, hero, mesh)
            delete([mesh])
            fitted.append({"name": part["name"], "fbx": part["name"] + ".fbx", "source": part["source"], "target": part["target"]})

    with open(os.path.join(fitted_dir, "fitted.json"), "w", encoding="utf-8") as f:
        json.dump({"parts": fitted}, f, indent="\t")
    log(f"{len(fitted)} parts in {fitted_dir}")
    log("Done")


try:
    main()
except Exception as error:
    log(f"ERROR {error}")
    raise
