"""Export the pointer-free static knob bodies from the THM Blender source.

Run with Blender, never with the application Python:

  blender -b assets/analog/blender_render/THM_hardware_components.blend \\
    -P tools/render_knob_frames.py -- \\
    --output assets/analog/02_KNOBS/frames

The exporter writes one transparent ``00.png`` per knob family.  It does not
save or modify the .blend file.  The app owns the sole live pointer in
``RotaryKnob``; baking a pointer into this asset would create a second needle.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import bpy
from mathutils import Vector


KNOB_FAMILIES = {
    "main": {
        "prefix": "KnobMain_",
    },
    "small": {
        "prefix": "KnobSmall_",
    },
}


def _arguments() -> argparse.Namespace:
    separator = sys.argv.index("--") if "--" in sys.argv else len(sys.argv)
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--size", default=256, type=int)
    return parser.parse_args(sys.argv[separator + 1:])


def _target_objects(prefix: str) -> list[bpy.types.Object]:
    objects = [obj for obj in bpy.data.objects if obj.name.startswith(prefix)]
    if not objects:
        raise RuntimeError(f"Không tìm thấy Blender object với prefix {prefix!r}.")
    return objects


def _frame_camera(scene: bpy.types.Scene, objects: list[bpy.types.Object], face: bpy.types.Object) -> tuple:
    """Use a perpendicular product-shot camera; never the overview camera."""
    camera = scene.camera
    if camera is None:
        raise RuntimeError("Scene Blender không có active camera.")

    points = [obj.matrix_world @ Vector(corner) for obj in objects for corner in obj.bound_box]
    center = sum(points, Vector((0.0, 0.0, 0.0))) / len(points)
    # The face normal is the only reliable camera direction across the source
    # file. A front-on view keeps the knob circular and prevents a giant,
    # side-on 3D body from replacing the compact panel knob.
    orientation = face.matrix_world.to_quaternion()
    normal = orientation @ Vector((0.0, 0.0, 1.0))
    # Blender cameras look along local -Z.  From a position above the face
    # (positive normal) the viewing direction must be *towards* the face.
    camera_orientation = (-normal).to_track_quat("-Z", "Y")
    right = camera_orientation @ Vector((1.0, 0.0, 0.0))
    up = camera_orientation @ Vector((0.0, 1.0, 0.0))
    width = max(point.dot(right) for point in points) - min(point.dot(right) for point in points)
    height = max(point.dot(up) for point in points) - min(point.dot(up) for point in points)
    span = max(width, height, .001) * 1.20

    original = (camera.matrix_world.copy(), camera.data.type, camera.data.ortho_scale)
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = span
    camera.rotation_mode = "QUATERNION"
    camera.rotation_quaternion = camera_orientation
    camera.location = center + normal * max(10.0, span * 4.0)
    bpy.context.view_layer.update()
    return camera, original


def _render_family(scene: bpy.types.Scene, family: str, spec: dict, output: Path, size: int) -> None:
    prefix = spec["prefix"]
    targets = _target_objects(prefix)
    output.mkdir(parents=True, exist_ok=True)

    original_hidden = {obj: obj.hide_render for obj in bpy.data.objects}
    original_path = scene.render.filepath
    original_transparent = scene.render.film_transparent
    original_size = (scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage)
    face = bpy.data.objects[f"{prefix}face"]
    camera, original_camera = _frame_camera(scene, targets, face)

    try:
        # Lights/camera remain available; every unrelated mesh is hidden.
        target_set = set(targets)
        for obj in bpy.data.objects:
            if obj.type == "MESH":
                # The old Blender pointer must never be baked into frames.
                # RotaryKnob owns the only live, correctly mapped indicator.
                obj.hide_render = obj not in target_set or obj.name == f"{prefix}pointer"

        scene.render.film_transparent = True
        scene.render.image_settings.file_format = "PNG"
        scene.render.image_settings.color_mode = "RGBA"
        scene.render.resolution_x = size
        scene.render.resolution_y = size
        scene.render.resolution_percentage = 100

        scene.render.filepath = str(output / "00.png")
        bpy.ops.render.render(write_still=True)
    finally:
        for obj, hidden in original_hidden.items():
            obj.hide_render = hidden
        scene.render.filepath = original_path
        scene.render.film_transparent = original_transparent
        scene.render.resolution_x, scene.render.resolution_y, scene.render.resolution_percentage = original_size
        camera.matrix_world, camera.data.type, camera.data.ortho_scale = original_camera


def main() -> None:
    args = _arguments()
    if args.size < 64:
        raise ValueError("--size phải từ 64 px trở lên.")
    scene = bpy.context.scene
    for family, spec in KNOB_FAMILIES.items():
        _render_family(scene, family, spec, args.output / family, args.size)
    print(f"Đã xuất {len(KNOB_FAMILIES)} body knob không kim vào {args.output}")


if __name__ == "__main__":
    main()
