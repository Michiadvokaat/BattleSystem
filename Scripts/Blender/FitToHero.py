"""Fits adult parts (FBX exported from UE) to the hero (child) skeleton SKEL_Hero, in Blender.

Run headless (step 2 of Scripts/FitChildParts.ps1):
    blender.exe -b --factory-startup -P <abs path>/Scripts/Blender/FitToHero.py -- <export dir> <fitted dir>

Reads <export dir>/manifest.json, written by Scripts/ExportAdultParts.py: the hero mesh, and per pack an adult body and its
parts. Every part is imported, rebound to the hero's armature and written to <fitted dir>/<name>.fbx; the list of written
files goes to <fitted dir>/fitted.json for Scripts/ImportFittedParts.py.

Hats ("kind": "hat") are rigid: all vertices get weight 1 on the Head bone. They are scaled around the adult head bone by the
ratio of the hero's head to the pack's adult head (width and depth of the vertices above the Neck bone), times the pack's
"tune", and moved onto the hero's head so the hat sits as high and as far forward on it as on the adult head.

Clothing ("kind": "clothing") keeps its own weights and is moved from the adult's rest pose to the hero's per bone
(retarget_maps): each bone's region is turned along the hero's bone, stretched to its length and scaled across by the ratio
of the two bodies there (front, back and side to side apart; body_extents), blended by the clothing's weights. Vertices that
end up inside the hero's body (or closer than CLOTH_MARGIN) are pushed out along the nearest face's normal, and weights on
bones the hero lacks move to their nearest ancestor it has. Finally the vertices near the body take the body's weights at the
nearest point (transfer_weights), so the clothing bends like the skin under it instead of poking through at the joints; loose
parts further away (hoods, hems) keep their own.

Every part also gets the hide zones of the hero's body it covers ("hide_zones" in fitted.json, a bit mask over
BODY_ZONES; covered_zones), and body_zones.json lists the hero's skin vertices with their zone for
Scripts/CreateBodyZones.py, which writes the zones into the bodies' vertex colors.

All measuring happens in Blender's world space after the FBX import, so the units and axes of the import cancel out.
"""

import json
import os
import sys

import bpy
from mathutils import Matrix, Vector
from mathutils.bvhtree import BVHTree
from mathutils.interpolate import poly_3d_calc

LOG_TAG = "[FitToHero]"
HEAD_BONE = "Head"
NECK_BONE = "Neck"
# The root bone of the UE skeletons; Blender's FBX import makes it the armature object.
ROOT_NAME = "Root"
# The first bone under the root; clothing vertices without weights follow it.
HIPS_BONE = "Hips"
# Children a bone with several children points at (the spine goes on through them).
SPINE_CHAIN = ("Spine", "Spine1", "Spine2", "Neck", "Head")
# The trunk, Hips up to the Neck: stretched as one piece, because the skeletons split it differently (the hero's Hips->Spine is
# 2.3 cm), so stretching each bone apart squeezed the waist and the overlap of tops and pants.
TRUNK_BONES = ("Hips", "Spine", "Spine1", "Spine2")
# Clothing: a bone's girth ratio (hero body / adult body) needs this many body vertices, and is clamped to this range.
MIN_GIRTH_VERTICES = 8
EXTENT_PERCENTILE = 0.8
# Clothing: a girth ratio needs both bodies to reach at least this far (m) from the bone.
MIN_EXTENT = 0.01
GIRTH_RANGE = (0.3, 1.5)
# Clothing: how far (m) every vertex stays outside the hero's body.
CLOTH_MARGIN = 0.006
# Clothing: vertices up to WEIGHT_NEAR (m) from the hero's body take the body's weights there, so they bend like the skin
# under them; from WEIGHT_FAR on (hoods, hems, skirts) they keep their own; in between the two are blended.
WEIGHT_NEAR = 0.02
WEIGHT_FAR = 0.06
# Clothing: at most this many bones per vertex, and smaller weights are dropped.
MAX_INFLUENCES = 8
MIN_WEIGHT = 0.01
# Hide zones of the hero's body, in the bit order of ECombatBodyZone (CombatHideZones.h); the two must match.
BODY_ZONES = ("Neck", "Collar", "Chest", "Waist", "Pelvis", "UpperArmL", "UpperArmR", "ForeArmL", "ForeArmR", "HandL", "HandR",
              "ThighL", "ThighR", "ShinL", "ShinR", "FootL", "FootR", "Crown", "BackOfHead", "Ears")
# The head is one bone, so its zones come from the shape, as fractions of the head (its vertices) from the Head bone:
# the crown above HEAD_CROWN of the height to the top, the back of the head behind HEAD_BACK of the depth to the back,
# the ears beyond HEAD_EARS of the half width (below the crown). The face never gets a zone.
HEAD_CROWN = 0.55
# The trunk (Chest, Waist, Pelvis by bone) is cut in bands by height, as fractions from the Hips to the Neck bone: Pelvis
# below TRUNK_PELVIS (in the overlap of a top's hem and the pants' waist), Collar above TRUNK_COLLAR (the neck opening).
TRUNK_PELVIS = 0.105
TRUNK_COLLAR = 0.9
HEAD_BACK = 0.25
HEAD_EARS = 0.8
# A clothing piece covers a zone when the rays out of at least COVER_FRACTION of the zone's skin vertices (along their
# normals) hit it within COVER_DISTANCE (m); for the limbs COVER_FRACTION_LIMBS, because the skin a sleeve or trouser leg
# misses there is mostly out of sight (the inner thighs at the crotch), while a trunk or head zone hidden too early shows a
# hole.
COVER_DISTANCE = 0.08
COVER_FRACTION = 0.95
COVER_FRACTION_LIMBS = 0.9
LIMB_ZONES = ("UpperArmL", "UpperArmR", "ForeArmL", "ForeArmR", "HandL", "HandR", "ThighL", "ThighR", "ShinL", "ShinR",
              "FootL", "FootR")


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


def rebind(mesh, armature, points):
    """Parents the mesh to the armature with a fresh Armature modifier; points are the new world positions of its vertices."""
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


def main_child(bone):
    """The child a bone points at: its only child, or the one that continues the spine; None for an end bone."""
    if len(bone.children) == 1:
        return bone.children[0]
    for name in SPINE_CHAIN:
        if name in bone.children:
            return bone.children[name]
    return None


def bone_segments(armature, keep=None):
    """Bone name -> (head, head of the first bone down its main chain that is in keep, or None), in world space.

    keep (default: all bones) skips bones the other skeleton lacks, so a segment spans the same body part on both
    (the Funny pack's Spine1 runs through its Spine2 to the Neck, like the hero's Spine1)."""
    segments = {}
    for bone in armature.data.bones:
        child = main_child(bone)
        while child is not None and keep is not None and child.name not in keep:
            child = main_child(child)
        segments[bone.name] = (armature.matrix_world @ bone.head_local,
                               armature.matrix_world @ child.head_local if child else None)
    return segments


def dominant_bones(mesh):
    """For each vertex the name of the group with the largest weight (None without weights)."""
    names = {g.index: g.name for g in mesh.vertex_groups}
    result = []
    for v in mesh.data.vertices:
        best = max(v.groups, key=lambda g: g.weight, default=None)
        result.append(names.get(best.group) if best and best.weight > 0.0 else None)
    return result


def cross_axes(direction):
    """Two axes across a bone direction: front to back (world Y made square to it), and side to side."""
    depth = Vector((0.0, 1.0, 0.0))
    if abs(direction.dot(depth)) > 0.9:
        depth = Vector((0.0, 0.0, 1.0))
    depth = (depth - direction * direction.dot(depth)).normalized()
    return depth, direction.cross(depth)


def offset_from_segment(p, head, tail):
    if tail is None:
        return p - head
    axis = tail - head
    t = max(0.0, min(1.0, (p - head).dot(axis) / axis.length_squared))
    return p - (head + t * axis)


def percentile(values, fraction):
    if len(values) < MIN_GIRTH_VERTICES:
        return None
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(fraction * len(ordered)))]


def body_extents(armature, meshes, keep=None):
    """Bone name -> (plus, minus, width): how far the body's vertices that follow that bone most reach from the bone's
    segment, along +depth and -depth (the spine lies at the back of the body, so the belly reaches further than the back)
    and side to side; the EXTENT_PERCENTILE of the distances, None with too few vertices. An end bone gets its plain
    distance three times. A percentile instead of the median, because the vertices of short bones lie mostly near the bone."""
    segments = bone_segments(armature, keep)
    offsets = {}
    for mesh in meshes:
        for v, bone in zip(mesh.data.vertices, dominant_bones(mesh)):
            if bone in segments:
                offsets.setdefault(bone, []).append(offset_from_segment(mesh.matrix_world @ v.co, *segments[bone]))
    extents = {}
    for bone, values in offsets.items():
        head, tail = segments[bone]
        if tail is None:
            distance = percentile([o.length for o in values], EXTENT_PERCENTILE)
            extents[bone] = (distance, distance, distance)
            continue
        depth, width = cross_axes((tail - head).normalized())
        along = [o.dot(depth) for o in values]
        extents[bone] = (percentile([a for a in along if a > 0.0], EXTENT_PERCENTILE),
                         percentile([-a for a in along if a < 0.0], EXTENT_PERCENTILE),
                         percentile([abs(o.dot(width)) for o in values], EXTENT_PERCENTILE))
    return extents


def girth_ratios(adult_extents, hero_extents):
    """Bone name -> (plus, minus, width) ratios of the hero's body to the adult's, clamped to GIRTH_RANGE; None where
    either body reaches less than MIN_EXTENT (too few or too central vertices to tell)."""
    def ratio(hero, adult):
        if hero is None or adult is None or hero < MIN_EXTENT or adult < MIN_EXTENT:
            return None
        return max(GIRTH_RANGE[0], min(GIRTH_RANGE[1], hero / adult))
    return {bone: tuple(ratio(h, a) for h, a in zip(hero_extents[bone], extent))
            for bone, extent in adult_extents.items() if bone in hero_extents}


def hierarchy(armature):
    """The bones, every parent before its children."""
    order = []
    stack = [b for b in armature.data.bones if b.parent is None]
    while stack:
        bone = stack.pop()
        order.append(bone)
        stack.extend(bone.children)
    return order


class BoneMap:
    """Moves points that follow one bone from the adult's rest pose to the hero's: relative to the adult bone's head, in the
    frame (along the bone, depth, width), scaled per axis (depth by side), turned onto the hero's bone, at the hero's head."""

    def __init__(self, adult_head, hero_head, turn, axes, scales):
        self.adult_head, self.hero_head, self.turn, self.axes, self.scales = adult_head, hero_head, turn, axes, scales

    def __call__(self, p):
        along, depth, width = self.axes
        stretch, plus, minus, side = self.scales
        q = p - self.adult_head
        d = q.dot(depth)
        local = along * (q.dot(along) * stretch) + depth * (d * (plus if d > 0.0 else minus)) + width * (q.dot(width) * side)
        return self.hero_head + self.turn @ local


def girth_of(name, girths, adult_armature, index):
    """One girth ratio of a bone, else of its nearest ancestor that has it, else down its main child chain, else 1."""
    bone = adult_armature.data.bones[name]
    for candidate in [bone] + list(bone.parent_recursive):
        ratio = girths.get(candidate.name, (None, None, None))[index]
        if ratio is not None:
            return ratio
    child = main_child(bone)
    while child is not None:
        ratio = girths.get(child.name, (None, None, None))[index]
        if ratio is not None:
            return ratio
        child = main_child(child)
    return 1.0


def retarget_maps(adult_armature, hero_armature, girths):
    """Bone name -> BoneMap from the adult's rest pose onto the hero's.

    A bone with a main child is turned along the hero's bone, stretched to its length and scaled across by its girth ratios
    (the trunk bones all use the segment Hips to Neck, see TRUNK_BONES);
    an end bone keeps its parent's turn and scales around its own head; a bone the hero lacks (the packs' hand prop bones,
    the Funny pack's Spine2) follows its parent, and a root bone (the Funny pack has Root as a bone, the hero as the
    armature object) stays where it is."""
    hero_names = set(hero_armature.data.bones.keys())
    adult = bone_segments(adult_armature, hero_names)
    hero = bone_segments(hero_armature)
    trunk = {bone: (bone_world(adult_armature, HIPS_BONE), bone_world(adult_armature, NECK_BONE),
                    bone_world(hero_armature, HIPS_BONE), bone_world(hero_armature, NECK_BONE))
             for bone in TRUNK_BONES if bone in hero_names}
    identity_axes = (Vector((0.0, 0.0, 1.0)), Vector((0.0, 1.0, 0.0)), Vector((-1.0, 0.0, 0.0)))
    maps = {}
    for bone in hierarchy(adult_armature):
        parent = maps.get(bone.parent.name) if bone.parent else None
        a_head, a_tail = adult[bone.name]
        c_head, c_tail = hero.get(bone.name, (None, None))
        if bone.name in trunk:
            # The trunk bones share one segment, Hips to Neck, anchored at the hips; only their girth differs.
            a_head, a_tail, c_head, c_tail = trunk[bone.name]
        if c_head is None:
            maps[bone.name] = parent or BoneMap(Vector(), Vector(), Matrix.Identity(3), identity_axes, (1.0, 1.0, 1.0, 1.0))
        elif a_tail is not None and c_tail is not None:
            a_dir, c_dir = (a_tail - a_head), (c_tail - c_head)
            stretch = c_dir.length / a_dir.length
            a_dir.normalize()
            c_dir.normalize()
            ratios = tuple(girth_of(bone.name, girths, adult_armature, i) for i in range(3))
            maps[bone.name] = BoneMap(a_head, c_head, a_dir.rotation_difference(c_dir).to_matrix(),
                                      (a_dir,) + cross_axes(a_dir), (stretch,) + ratios)
        elif parent is not None:
            maps[bone.name] = BoneMap(a_head, c_head, parent.turn, parent.axes, parent.scales)
        else:
            girth = girth_of(bone.name, girths, adult_armature, 2)
            maps[bone.name] = BoneMap(a_head, c_head, Matrix.Identity(3), identity_axes, (girth,) * 4)
    return maps


def skin_points(mesh, maps, root):
    """The mesh's world positions moved by its own weights over the bone maps (linear blend skinning)."""
    names = {g.index: g.name for g in mesh.vertex_groups}
    points = []
    for v in mesh.data.vertices:
        p = mesh.matrix_world @ v.co
        weights = [(names[g.group], g.weight) for g in v.groups if g.weight > 0.0 and names.get(g.group) in maps]
        total = sum(w for _, w in weights)
        if total <= 0.0:
            points.append(maps[root](p))
            continue
        moved = Vector((0.0, 0.0, 0.0))
        for name, w in weights:
            moved += (w / total) * maps[name](p)
        points.append(moved)
    return points


def vertex_weights(mesh):
    """Per vertex a dict bone name -> weight."""
    names = {g.index: g.name for g in mesh.vertex_groups}
    return [{names[g.group]: g.weight for g in v.groups if g.weight > 0.0 and g.group in names} for v in mesh.data.vertices]


def body_region(bone):
    """The part of the body a bone moves: Left/RightArm (arm, forearm, hand, fingers), Left/RightLeg, else Trunk."""
    for side in ("Left", "Right"):
        if bone.startswith(side):
            rest = bone[len(side):]
            if rest.startswith(("Arm", "ForeArm", "Hand")):
                return side + "Arm"
            if rest.startswith(("UpLeg", "Leg", "Foot", "Toe")):
                return side + "Leg"
    return "Trunk"


def dominant(weights):
    return max(weights.items(), key=lambda item: item[1])[0] if weights else None


class BodySurface:
    """The hero's body in world space: BVH trees over its faces (all, and per body region), with their corners' positions
    and weights."""

    def __init__(self, meshes):
        self.points, self.polys, self.weights = [], [], []
        for mesh in meshes:
            base = len(self.points)
            self.points.extend(mesh.matrix_world @ v.co for v in mesh.data.vertices)
            self.polys.extend([base + i for i in p.vertices] for p in mesh.data.polygons)
            self.weights.extend(vertex_weights(mesh))
        self.tree = BVHTree.FromPolygons(self.points, self.polys)
        # A face belongs to the region of the bone that moves most of its corners.
        by_region = {}
        for index, corners in enumerate(self.polys):
            regions = [body_region(dominant(self.weights[c]) or HIPS_BONE) for c in corners]
            by_region.setdefault(max(set(regions), key=regions.count), []).append(index)
        self.regions = {region: (BVHTree.FromPolygons(self.points, [self.polys[i] for i in faces]), faces)
                        for region, faces in by_region.items()}

    def nearest(self, p, region=None):
        """(location, normal, face index, distance) of the nearest point, on the faces of region when it has any."""
        if region in self.regions:
            tree, faces = self.regions[region]
            location, normal, index, distance = tree.find_nearest(p)
            if location is not None:
                return location, normal, faces[index], distance
        return self.tree.find_nearest(p)

    def weights_at(self, location, index):
        """The body's weights at a point on face index, interpolated over the face's corners."""
        corners = self.polys[index]
        factors = poly_3d_calc([self.points[i] for i in corners], location)
        result = {}
        for corner, factor in zip(corners, factors):
            for bone, weight in self.weights[corner].items():
                result[bone] = result.get(bone, 0.0) + factor * weight
        return result


def push_out(points, surface, margin):
    """Moves the points that are inside the body, or closer to it than margin, out along the nearest face's normal."""
    pushed = 0
    result = []
    for p in points:
        location, normal, _index, _distance = surface.tree.find_nearest(p)
        if location is not None and (p - location).dot(normal) < margin:
            p = location + normal * margin
            pushed += 1
        result.append(p)
    return result, pushed


def merge_missing_groups(mesh, adult_armature, hero_armature):
    """Moves the weights of groups whose bone the hero lacks onto the nearest ancestor (in the adult skeleton) it has."""
    bones = hero_armature.data.bones
    for group in list(mesh.vertex_groups):
        if group.name in bones:
            continue
        ancestor = adult_armature.data.bones.get(group.name)
        while ancestor is not None and ancestor.name not in bones:
            ancestor = ancestor.parent
        if ancestor is None:
            raise RuntimeError(f"{LOG_TAG} {mesh.name}: group {group.name} has no bone or ancestor on the hero")
        target = ancestor.name
        into = mesh.vertex_groups.get(target) or mesh.vertex_groups.new(name=target)
        for v in mesh.data.vertices:
            for g in v.groups:
                if g.group == group.index and g.weight > 0.0:
                    into.add([v.index], g.weight, "ADD")
        mesh.vertex_groups.remove(group)


def transfer_weights(mesh, surface):
    """Gives the vertices near the body the body's weights at the nearest point of the same body part (body_region of their
    own main bone; WEIGHT_NEAR..WEIGHT_FAR blends them with their own), so the clothing bends like the skin under it;
    returns how many took the body's weights fully."""
    own = vertex_weights(mesh)
    blended = []
    full = 0
    for v, weights in zip(mesh.data.vertices, own):
        # Only the skin of the vertex's own body part: a sleeve at the armpit is nearer to the trunk, but bends with the arm.
        region = body_region(dominant(weights)) if weights else None
        location, _normal, index, distance = surface.nearest(mesh.matrix_world @ v.co, region)
        body = max(0.0, min(1.0, (WEIGHT_FAR - distance) / (WEIGHT_FAR - WEIGHT_NEAR))) if location is not None else 0.0
        if body >= 1.0:
            full += 1
        mixed = {bone: (1.0 - body) * w for bone, w in weights.items()}
        if body > 0.0:
            for bone, w in surface.weights_at(location, index).items():
                mixed[bone] = mixed.get(bone, 0.0) + body * w
        kept = sorted(((w, bone) for bone, w in mixed.items() if w >= MIN_WEIGHT), reverse=True)[:MAX_INFLUENCES]
        total = sum(w for w, _ in kept)
        blended.append({bone: w / total for w, bone in kept} if total > 0.0 else weights)
    mesh.vertex_groups.clear()
    groups = {}
    for v, weights in zip(mesh.data.vertices, blended):
        for bone, w in weights.items():
            if bone not in groups:
                groups[bone] = mesh.vertex_groups.new(name=bone)
            groups[bone].add([v.index], w, "REPLACE")
    return full


def fit_clothing(mesh, armature, hero, girths, surface):
    """Moves the clothing from the adult's rest pose to the hero's (its own weights over the per-bone retarget), pushes it
    out of the hero's body, rebinds it to the hero's armature and gives it the body's weights where it lies on the body.
    Returns (vertices pushed out, vertices with the body's weights)."""
    points = skin_points(mesh, retarget_maps(armature, hero, girths), HIPS_BONE)
    points, pushed = push_out(points, surface, CLOTH_MARGIN)
    merge_missing_groups(mesh, armature, hero)
    rebind(mesh, hero, points)
    return pushed, transfer_weights(mesh, surface)


def bone_zone(bone):
    """The hide zone of the skin that follows a bone most (index into BODY_ZONES), None for the head (by shape) and
    unknown bones. Trunk zones are cut by height afterwards (body_zones)."""
    side = "L" if bone.startswith("Left") else "R" if bone.startswith("Right") else ""
    rest = bone[4:] if side == "L" else bone[5:] if side == "R" else bone
    if not side:
        name = {"Neck": "Neck", "Spine1": "Chest", "Spine2": "Chest", "Spine": "Waist", "Hips": "Pelvis"}.get(bone)
    elif rest == "Shoulder":
        name = "Chest"
    elif rest.startswith("ForeArm"):
        name = "ForeArm" + side
    elif rest.startswith("Arm"):
        name = "UpperArm" + side
    elif rest.startswith("Hand"):
        name = "Hand" + side
    elif rest.startswith("UpLeg"):
        name = "Thigh" + side
    elif rest.startswith("Leg"):
        name = "Shin" + side
    elif rest.startswith(("Foot", "Toe")):
        name = "Foot" + side
    else:
        name = None
    return BODY_ZONES.index(name) if name else None


def body_zones(armature, meshes):
    """Per mesh, the hide zone of every vertex (index into BODY_ZONES, -1 for the face): by its main bone, on the trunk
    by height (TRUNK_PELVIS, TRUNK_COLLAR), on the head by its place on the head (HEAD_CROWN, HEAD_BACK, HEAD_EARS; the
    character faces -Y)."""
    head = bone_world(armature, HEAD_BONE)
    hips_z = bone_world(armature, HIPS_BONE).z
    trunk_height = bone_world(armature, NECK_BONE).z - hips_z
    trunk = {BODY_ZONES.index(name) for name in ("Collar", "Chest", "Waist", "Pelvis")}
    pelvis, collar = BODY_ZONES.index("Pelvis"), BODY_ZONES.index("Collar")
    mains = [dominant_bones(mesh) for mesh in meshes]
    on_head = [mesh.matrix_world @ v.co for mesh, bones in zip(meshes, mains)
               for v, bone in zip(mesh.data.vertices, bones) if bone == HEAD_BONE]
    top = max(p.z for p in on_head)
    back = max(p.y for p in on_head)
    half_width = max(abs(p.x - head.x) for p in on_head)
    crown, ears, back_of_head = BODY_ZONES.index("Crown"), BODY_ZONES.index("Ears"), BODY_ZONES.index("BackOfHead")
    result = []
    for mesh, bones in zip(meshes, mains):
        zones = []
        for v, bone in zip(mesh.data.vertices, bones):
            if bone != HEAD_BONE:
                zone = bone_zone(bone) if bone else None
                if zone in trunk:
                    height = ((mesh.matrix_world @ v.co).z - hips_z) / trunk_height
                    zone = pelvis if height < TRUNK_PELVIS else collar if height > TRUNK_COLLAR else zone
                    # Waist skin below the pelvis line, or chest skin, stays as it is; the hips' skin above it is waist.
                    if zone == pelvis and height >= TRUNK_PELVIS:
                        zone = BODY_ZONES.index("Waist")
            else:
                p = mesh.matrix_world @ v.co
                if p.z > head.z + HEAD_CROWN * (top - head.z):
                    zone = crown
                elif abs(p.x - head.x) > HEAD_EARS * half_width:
                    zone = ears
                elif p.y > head.y + HEAD_BACK * (back - head.y):
                    zone = back_of_head
                else:
                    zone = None
            zones.append(-1 if zone is None else zone)
        result.append(zones)
    return result


def zone_coverage(part, body_meshes, zones):
    """Per zone (BODY_ZONES) the fraction of its skin vertices whose ray out of the skin, along the vertex normal, hits the
    part within COVER_DISTANCE."""
    tree = BVHTree.FromPolygons([part.matrix_world @ v.co for v in part.data.vertices],
                                [list(p.vertices) for p in part.data.polygons])
    hits, totals = [0] * len(BODY_ZONES), [0] * len(BODY_ZONES)
    for mesh, mesh_zones in zip(body_meshes, zones):
        to_world = mesh.matrix_world
        turn = to_world.to_3x3()
        for v, zone in zip(mesh.data.vertices, mesh_zones):
            if zone < 0:
                continue
            totals[zone] += 1
            normal = (turn @ v.normal).normalized()
            location, *_ = tree.ray_cast(to_world @ v.co + normal * 0.001, normal, COVER_DISTANCE)
            if location is not None:
                hits[zone] += 1
    return [hits[z] / totals[z] if totals[z] else 0.0 for z in range(len(BODY_ZONES))]


def covered_zones(part, body_meshes, zones):
    """The zones (bit mask over BODY_ZONES) the part covers: at least COVER_FRACTION of their skin (zone_coverage), of a
    limb COVER_FRACTION_LIMBS."""
    coverage = zone_coverage(part, body_meshes, zones)
    return sum(1 << z for z, fraction in enumerate(coverage)
               if fraction >= (COVER_FRACTION_LIMBS if BODY_ZONES[z] in LIMB_ZONES else COVER_FRACTION))


def zone_names(mask):
    return [name for z, name in enumerate(BODY_ZONES) if mask & (1 << z)]


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
    hero_extents = body_extents(hero, hero_meshes)
    hero_surface = BodySurface(hero_meshes)
    hero_zones = body_zones(hero, hero_meshes)
    for m in hero_meshes:
        m.hide_set(True)

    fitted = []
    for pack in manifest["packs"]:
        body, body_meshes, body_others = import_fbx(os.path.join(export_dir, pack["body"]))
        adult = measure_head(body, body_meshes)
        scale, offset = fit_head(child, adult, pack.get("tune", 1.0))
        girths = girth_ratios(body_extents(body, body_meshes, set(hero.data.bones.keys())), hero_extents)
        delete([body] + body_meshes + body_others)
        log(f"{pack['name']}: girth ratios (front, back, width) "
            + ", ".join(f"{b} " + "/".join("-" if r is None else f"{r:.2f}" for r in g) for b, g in sorted(girths.items())))
        log(f"{pack['name']}: scale {scale:.3f} around the adult head bone, then offset {tuple(round(x, 4) for x in offset)} "
            f"from the hero's head bone")

        def place(p, adult_head=adult[0], scale=scale, offset=offset):
            return child[0] + scale * (p - adult_head) + offset

        for part in pack["parts"]:
            if part["kind"] not in ("hat", "clothing"):
                raise RuntimeError(f"{LOG_TAG} {part['name']}: unknown kind {part['kind']}")
            armature, meshes, others = import_fbx(os.path.join(export_dir, part["fbx"]))
            if len(meshes) != 1:
                raise RuntimeError(f"{LOG_TAG} {part['name']}: expected one mesh, found {len(meshes)}")
            mesh = meshes[0]
            if part["kind"] == "hat":
                rebind(mesh, hero, [place(mesh.matrix_world @ v.co) for v in mesh.data.vertices])
                weight_rigid(mesh, HEAD_BONE)
            else:
                pushed, skin = fit_clothing(mesh, armature, hero, girths, hero_surface)
                log(f"{part['name']}: {len(mesh.data.vertices)} vertices, {pushed} pushed out of the body, "
                    f"{skin} with the body's weights")
            delete([armature] + others)
            mesh.name = part["name"]
            hide_zones = covered_zones(mesh, hero_meshes, hero_zones)
            if hide_zones:
                log(f"{part['name']}: hides {', '.join(zone_names(hide_zones))}")
            path = os.path.join(fitted_dir, part["name"] + ".fbx")
            export_fbx(path, hero, mesh)
            delete([mesh])
            fitted.append({"name": part["name"], "fbx": part["name"] + ".fbx", "source": part["source"], "target": part["target"],
                           "hide_zones": hide_zones})

    with open(os.path.join(fitted_dir, "fitted.json"), "w", encoding="utf-8") as f:
        json.dump({"parts": fitted}, f, indent="\t")
    # The hero's skin vertices with their zone, for Scripts/CreateBodyZones.py (Blender world space, m).
    points = [[round(c, 5) for c in mesh.matrix_world @ v.co] + [zone]
              for mesh, zones in zip(hero_meshes, hero_zones) for v, zone in zip(mesh.data.vertices, zones)]
    with open(os.path.join(fitted_dir, "body_zones.json"), "w", encoding="utf-8") as f:
        json.dump({"zones": list(BODY_ZONES), "points": points}, f)
    log(f"{len(fitted)} parts in {fitted_dir}")
    log("Done")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        log(f"ERROR {error}")
        raise
