#!/usr/bin/env python3
"""Render a rigged avatar (GLB, e.g. exported from 360sona.com) into the sprites
Ex-Box 360 shows on My Xbox, the gamercard and the Avatar screen.

Run with Blender's Python module (pip install bpy):

    python render_avatar.py avatar.glb OUTDIR [--quick] [--samples N] [--size 540x900]

OUTDIR (new or empty) receives:
    avatar_idle.frames   a 15 fps loop, the avatar standing and breathing
    avatar_wave.frames   a wave (when the rig has a right arm), else a nod
    avatar_turn.frames   36 frames, one full turn, for the Avatar screen
    avatar_full.png      one still, the size of the sprites
    avatar_head.png      256x256 head shot on the NXE green, used as a gamerpic
    avatar.txt           sizes, frame counts and which bones drove the motion

Sprites are transparent PNGs packed like the trailers: "NXEF", the frame count,
then each frame as a 4-byte little-endian length and its PNG bytes. If the GLB
carries its own animations, the first one plays as the idle loop instead of the
procedural one. Nothing here runs on the console.
SPDX-License-Identifier: GPL-3.0-or-later
"""
import argparse
import math
import os
import struct
import sys
import tempfile

import bpy
from mathutils import Euler, Matrix, Quaternion, Vector

FPS = 15


def log(*parts):
    print("avatar:", *parts, flush=True)


# ---- scene -----------------------------------------------------------------
def fresh_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene
    scene.render.fps = FPS
    return scene


def import_glb(path):
    before = set(bpy.data.objects)
    bpy.ops.import_scene.gltf(filepath=path)
    objects = [o for o in bpy.data.objects if o not in before]
    armatures = [o for o in objects if o.type == "ARMATURE"]
    # The importer adds shapes for drawing bones; they are not the avatar.
    shapes = {pb.custom_shape for a in armatures for pb in a.pose.bones if pb.custom_shape}
    for shape in shapes:
        shape.hide_render = True
    meshes = [o for o in objects if o.type == "MESH" and o not in shapes and not o.hide_render]
    if not meshes:
        raise SystemExit("no mesh in " + path)
    return objects, meshes, (armatures[0] if armatures else None)


def world_bounds(meshes):
    deps = bpy.context.evaluated_depsgraph_get()
    lo = Vector((1e9, 1e9, 1e9))
    hi = Vector((-1e9, -1e9, -1e9))
    for m in meshes:
        ev = m.evaluated_get(deps)
        mesh = ev.to_mesh()
        for v in mesh.vertices:
            p = ev.matrix_world @ v.co
            lo = Vector(map(min, lo, p))
            hi = Vector(map(max, hi, p))
        ev.to_mesh_clear()
    return lo, hi


def find_bone(arm, *patterns, side=None):
    """First bone whose lower-cased name contains every word of one pattern,
    on the requested side ('r' or 'l') when given."""
    if not arm:
        return None
    names = [b.name for b in arm.pose.bones]

    def on_side(name):
        n = name.lower().replace(":", "_").replace(".", "_").replace("-", "_").replace(" ", "_")
        if side is None:
            return True
        words = {"r": ("right", "_r_", "_r", "r_"), "l": ("left", "_l_", "_l", "l_")}[side]
        other = {"r": ("left",), "l": ("right",)}[side]
        if any(o in n for o in other):
            return False
        return any(n.endswith(w) or n.startswith(w) or w in n for w in words)

    for pattern in patterns:
        for name in names:
            low = name.lower()
            if all(word in low for word in pattern.split()) and on_side(name):
                return arm.pose.bones[name]
    return None


def setup_render(scene, width, height, samples):
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = samples
    scene.cycles.use_denoising = True
    scene.render.film_transparent = True
    scene.render.resolution_x = width
    scene.render.resolution_y = height
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.image_settings.color_mode = "RGBA"
    scene.view_settings.view_transform = "Standard"
    world = bpy.data.worlds.new("NXE")
    world.use_nodes = True
    bg = world.node_tree.nodes["Background"]
    bg.inputs[0].default_value = (0.82, 0.88, 0.80, 1.0)   # soft green-grey sky light
    bg.inputs[1].default_value = 0.35
    scene.world = world


def add_light(name, kind, energy, location, target, size=1.0, color=(1, 1, 1)):
    data = bpy.data.lights.new(name, kind)
    data.energy = energy
    data.color = color
    if kind == "AREA":
        data.size = size
    obj = bpy.data.objects.new(name, data)
    bpy.context.scene.collection.objects.link(obj)
    obj.location = location
    direction = Vector(target) - Vector(location)
    obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    return obj


def add_camera(scene, lens=50):
    cam = bpy.data.cameras.new("Camera")
    cam.lens = lens
    obj = bpy.data.objects.new("Camera", cam)
    scene.collection.objects.link(obj)
    scene.camera = obj
    return obj


def aim(obj, eye, target):
    obj.location = eye
    obj.rotation_euler = (Vector(target) - Vector(eye)).to_track_quat("-Z", "Y").to_euler()


def frame_body(cam, lo, hi, width, height):
    """Camera in front (-Y), a touch above the waist, the whole body in frame."""
    centre = (lo + hi) / 2
    tall = hi.z - lo.z
    wide = max(hi.x - lo.x, 1e-3)
    fov_v = 2 * math.atan(cam.data.sensor_width / 2 / cam.data.lens) * (height / width if width < height else 1)
    # Blender fits the sensor to the larger dimension; for a tall frame that is the height.
    fov_v = 2 * math.atan((cam.data.sensor_width / 2) / cam.data.lens)
    fov_h = fov_v * width / height
    need = max(tall * 1.12 / 2 / math.tan(fov_v / 2), wide * 1.25 / 2 / math.tan(fov_h / 2))
    target = Vector((centre.x, centre.y, lo.z + tall * 0.52))
    aim(cam, target + Vector((0, -need, tall * 0.06)), target)
    return target, need


# ---- motion ----------------------------------------------------------------
BASE = {}   # bone name -> rest-correcting rotation (arms down from a T-pose)


def clear_motion(arm, objects):
    for o in objects:
        o.animation_data_clear()
    if arm:
        for pb in arm.pose.bones:
            pb.rotation_mode = "QUATERNION"
            pb.rotation_quaternion = BASE.get(pb.name, Quaternion())
            pb.location = Vector((0, 0, 0))


def rest_direction(arm, pb):
    """Which way the limb really runs at rest, in world space: from this joint to
    the next one down the chain (bone axes from an importer need not follow the
    limb), plus the bone's rest basis for turning world rotations into pose ones."""
    m = arm.matrix_world @ pb.bone.matrix_local
    start = arm.matrix_world @ pb.bone.head_local
    child = max(pb.bone.children, key=lambda c: (c.head_local - pb.bone.head_local).length, default=None)
    end = arm.matrix_world @ (child.head_local if child else pb.bone.tail_local)
    run = end - start
    if run.length < 1e-6:
        run = m.to_3x3() @ Vector((0, 1, 0))
    return run.normalized(), m.to_3x3()


def turn_toward(arm, pb, world_dir):
    """The bone's pose rotation that points it along world_dir (parents at rest)."""
    rest, basis = rest_direction(arm, pb)
    world_dir = world_dir.normalized()
    axis = rest.cross(world_dir)
    if axis.length < 1e-6:
        return Quaternion()
    angle = rest.angle(world_dir)
    local_axis = (basis.inverted() @ axis).normalized()
    return Quaternion(local_axis, angle)


def arms_down(arm):
    """A rig exported in a T-pose would stand with its arms out: bring them to its sides."""
    BASE.clear()
    done = []
    for side, sign in (("r", -1), ("l", 1)):
        upper = find_bone(arm, "upper arm", "upperarm", "arm", "shoulder", side=side)
        if not upper:
            continue
        rest, _ = rest_direction(arm, upper)
        if abs(rest.z) > 0.6:
            continue   # already hanging down
        out = 1 if rest.x > 0 else -1
        BASE[upper.name] = turn_toward(arm, upper, Vector((0.28 * out, 0.02, -1.0)))
        done.append(upper.name)
    return done


def key_bones(arm, frame, poses):
    for pb, rot in poses:
        extra = rot if isinstance(rot, Quaternion) else Euler(rot).to_quaternion()
        pb.rotation_quaternion = BASE.get(pb.name, Quaternion()) @ extra
        pb.keyframe_insert("rotation_quaternion", frame=frame)


def procedural_idle(arm, root, frames):
    spine = find_bone(arm, "spine", "chest", "torso", "back")
    head = find_bone(arm, "head", "neck")
    used = []
    for f in range(frames + 1):
        t = f / frames * 2 * math.pi
        poses = []
        if spine:
            poses.append((spine, (0.025 * math.sin(t), 0, 0.02 * math.sin(t + 0.6))))
        if head:
            poses.append((head, (0.03 * math.sin(t + 1.2), 0, 0.05 * math.sin(t * 0.5))))
        if poses:
            key_bones(arm, f + 1, poses)
        else:
            root.rotation_euler.z = 0.03 * math.sin(t)
            root.keyframe_insert("rotation_euler", frame=f + 1)
    used = [b.name for b in (spine, head) if b]
    return used or ["(whole model)"]


def procedural_wave(arm, root, frames):
    upper = find_bone(arm, "upper arm", "upperarm", "arm", "shoulder", side="r")
    lower = find_bone(arm, "fore arm", "forearm", "lower arm", "lowerarm", "elbow", side="r")
    if not upper:
        procedural_idle(arm, root, frames)
        return ["(no right arm found: idle instead)"]
    rest, _ = rest_direction(arm, upper)
    out = 1 if rest.x > 0 else -1
    down = BASE.get(upper.name, Quaternion())
    up = turn_toward(arm, upper, Vector((0.55 * out, -0.15, 0.85)))
    for f in range(frames + 1):
        t = f / frames
        lift = math.sin(min(t, 0.18) / 0.18 * math.pi / 2) if t < 0.82 else math.cos((t - 0.82) / 0.18 * math.pi / 2)
        lift = min(1.0, max(0.0, lift))
        swing = math.sin(t * 6 * math.pi) * 0.45 * lift
        upper.rotation_quaternion = down.slerp(up, lift)
        upper.keyframe_insert("rotation_quaternion", frame=f + 1)
        if lower:
            lower.rotation_quaternion = Quaternion((0, 0, 1), swing) @ Quaternion((1, 0, 0), -0.5 * lift)
            lower.keyframe_insert("rotation_quaternion", frame=f + 1)
    return [b.name for b in (upper, lower) if b]


def own_animation(arm, objects):
    """The GLB's first animation, when it brought one: (action, frame range)."""
    actions = list(bpy.data.actions)
    if not actions:
        return None
    action = actions[0]
    owner = arm or objects[0]
    owner.animation_data_create()
    owner.animation_data.action = action
    start, end = (int(x) for x in action.frame_range)
    return action.name, start, end


# ---- output ----------------------------------------------------------------
def render_frames(scene, first, last, folder, step=1):
    paths = []
    for f in range(first, last + 1, step):
        scene.frame_set(f)
        bpy.context.view_layer.update()   # render the pose just set, not the last one
        path = os.path.join(folder, "%05d.png" % f)
        scene.render.filepath = path
        bpy.ops.render.render(write_still=True)
        paths.append(path)
    return paths


def pack(paths, out):
    with open(out + ".part", "wb") as w:
        w.write(b"NXEF" + struct.pack("<I", len(paths)))
        for p in paths:
            data = open(p, "rb").read()
            w.write(struct.pack("<I", len(data)))
            w.write(data)
    os.replace(out + ".part", out)


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else sys.argv[1:]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("glb")
    ap.add_argument("out")
    ap.add_argument("--size", default="540x900")
    ap.add_argument("--samples", type=int, default=24)
    ap.add_argument("--quick", action="store_true", help="few frames and samples, to check framing")
    a = ap.parse_args(argv)
    width, height = (int(x) for x in a.size.lower().split("x"))
    os.makedirs(a.out, exist_ok=True)
    if any(n.endswith((".frames", ".png")) for n in os.listdir(a.out)):
        raise SystemExit("OUTDIR already has sprites: " + a.out)
    samples = 6 if a.quick else a.samples
    idle_n, wave_n, turn_n = (8, 8, 6) if a.quick else (48, 40, 36)

    scene = fresh_scene()
    objects, meshes, arm = import_glb(a.glb)
    root = next((o for o in objects if o.parent is None), objects[0])
    log("imported", len(meshes), "meshes;", "armature", arm.name if arm else "none",
        "with", len(arm.pose.bones) if arm else 0, "bones")
    if arm:
        log("bones:", ", ".join(b.name for b in arm.pose.bones)[:1500])
    lowered = arms_down(arm) if arm else []
    if lowered:
        log("arms lowered from a T-pose:", ", ".join(lowered))
    clear_motion(arm, objects)
    own = own_animation(arm, objects)
    if own:
        scene.frame_set(own[1])
    lo, hi = world_bounds(meshes)
    log("bounds", tuple(round(v, 3) for v in lo), tuple(round(v, 3) for v in hi))
    setup_render(scene, width, height, samples)
    cam = add_camera(scene)
    target, dist = frame_body(cam, lo, hi, width, height)
    tall = hi.z - lo.z
    add_light("Key", "AREA", 150 * tall * tall, target + Vector((-1.2, -1.6, 1.2)) * tall, target, size=tall)
    add_light("Fill", "AREA", 45 * tall * tall, target + Vector((1.4, -1.2, 0.3)) * tall, target, size=tall)
    add_light("Rim", "AREA", 160 * tall * tall, target + Vector((0.4, 1.6, 1.0)) * tall, target, size=tall * 0.6,
              color=(0.85, 1.0, 0.8))

    info = ["size %dx%d" % (width, height), "fps %d" % FPS]
    with tempfile.TemporaryDirectory() as tmp:
        # Idle: the GLB's own animation, else breathing.
        if own:
            name, start, end = own
            step = max(1, (end - start) // idle_n)
            paths = render_frames(scene, start, end, os.path.join(tmp, "idle"), step)
            info.append("idle: animation %s" % name)
        else:
            clear_motion(arm, objects)
            info.append("idle: " + ", ".join(procedural_idle(arm, root, idle_n)))
            paths = render_frames(scene, 1, idle_n, os.path.join(tmp, "idle"))
        pack(paths, os.path.join(a.out, "avatar_idle.frames"))
        info.append("idle frames %d" % len(paths))
        # Still: the first idle frame.
        os.replace(paths[0], os.path.join(a.out, "avatar_full.png"))

        clear_motion(arm, objects)
        info.append("wave: " + ", ".join(procedural_wave(arm, root, wave_n)))
        paths = render_frames(scene, 1, wave_n, os.path.join(tmp, "wave"))
        pack(paths, os.path.join(a.out, "avatar_wave.frames"))
        info.append("wave frames %d" % len(paths))

        # Turntable: the camera circles the avatar.
        clear_motion(arm, objects)
        scene.frame_set(1)
        bpy.context.view_layer.update()
        paths = []
        for i in range(turn_n):
            ang = i / turn_n * 2 * math.pi
            eye = target + Vector((math.sin(ang) * dist, -math.cos(ang) * dist, tall * 0.06))
            aim(cam, eye, target)
            bpy.context.view_layer.update()
            scene.render.filepath = os.path.join(tmp, "turn", "%05d.png" % i)
            bpy.ops.render.render(write_still=True)
            paths.append(scene.render.filepath)
        pack(paths, os.path.join(a.out, "avatar_turn.frames"))
        info.append("turn frames %d" % len(paths))

        # Head shot: square, tight on the head, on the NXE green.
        head = find_bone(arm, "head")
        if head:
            hp = arm.matrix_world @ head.head
            top = max(hi.z, hp.z + tall * 0.12)
            centre = Vector((hp.x, hp.y, (hp.z + top) / 2 - tall * 0.01))
        else:
            centre = Vector((target.x, target.y, hi.z - tall * 0.09))
        aim(cam, centre + Vector((0, -tall * 0.62, tall * 0.02)), centre)
        bpy.context.view_layer.update()
        scene.render.resolution_x = scene.render.resolution_y = 256
        scene.render.film_transparent = False
        scene.world.node_tree.nodes["Background"].inputs[0].default_value = (0.36, 0.62, 0.12, 1.0)
        scene.world.node_tree.nodes["Background"].inputs[1].default_value = 1.0
        scene.render.filepath = os.path.join(a.out, "avatar_head.png")
        bpy.ops.render.render(write_still=True)
    with open(os.path.join(a.out, "avatar.txt"), "w") as f:
        f.write("\n".join(info) + "\n")
    log("done:", "; ".join(info))


if __name__ == "__main__":
    main()
