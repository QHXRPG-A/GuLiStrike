import bpy, json
import numpy as np
from pathlib import Path
from mathutils import Vector

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919')
OUT = ROOT / 'Production_v7_Black'
OUT.mkdir(exist_ok=True)
(OUT / 'Previews').mkdir(exist_ok=True)

def linear(h):
    c = [int(h[i:i+2], 16) / 255 for i in (0, 2, 4)]
    return [v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4 for v in c] + [1]

fairing = bpy.data.objects['WORK_Mech_Lightest__Sealed_Armor_Fairing']
mat = fairing.data.materials[0]
mat.name = 'PART7_LightMech_ClosedArmor_Black'
black = linear('24282C')
mat.node_tree.nodes['Paint_x_Three_Tones'].inputs[1].default_value = black
next(n for n in mat.node_tree.nodes if n.type == 'BSDF_PRINCIPLED').inputs['Base Color'].default_value = black
mat.diffuse_color = black

baseline = json.loads((ROOT / 'Production_v3_InkCel/production_report.json').read_text())
key = 'Mech_Lightest'
scene = bpy.data.scenes['Review_' + key]
bpy.context.window.scene = scene
bpy.context.view_layer.update()
cam = scene.camera
bounds = baseline['assets'][key]['bounds']
lo, hi = Vector(bounds['min']), Vector(bounds['max'])
center, span = (lo + hi) / 2, max(hi - lo)
for view, direction in [('Hero', Vector((1.5, -2, 1.05))), ('Front', Vector((0, -1, 0))), ('Side', Vector((1, 0, 0))), ('Rear', Vector((0, 1, 0)))]:
    cam.location = center + direction.normalized() * span * 3
    rot = (center - cam.location).to_track_quat('-Z', 'Y')
    cam.rotation_euler = rot.to_euler()
    pts = [Vector((x, y, z)) for x in (lo.x, hi.x) for y in (lo.y, hi.y) for z in (lo.z, hi.z)]
    right, up = rot @ Vector((1, 0, 0)), rot @ Vector((0, 1, 0))
    width = max(p.dot(right) for p in pts) - min(p.dot(right) for p in pts)
    height = max(p.dot(up) for p in pts) - min(p.dot(up) for p in pts)
    cam.data.ortho_scale = max(width, height * scene.render.resolution_x / scene.render.resolution_y) * 1.12
    scene.render.filepath = str(OUT / 'Previews' / (key + '_' + view + '.png'))
    bpy.ops.render.render(write_still=True)
    print('BLACK_V7_RENDERED ' + view, flush=True)

changed = []
for obj in bpy.data.collections['WORK_' + key].objects:
    if obj.type != 'MESH' or obj.get('ink_layer'):
        continue
    for material in obj.data.materials:
        for element in material.node_tree.nodes['THREE_TONE_Shadow_Mid_Light'].color_ramp.elements:
            changed.append((element, element.color[:]))
            element.color = (1, 1, 1, 1)
scene.render.filepath = str(OUT / 'Previews/Mech_Lightest_Rear_Paint.png')
bpy.ops.render.render(write_still=True)
for element, color in changed:
    element.color = color

bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'Mechs_ComponentColors_v7.blend'), check_existing=False)
report = json.loads((ROOT / 'Production_v6_Swap/component_paint_report.json').read_text())
report['source'] = str(ROOT / 'Production_v6_Swap/Mechs_ComponentColors_v6.blend')
report['revision'] = '7 white sealed armor changed to black per user'
report['parts'][fairing.name][0]['color'] = '24282C'
(OUT / 'component_paint_report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print('BLACK_V7_READY', flush=True)
