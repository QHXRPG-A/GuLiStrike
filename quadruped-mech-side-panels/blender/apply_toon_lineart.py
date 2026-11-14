import bpy
import hashlib
import json
import math
from pathlib import Path
from mathutils import Matrix

ROOT = Path(__file__).resolve().parent.parent
CURRENT_BLEND = ROOT / "blender" / "quadruped-mech-side-panels.blend"
BACKUP_BLEND = ROOT / "blender" / "quadruped-mech-side-panels.pre-toon.blend"
REPORT_PATH = ROOT / "blender" / "toon-lineart-report.json"
REVIEW_DIR = ROOT / "review" / "toon"
REVIEW_DIR.mkdir(parents=True, exist_ok=True)

SHADOW_FACTOR = 0.58
MID_FACTOR = 0.80
HIGHLIGHT_FACTOR = 1.0
MID_THRESHOLD = 0.32
HIGHLIGHT_THRESHOLD = 0.68
OUTLINE_HEX = "1A182F"


def srgb_channel_to_linear(value):
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def hex_to_linear(hex_value):
    return tuple(srgb_channel_to_linear(int(hex_value[i:i + 2], 16) / 255.0) for i in (0, 2, 4))


def geometry_signature(obj):
    digest = hashlib.sha256()
    for vertex in obj.data.vertices:
        digest.update(tuple(round(value, 7) for value in vertex.co).__repr__().encode("ascii"))
    for polygon in obj.data.polygons:
        digest.update(tuple(polygon.vertices).__repr__().encode("ascii"))
    for row in obj.matrix_world:
        digest.update(tuple(round(value, 7) for value in row).__repr__().encode("ascii"))
    return digest.hexdigest()


def new_node(nodes, node_type, name, x, y):
    node = nodes.new(node_type)
    node.name = name
    node.label = name
    node.location = (x, y)
    return node


def principled_input(principled, name, fallback):
    socket = principled.inputs.get(name)
    return socket.default_value if socket is not None else fallback


scene = bpy.context.scene
scene.render.engine = "BLENDER_EEVEE"
source_meshes = [
    obj for obj in scene.objects
    if obj.type == "MESH" and obj.name != "Studio ground" and not obj.get("is_lineart", False)
]
original_signatures = {obj.name: geometry_signature(obj) for obj in source_meshes}
original_panel_angles = {
    obj.name: obj.get("内倾角度_度") for obj in source_meshes if obj.name.startswith("Side_Panel_")
}

# Keep a direct recovery copy, while editing the current deliverable in place.
bpy.ops.wm.save_as_mainfile(filepath=str(BACKUP_BLEND), check_existing=False)

# Dedicated collections keep the render-time internal lines off the outline shells and ground.
mech_collection = bpy.data.collections.get("Mech · 三渲二") or bpy.data.collections.new("Mech · 三渲二")
outline_collection = bpy.data.collections.get("Lineart · 外轮廓") or bpy.data.collections.new("Lineart · 外轮廓")
if mech_collection.name not in scene.collection.children:
    scene.collection.children.link(mech_collection)
if outline_collection.name not in scene.collection.children:
    scene.collection.children.link(outline_collection)
for obj in source_meshes:
    if obj.name not in mech_collection.objects:
        mech_collection.objects.link(obj)

outline_color = hex_to_linear(OUTLINE_HEX)
outline_material = bpy.data.materials.get("线稿 · 深蓝灰外轮廓") or bpy.data.materials.new("线稿 · 深蓝灰外轮廓")
outline_material.use_nodes = True
outline_material.diffuse_color = (*outline_color, 1.0)
outline_material.use_backface_culling = True
if hasattr(outline_material, "use_backface_culling_shadow"):
    outline_material.use_backface_culling_shadow = True
outline_nodes = outline_material.node_tree.nodes
outline_nodes.clear()
outline_emission = new_node(outline_nodes, "ShaderNodeEmission", "线稿固有色", 0, 0)
outline_emission.inputs["Color"].default_value = (*outline_color, 1.0)
outline_emission.inputs["Strength"].default_value = 1.0
outline_output = new_node(outline_nodes, "ShaderNodeOutputMaterial", "线稿输出", 220, 0)
outline_material.node_tree.links.new(outline_emission.outputs["Emission"], outline_output.inputs["Surface"])

material_records = []
for material in list(bpy.data.materials):
    if material.users == 0 or not material.name.startswith("机甲 ·"):
        continue
    source_principled = next((node for node in material.node_tree.nodes if node.bl_idname == "ShaderNodeBsdfPrincipled"), None)
    if source_principled is None:
        continue
    base_color = tuple(principled_input(source_principled, "Base Color", (0.5, 0.5, 0.5, 1.0)))
    emission_strength = float(principled_input(source_principled, "Emission Strength", 0.0))

    pbr_backup_name = f"PBR_BACKUP · {material.name}"
    pbr_backup = bpy.data.materials.get(pbr_backup_name)
    if pbr_backup is None:
        pbr_backup = material.copy()
        pbr_backup.name = pbr_backup_name
        pbr_backup.use_fake_user = True

    tree = material.node_tree
    tree.nodes.clear()
    nodes = tree.nodes
    links = tree.links
    base = new_node(nodes, "ShaderNodeRGB", "组件纯色基色", -620, 220)
    base.outputs[0].default_value = base_color
    diffuse = new_node(nodes, "ShaderNodeBsdfDiffuse", "受光亮度", -620, -60)
    diffuse.inputs["Color"].default_value = (1.0, 1.0, 1.0, 1.0)
    diffuse.inputs["Roughness"].default_value = 0.0
    shader_to_rgb = new_node(nodes, "ShaderNodeShaderToRGB", "EEVEE 明暗采样", -390, -60)
    ramp = new_node(nodes, "ShaderNodeValToRGB", "三档明暗 · 硬边", -160, -60)
    ramp.color_ramp.interpolation = "CONSTANT"
    elements = ramp.color_ramp.elements
    elements[0].position = 0.0
    elements[0].color = (SHADOW_FACTOR, SHADOW_FACTOR, SHADOW_FACTOR, 1.0)
    elements[1].position = HIGHLIGHT_THRESHOLD
    elements[1].color = (HIGHLIGHT_FACTOR, HIGHLIGHT_FACTOR, HIGHLIGHT_FACTOR, 1.0)
    middle = elements.new(MID_THRESHOLD)
    middle.color = (MID_FACTOR, MID_FACTOR, MID_FACTOR, 1.0)
    multiply = new_node(nodes, "ShaderNodeMixRGB", "基色 × 三档明暗", 110, 190)
    multiply.blend_type = "MULTIPLY"
    multiply.inputs[0].default_value = 1.0
    emission = new_node(nodes, "ShaderNodeEmission", "三渲二输出", 340, 190)
    emission.inputs["Strength"].default_value = 2.2 if material.name == "机甲 · 冷白灯缝" else 1.0
    output = new_node(nodes, "ShaderNodeOutputMaterial", "材质输出", 570, 190)
    links.new(diffuse.outputs["BSDF"], shader_to_rgb.inputs["Shader"])
    links.new(shader_to_rgb.outputs["Color"], ramp.inputs["Fac"])
    links.new(base.outputs["Color"], multiply.inputs[1])
    links.new(ramp.outputs["Color"], multiply.inputs[2])
    links.new(multiply.outputs["Color"], emission.inputs["Color"])
    links.new(emission.outputs["Emission"], output.inputs["Surface"])
    material.diffuse_color = base_color
    material["风格"] = "三渲二 · 三档硬边明暗"
    material["阴影档"] = SHADOW_FACTOR
    material["中间档"] = MID_FACTOR
    material["亮部档"] = HIGHLIGHT_FACTOR
    material["原PBR备份"] = pbr_backup.name
    material_records.append({
        "name": material.name,
        "base_color_linear": list(base_color),
        "emission_strength": emission.inputs["Strength"].default_value,
        "pbr_backup": pbr_backup.name,
        "ramp_interpolation": ramp.color_ramp.interpolation,
        "ramp_stops": [
            {"position": element.position, "factor": element.color[0]}
            for element in sorted(ramp.color_ramp.elements, key=lambda item: item.position)
        ],
    })

outline_objects = []
for source in source_meshes:
    if source.name == "Front_Armor · 机甲 · 冷白灯缝":
        continue
    outline = bpy.data.objects.new(f"OUTLINE · {source.name}", source.data.copy())
    outline_collection.objects.link(outline)
    outline.parent = source
    outline.matrix_parent_inverse = Matrix.Identity(4)
    outline.matrix_basis = Matrix.Identity(4)
    outline.hide_select = True
    outline.hide_render = False
    outline.hide_viewport = False
    outline["is_lineart"] = True
    outline["轮廓来源"] = source.name
    outline.data.materials.clear()
    outline.data.materials.append(outline_material)
    for polygon in outline.data.polygons:
        polygon.material_index = 0
    solidify = outline.modifiers.new("外轮廓 · 反向壳", "SOLIDIFY")
    minimum_dimension = min((value for value in source.dimensions if value > 0.0001), default=0.01)
    solidify.thickness = max(0.0012, min(0.006, minimum_dimension * 0.12))
    solidify.offset = 1.0
    solidify.use_flip_normals = True
    solidify.use_rim = False
    solidify.use_even_offset = True
    solidify.use_quality_normals = True
    for property_name in ["visible_shadow", "visible_diffuse", "visible_glossy", "visible_transmission", "visible_volume_scatter"]:
        if hasattr(outline, property_name):
            setattr(outline, property_name, False)
    outline_objects.append(outline)

scene.render.use_freestyle = True
freestyle = bpy.context.view_layer.freestyle_settings
lineset = freestyle.linesets[0] if len(freestyle.linesets) else freestyle.linesets.new("主要结构线")
lineset.name = "主要折角与组件分界"
lineset.select_by_collection = True
lineset.collection = mech_collection
lineset.select_by_edge_types = True
lineset.select_silhouette = False
lineset.select_border = False
lineset.select_contour = False
lineset.select_external_contour = False
lineset.select_crease = True
lineset.select_material_boundary = True
lineset.select_edge_mark = False
freestyle.crease_angle = math.radians(134.0)
linestyle = lineset.linestyle
linestyle.name = "线稿 · 深蓝灰细内线"
linestyle.color = outline_color
linestyle.alpha = 0.78
linestyle.thickness = 0.85
if hasattr(linestyle, "caps"):
    linestyle.caps = "ROUND"

scene.render.resolution_x = 1440
scene.render.resolution_y = 1080
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.film_transparent = False

# Open the native resource in a scene-lit material view, with the review camera active.
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type != "VIEW_3D":
            continue
        space = area.spaces.active
        space.shading.type = "MATERIAL"
        space.shading.use_scene_world = True
        space.shading.use_scene_lights = True
        space.region_3d.view_perspective = "CAMERA"

bpy.context.view_layer.update()
geometry_unchanged = all(geometry_signature(obj) == original_signatures[obj.name] for obj in source_meshes)
panel_angles_preserved = all(obj.get("内倾角度_度") == angle for obj in bpy.data.objects if (angle := original_panel_angles.get(obj.name)) is not None)

camera_map = {
    "three-quarter": "Review main",
    "front": "Review front",
    "side": "Review side",
    "rear": "Review rear",
}
rendered_views = []
for view_name, camera_name in camera_map.items():
    camera = bpy.data.objects.get(camera_name)
    if camera is None:
        raise RuntimeError(f"Missing review camera: {camera_name}")
    scene.camera = camera
    scene.render.filepath = str(REVIEW_DIR / f"{view_name}.png")
    bpy.ops.render.render(write_still=True)
    rendered_views.append(scene.render.filepath)

scene.camera = bpy.data.objects["Review main"]
scene.render.filepath = "//review/toon/three-quarter.png"
bpy.ops.file.pack_all()
bpy.ops.wm.save_as_mainfile(filepath=str(CURRENT_BLEND), check_existing=False)

report = {
    "updated_in_place": str(CURRENT_BLEND),
    "recovery_backup": str(BACKUP_BLEND),
    "render_engine": scene.render.engine,
    "three_tone": {
        "shadow_factor": SHADOW_FACTOR,
        "middle_factor": MID_FACTOR,
        "highlight_factor": HIGHLIGHT_FACTOR,
        "thresholds": [MID_THRESHOLD, HIGHLIGHT_THRESHOLD],
        "interpolation": "CONSTANT",
    },
    "materials": material_records,
    "outline_object_count": len(outline_objects),
    "outline_color_srgb": f"#{OUTLINE_HEX}",
    "freestyle_enabled": scene.render.use_freestyle,
    "freestyle_lineset": lineset.name,
    "freestyle_crease_angle_degrees": math.degrees(freestyle.crease_angle),
    "source_mesh_count": len(source_meshes),
    "source_geometry_and_transforms_unchanged": geometry_unchanged,
    "side_panel_angles_preserved": panel_angles_preserved,
    "rendered_review_views": rendered_views,
    "visual_acceptance": "待用户审核 B；未进行助手读图目检",
    "ue_imported": False,
}
REPORT_PATH.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps({key: value for key, value in report.items() if key != "materials"}, ensure_ascii=False, indent=2))
print("TOON_MATERIAL_COUNT=", len(material_records))
print("REPORT_JSON=", REPORT_PATH)
