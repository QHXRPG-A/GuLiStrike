import bpy
import json
import math
from pathlib import Path
from mathutils import Vector, Matrix, Euler

ROOT = Path(__file__).resolve().parent.parent
SOURCE_BLEND = ROOT / "blender" / "quadruped-mech-side-panels.blend"
OUTPUT_BLEND = ROOT / "blender" / "quadruped-mech-vertex-animation.blend"
REPORT_PATH = ROOT / "blender" / "vertex-animation-production.json"
FPS = 24
WALK_FRAMES = 29
MINING_FRAMES = 73
TAU = math.tau


def smooth01(value):
    value = max(0.0, min(1.0, value))
    return value * value * (3.0 - 2.0 * value)


def translation(value):
    return Matrix.Translation(Vector(value))


def body_matrix(offset=(0.0, 0.0, 0.0), pitch=0.0, roll=0.0):
    pivot = Vector((0.0, 0.0, 1.55))
    rotation = Euler((pitch, roll, 0.0), "XYZ").to_matrix().to_4x4()
    return translation(offset) @ translation(pivot) @ rotation @ translation(-pivot)


def walk_pose(frame):
    phase = ((frame - 1) % (WALK_FRAMES - 1)) / (WALK_FRAMES - 1)
    offset = (
        0.026 * math.sin(TAU * phase),
        0.0,
        0.042 * math.cos(2.0 * TAU * phase),
    )
    pitch = math.radians(1.1) * math.sin(TAU * phase)
    roll = math.radians(0.75) * math.sin(TAU * phase)
    return body_matrix(offset, pitch, roll), offset, phase


def mining_pose(frame):
    phase = ((frame - 1) % (MINING_FRAMES - 1)) / (MINING_FRAMES - 1)
    laser = 0.0
    if phase < 0.18:
        brace = smooth01(phase / 0.18)
        back = 0.12 * brace
        down = -0.14 * brace
        pitch = math.radians(-1.0) * brace
    elif phase < 0.24:
        kick = smooth01((phase - 0.18) / 0.06)
        back = 0.12 + 0.22 * kick
        down = -0.14 - 0.18 * kick
        pitch = math.radians(-1.0 - 2.5 * kick)
        laser = kick
    elif phase < 0.78:
        settle = smooth01((phase - 0.24) / 0.12)
        vibration = math.sin(TAU * (phase - 0.24) * 9.0)
        back = 0.34 - 0.05 * settle + 0.004 * vibration
        down = -0.32 + 0.04 * settle + 0.003 * vibration
        pitch = math.radians(-3.5 + 1.0 * settle)
        laser = 0.94 + 0.06 * math.sin(TAU * (phase - 0.24) * 12.0)
    elif phase < 0.84:
        back = 0.29
        down = -0.28
        pitch = math.radians(-2.5)
        laser = 1.0 - smooth01((phase - 0.78) / 0.06)
    else:
        recover = smooth01((phase - 0.84) / 0.16)
        back = 0.29 * (1.0 - recover)
        down = -0.28 * (1.0 - recover)
        pitch = math.radians(-2.5) * (1.0 - recover)
    return body_matrix((0.0, back, down), pitch, 0.0), (0.0, back, down), phase, laser


LEGS = {
    "Leg_Front_L": {"sign": -1.0, "phase": 0.0},
    "Leg_Front_R": {"sign": 1.0, "phase": 0.5},
    "Leg_Rear_L": {"sign": -1.0, "phase": 0.5},
    "Leg_Rear_R": {"sign": 1.0, "phase": 0.0},
}
for name, data in LEGS.items():
    sign = data["sign"]
    if "Front" in name:
        data["hip"] = Vector((sign * 0.99, -1.10, 1.86))
        data["knee"] = Vector((sign * 1.46, -1.69, 0.99))
        data["ankle"] = Vector((sign * 1.55, -2.06, 0.25))
    else:
        data["hip"] = Vector((sign * 0.99, 1.12, 1.88))
        data["knee"] = Vector((sign * 1.72, 1.62, 0.99))
        data["ankle"] = Vector((sign * 1.98, 1.96, 0.25))
    data["upper_length"] = (data["knee"] - data["hip"]).length
    data["lower_length"] = (data["ankle"] - data["knee"]).length
    rest_axis = (data["ankle"] - data["hip"]).normalized()
    bend = data["knee"] - data["hip"]
    data["bend"] = (bend - rest_axis * bend.dot(rest_axis)).normalized()


def walk_foot(leg_name, cycle_phase):
    data = LEGS[leg_name]
    phase = (cycle_phase + data["phase"]) % 1.0
    stride = 0.72
    stance = 0.62
    lift = 0.24
    if phase < stance:
        y = -stride * 0.5 + stride * phase / stance
        z = 0.0
        ankle_alignment = 0.0
    else:
        swing = (phase - stance) / (1.0 - stance)
        y = stride * 0.5 - stride * smooth01(swing)
        ankle_alignment = math.sin(math.pi * swing)
        z = lift * ankle_alignment
    target = data["ankle"] + Vector((0.0, y, z))
    return target, ankle_alignment


def solve_leg(leg_name, hip, ankle):
    data = LEGS[leg_name]
    knee_rest = data["knee"]
    hip_rest = data["hip"]
    axis = ankle - hip
    distance = max(axis.length, 0.0001)
    direction = axis / distance
    l1 = data["upper_length"]
    l2 = data["lower_length"]
    reachable = min(distance, l1 + l2 - 0.0001)
    along = (l1 * l1 - l2 * l2 + reachable * reachable) / (2.0 * reachable)
    height = math.sqrt(max(0.0, l1 * l1 - along * along))
    bend = data["bend"] - direction * data["bend"].dot(direction)
    if bend.length < 0.0001:
        bend = Vector((1.0, 0.0, 0.0))
    bend.normalize()
    knee = hip + direction * along + bend * height
    return knee


def segment_matrix(rest_start, rest_end, new_start, new_end):
    old_direction = rest_end - rest_start
    new_direction = new_end - new_start
    rotation = old_direction.normalized().rotation_difference(new_direction.normalized())
    return translation(new_start) @ rotation.to_matrix().to_4x4() @ translation(-rest_start)


def point_segment_distance(point, start, end):
    segment = end - start
    factor = max(0.0, min(1.0, (point - start).dot(segment) / max(segment.length_squared, 1e-12)))
    return (point - (start + segment * factor)).length


def component_indices(obj):
    mesh = obj.data
    adjacency = [[] for _ in mesh.vertices]
    for edge in mesh.edges:
        a, b = edge.vertices
        adjacency[a].append(b)
        adjacency[b].append(a)
    visited = set()
    result = []
    for seed in range(len(mesh.vertices)):
        if seed in visited:
            continue
        stack = [seed]
        visited.add(seed)
        indices = []
        while stack:
            index = stack.pop()
            indices.append(index)
            for other in adjacency[index]:
                if other not in visited:
                    visited.add(other)
                    stack.append(other)
        result.append(indices)
    return result


def source_name(obj):
    if obj.get("is_lineart", False) and obj.parent:
        return obj.parent.get("asset_source_name", obj.parent.name)
    return obj.get("asset_source_name", obj.name)


def leg_for_object(obj):
    name = source_name(obj)
    prefix = name.split(" · ")[0]
    return prefix if prefix in LEGS else None


def classify_component(center, leg_name):
    data = LEGS[leg_name]
    if center.z <= 0.38 or (center - data["ankle"]).length < 0.34:
        return "foot"
    hip_delta = center - data["hip"]
    hip_cap = abs(hip_delta.x) > 0.18 and abs(hip_delta.y) < 0.08 and abs(hip_delta.z) < 0.08 and hip_delta.length < 0.36
    if center.z > 1.46 and (hip_delta.length < 0.24 or hip_cap):
        return "hip"
    knee_delta = center - data["knee"]
    knee_cap = abs(knee_delta.x) > 0.18 and abs(knee_delta.y) < 0.12 and abs(knee_delta.z) < 0.16 and knee_delta.length < 0.38
    if knee_delta.length < 0.20 or knee_cap:
        return "lower"
    upper_distance = point_segment_distance(center, data["hip"], data["knee"])
    lower_distance = point_segment_distance(center, data["knee"], data["ankle"])
    return "upper" if upper_distance <= lower_distance else "lower"


def all_fcurves(action):
    if hasattr(action, "fcurves"):
        yield from action.fcurves
        return
    for layer in action.layers:
        for strip in layer.strips:
            if hasattr(strip, "channelbags"):
                for bag in strip.channelbags:
                    yield from bag.fcurves


def set_linear(action):
    for fcurve in all_fcurves(action):
        for point in fcurve.keyframe_points:
            point.interpolation = "LINEAR"


def animate_shape_keys(obj, poses, clip_name):
    key_data = obj.data.shape_keys
    key_data.use_relative = False
    key_data.eval_time = 0.0
    key_data.animation_data_create()
    action = bpy.data.actions.new(f"VA_{clip_name}_{obj.name}")
    key_data.animation_data.action = action
    for frame in range(1, len(poses) + 1):
        key_data.eval_time = float((frame - 1) * 10)
        key_data.keyframe_insert(data_path="eval_time", frame=frame, group="顶点动画采样")
    set_linear(action)
    return action


def add_absolute_pose_keys(obj, world_poses, clip_name):
    inverse = obj.matrix_world.inverted()
    for frame, world_points in enumerate(world_poses, start=1):
        if frame == 1:
            key = obj.data.shape_keys.key_blocks[0]
            key.name = f"{clip_name}_001_Basis"
        else:
            key = obj.shape_key_add(name=f"{clip_name}_{frame:03d}", from_mix=False)
        for index, point in enumerate(world_points):
            key.data[index].co = inverse @ point
    return animate_shape_keys(obj, world_poses, clip_name)


def prepare_object(obj):
    if obj.type == "MESH" and obj.data.shape_keys is None:
        obj.shape_key_add(name="Basis", from_mix=False)


def duplicate_scene(base_scene, name, prefix):
    bpy.context.window.scene = base_scene
    bpy.ops.scene.new(type="FULL_COPY")
    scene = bpy.context.scene
    scene.name = name
    scene.render.fps = FPS
    scene.render.fps_base = 1.0
    scene.frame_start = 1
    scene.use_preview_range = True
    scene.render.engine = "BLENDER_EEVEE"
    for obj in list(scene.objects):
        original = obj.get("asset_source_name", obj.name)
        obj.name = f"{prefix}{original}"
        if obj.type == "MESH":
            obj.data = obj.data.copy()
            obj.data.name = f"{prefix}{original}_Mesh"
    bpy.context.view_layer.update()
    return scene


def build_mech_animation(scene, clip):
    is_walk = clip == "Walk"
    frame_count = WALK_FRAMES if is_walk else MINING_FRAMES
    scene.frame_end = frame_count
    scene.frame_preview_end = frame_count
    scene["动画类型"] = "绝对形态键顶点动画，无骨骼"
    scene["循环帧数"] = frame_count - 1
    scene["审核提示"] = "空格播放；在顶栏场景下拉框切换行进、采矿和静态"
    objects = [
        obj for obj in scene.objects
        if obj.type == "MESH" and source_name(obj) != "Studio ground"
    ]
    component_cache = {obj.name: component_indices(obj) for obj in objects if leg_for_object(obj)}
    rest_world = {
        obj.name: [obj.matrix_world @ vertex.co for vertex in obj.data.vertices]
        for obj in objects
    }
    component_groups = {}
    component_spans = {}
    for obj in objects:
        leg_name = leg_for_object(obj)
        if not leg_name:
            continue
        data = LEGS[leg_name]
        groups = []
        spans = []
        for indices in component_cache[obj.name]:
            points = [rest_world[obj.name][i] for i in indices]
            center = sum(points, Vector()) / len(points)
            group = classify_component(center, leg_name)
            dimensions = [max(point[axis] for point in points) - min(point[axis] for point in points) for axis in range(3)]
            if max(dimensions) < 0.09 and center.z > 1.46 and (center - data["hip"]).length < 0.40:
                group = "hip"
            elif max(dimensions) < 0.09 and (center - data["knee"]).length < 0.40:
                group = "lower"
            groups.append(group)
            min_z = min(point.z for point in points)
            max_z = max(point.z for point in points)
            span = None
            if group in {"upper", "lower"} and min_z < data["knee"].z - 0.08 and max_z > data["knee"].z + 0.08:
                span = "knee"
            elif group == "lower" and min_z < data["ankle"].z + 0.45 and max_z > data["ankle"].z + 0.35:
                span = "ankle"
            elif group == "upper" and min_z < data["hip"].z - 0.05 and max_z > data["hip"].z - 0.30:
                span = "hip"
            spans.append(span)
        component_groups[obj.name] = groups
        component_spans[obj.name] = spans

    for obj in objects:
        prepare_object(obj)
        world_poses = []
        leg_name = leg_for_object(obj)
        for frame in range(1, frame_count + 1):
            if is_walk:
                current_body, _, cycle_phase = walk_pose(frame)
            else:
                current_body, _, cycle_phase, _ = mining_pose(frame)
            if not leg_name:
                points = [current_body @ point for point in rest_world[obj.name]]
            else:
                data = LEGS[leg_name]
                hip = current_body @ data["hip"]
                if is_walk:
                    ankle, ankle_alignment = walk_foot(leg_name, cycle_phase)
                else:
                    ankle = data["ankle"].copy()
                    ankle_alignment = 0.0
                knee = solve_leg(leg_name, hip, ankle)
                lower_matrix = segment_matrix(data["knee"], data["ankle"], knee, ankle)
                foot_matrix = translation(ankle - data["ankle"])
                if ankle_alignment > 0.0:
                    lower_rotation = lower_matrix.to_quaternion()
                    foot_rotation = lower_rotation.copy()
                    foot_rotation.identity()
                    foot_rotation = foot_rotation.slerp(lower_rotation, ankle_alignment)
                    foot_matrix = translation(ankle) @ foot_rotation.to_matrix().to_4x4() @ translation(-data["ankle"])
                matrices = {
                    "hip": current_body,
                    "upper": segment_matrix(data["hip"], data["knee"], hip, knee),
                    "lower": lower_matrix,
                    "foot": foot_matrix,
                }
                points = [None] * len(obj.data.vertices)
                for indices, group, span in zip(component_cache[obj.name], component_groups[obj.name], component_spans[obj.name]):
                    matrix = matrices[group]
                    for index in indices:
                        rest_point = rest_world[obj.name][index]
                        if span == "knee":
                            weight = smooth01((rest_point.z - (data["knee"].z - 0.30)) / 0.60)
                            points[index] = (matrices["upper"] @ rest_point) * weight + (matrices["lower"] @ rest_point) * (1.0 - weight)
                        elif span == "ankle":
                            weight = smooth01((rest_point.z - data["ankle"].z) / 0.50)
                            points[index] = (matrices["lower"] @ rest_point) * weight + (matrices["foot"] @ rest_point) * (1.0 - weight)
                        elif span == "hip":
                            weight = smooth01((data["hip"].z - rest_point.z) / 0.45)
                            points[index] = (matrices["upper"] @ rest_point) * weight + (matrices["hip"] @ rest_point) * (1.0 - weight)
                        else:
                            points[index] = matrix @ rest_point
            world_poses.append(points)
        add_absolute_pose_keys(obj, world_poses, clip)
    return {
        "mesh_objects": len(objects),
        "leg_component_groups": {
            object_name: {group: groups.count(group) for group in sorted(set(groups))}
            for object_name, groups in component_groups.items()
        },
        "joint_transition_components": {
            object_name: {span: spans.count(span) for span in sorted(set(item for item in spans if item))}
            for object_name, spans in component_spans.items() if any(spans)
        },
    }


def srgb_to_linear(value):
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def hex_rgba(value):
    return tuple(srgb_to_linear(int(value[i:i + 2], 16) / 255.0) for i in (0, 2, 4)) + (1.0,)


def emission_material(name, color, strength):
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    material.diffuse_color = hex_rgba(color)
    nodes = material.node_tree.nodes
    nodes.clear()
    emission = nodes.new("ShaderNodeEmission")
    emission.inputs["Color"].default_value = hex_rgba(color)
    emission.inputs["Strength"].default_value = strength
    output = nodes.new("ShaderNodeOutputMaterial")
    output.location.x = 240
    material.node_tree.links.new(emission.outputs[0], output.inputs["Surface"])
    return material


def create_beam_object(scene, collection, name, radius, material):
    sides = 16
    vertices = []
    for ring in range(2):
        for side in range(sides):
            angle = TAU * side / sides
            vertices.append((radius * math.cos(angle), -float(ring), radius * math.sin(angle)))
    faces = []
    for side in range(sides):
        nxt = (side + 1) % sides
        faces.append((side, nxt, sides + nxt, sides + side))
    faces.append(tuple(reversed(range(sides))))
    faces.append(tuple(range(sides, sides * 2)))
    mesh = bpy.data.meshes.new(f"{name}_Mesh")
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    obj = bpy.data.objects.new(name, mesh)
    collection.objects.link(obj)
    obj["asset_source_name"] = name
    obj["顶点动画角色"] = "采矿激光"
    obj.data.materials.append(material)
    obj.shape_key_add(name="Basis", from_mix=False)
    muzzle_rest = Vector((0.0, -2.055, 1.712))
    target = Vector((0.0, -6.50, 0.15))
    poses = []
    laser_values = []
    for frame in range(1, MINING_FRAMES + 1):
        current_body, _, _, laser = mining_pose(frame)
        laser_values.append(laser)
        start = current_body @ muzzle_rest
        direction = target - start
        length = direction.length
        direction.normalize()
        u = direction.cross(Vector((1.0, 0.0, 0.0))).normalized()
        v = direction.cross(u).normalized()
        actual_radius = max(0.000001, radius * laser)
        points = []
        for ring in range(2):
            center = start if ring == 0 else target
            for side in range(sides):
                angle = TAU * side / sides
                points.append(center + actual_radius * (u * math.cos(angle) + v * math.sin(angle)))
        poses.append(points)
    add_absolute_pose_keys(obj, poses, "Mining")
    return obj, laser_values


base_scene = bpy.context.scene
base_scene.name = "00_静态 · 当前资源"
for obj in base_scene.objects:
    obj["asset_source_name"] = obj.name

walk_scene = duplicate_scene(base_scene, "01_行进 · 顶点动画", "W · ")
walk_report = build_mech_animation(walk_scene, "Walk")
walk_scene.timeline_markers.clear()
walk_scene.timeline_markers.new("对角步态 A｜循环起点", frame=1)
walk_scene.timeline_markers.new("对角步态 B", frame=8)
walk_scene.timeline_markers.new("对角步态 A｜对侧", frame=15)
walk_scene.timeline_markers.new("对角步态 B｜回环", frame=22)
walk_scene.frame_set(1)
walk_scene.camera = next(obj for obj in walk_scene.objects if obj.type == "CAMERA" and "Review main" in obj.name)

mining_scene = duplicate_scene(base_scene, "02_采矿 · 顶点动画", "M · ")
mining_report = build_mech_animation(mining_scene, "Mining")
mining_scene.timeline_markers.clear()
mining_scene.timeline_markers.new("支撑预备｜四足固定", frame=1)
mining_scene.timeline_markers.new("激光点火｜后坐开始", frame=14)
mining_scene.timeline_markers.new("最大后坐｜下压后移", frame=18)
mining_scene.timeline_markers.new("持续采矿", frame=25)
mining_scene.timeline_markers.new("激光收束", frame=58)
mining_scene.timeline_markers.new("恢复待机｜闭环", frame=73)
fx_collection = bpy.data.collections.new("M · Mining Laser · 顶点动画")
mining_scene.collection.children.link(fx_collection)
outer_material = emission_material("采矿激光 · 青蓝外层", "0D9099", 4.0)
core_material = emission_material("采矿激光 · 奶白亮核", "FEE4D9", 7.0)
outer_beam, outer_laser = create_beam_object(mining_scene, fx_collection, "M · Mining Laser Outer", 0.050, outer_material)
core_beam, core_laser = create_beam_object(mining_scene, fx_collection, "M · Mining Laser Core", 0.022, core_material)
mining_camera = next(obj for obj in mining_scene.objects if obj.type == "CAMERA" and "Review main" in obj.name)
mining_camera.location = (7.5, -10.5, 4.8)
look_target = Vector((0.0, -1.65, 1.05))
mining_camera.rotation_euler = (look_target - mining_camera.location).to_track_quat("-Z", "Y").to_euler()
mining_camera.data.lens = 48
mining_scene.camera = mining_camera
mining_scene.frame_set(18)

for scene in (walk_scene, mining_scene):
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type != "VIEW_3D":
                continue
            space = area.spaces.active
            space.shading.type = "MATERIAL"
            space.shading.use_scene_world = True
            space.shading.use_scene_lights = True
            space.region_3d.view_perspective = "CAMERA"
            space.overlay.show_extras = False

notes = bpy.data.texts.get("动画审核说明") or bpy.data.texts.new("动画审核说明")
notes.clear()
notes.write("""四足机甲顶点动画审核

在 Blender 顶栏 Scene 下拉框切换：
00_静态 · 当前资源：未变形的当前模型。
01_行进 · 顶点动画：28 帧对角步态循环，24 fps，约 1.167 秒。
02_采矿 · 顶点动画：72 帧采矿循环，24 fps，3 秒。

按空格播放。全部机甲网格和独立描边壳使用同步的绝对形态键；没有骨骼蒙皮。
行进：对角两组交替， stride 0.72 m，抬脚 0.24 m，机身轻微起伏和侧摆，原地循环。
采矿：四足脚掌不动，机身最大后移约 0.34 m、下降约 0.32 m；头部激光从原灯缝位置发射，激光落点固定。

采矿时间轴：1–13 支撑预备；14 点火；18 最大后坐；25–57 持续采矿；58 收束；73 回到起点。
激光是网格顶点动画，不是 Niagara；后续导入 UE 时需按最终审核版本烘焙 VAT/等效材质。
本次未导入 UE，未替换矿车正式资源。视觉审核 B 由用户进行。
""")

bpy.context.window.scene = walk_scene
walk_scene.frame_set(1)
bpy.ops.wm.save_as_mainfile(filepath=str(OUTPUT_BLEND), check_existing=False, compress=True)

report = {
    "source_blend": str(SOURCE_BLEND),
    "output_blend": str(OUTPUT_BLEND),
    "method": "absolute shape keys / sampled rigid-component vertex animation",
    "fps": FPS,
    "walk": {
        "scene": walk_scene.name,
        "frames": [1, WALK_FRAMES],
        "loop_frames": WALK_FRAMES - 1,
        "seconds": (WALK_FRAMES - 1) / FPS,
        "stride_m": 0.72,
        "foot_lift_m": 0.24,
        "body_vertical_amplitude_m": 0.042,
        **walk_report,
    },
    "mining": {
        "scene": mining_scene.name,
        "frames": [1, MINING_FRAMES],
        "loop_frames": MINING_FRAMES - 1,
        "seconds": (MINING_FRAMES - 1) / FPS,
        "maximum_body_back_m": 0.34,
        "maximum_body_down_m": 0.32,
        "feet_planted": True,
        "laser_outer": outer_beam.name,
        "laser_core": core_beam.name,
        "laser_peak_frames": [14, 57],
        **mining_report,
    },
    "static_scene": base_scene.name,
    "ue_imported": False,
    "visual_acceptance": "待用户在 Blender 审核；未进行助手读图目检",
}
REPORT_PATH.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print(f"VERTEX_ANIMATION_BLEND={OUTPUT_BLEND}")
print(f"VERTEX_ANIMATION_REPORT={REPORT_PATH}")
