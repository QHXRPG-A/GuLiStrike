"""Render the Blender Ship study and its matching source views for visual review."""
import bpy
import json
import sys
from pathlib import Path

ROOT = Path('D:/UE5.7/test1/ArtSource/Ships/ShipStylizedStudy_20260916')
views = {
    'hero': ('Ship_Stylized_Study', 'SS_Camera_Hero', '01_stylized_hero.png'),
    'top': ('Ship_Stylized_Study', 'SS_Camera_Top', '02_stylized_top.png'),
    'side': ('Ship_Stylized_Study', 'SS_Camera_Side', '03_stylized_side.png'),
    'rear': ('Ship_Stylized_Study', 'SS_Camera_Rear', '04_stylized_rear.png'),
    'original': ('Ship_Original_Reference', 'SS_Camera_Hero', '05_original_textured.png'),
    'compare': ('Ship_Compare_Original_Stylized', 'SS_Camera_Compare', '06_original_stylized_comparison.png'),
    'line_hero': ('Ship_Stylized_Study', 'SS_Camera_Hero', '07_lineart_hero.png'),
    'line_top': ('Ship_Stylized_Study', 'SS_Camera_Top', '08_lineart_top.png'),
    'line_side': ('Ship_Stylized_Study', 'SS_Camera_Side', '09_lineart_side.png'),
    'line_rear': ('Ship_Stylized_Study', 'SS_Camera_Rear', '10_lineart_rear.png'),
    'line_compare': ('Ship_Compare_Original_Stylized', 'SS_Camera_Compare', '11_lineart_comparison.png'),
    'bake_hero': ('Ship_Stylized_Study', 'SS_Camera_Hero', '12_baked_lineart_hero.png'),
    'bake_top': ('Ship_Stylized_Study', 'SS_Camera_Top', '13_baked_lineart_top.png'),
    'bake_side': ('Ship_Stylized_Study', 'SS_Camera_Side', '14_baked_lineart_side.png'),
    'bake_rear': ('Ship_Stylized_Study', 'SS_Camera_Rear', '15_baked_lineart_rear.png'),
    'bake_compare': ('Ship_Compare_Original_Stylized', 'SS_Camera_Compare', '16_baked_lineart_comparison.png'),
}
requested = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else list(views)
outputs = []
for key in requested:
    scene_name, camera_name, filename = views[key]
    scene = bpy.data.scenes[scene_name]
    scene.camera = bpy.data.objects[camera_name]
    scene.render.resolution_x, scene.render.resolution_y = (1800, 1050) if key.endswith('compare') else ((1600, 650) if key.endswith('side') else (1600, 1150))
    if hasattr(scene, 'eevee') and hasattr(scene.eevee, 'taa_render_samples'):
        scene.eevee.taa_render_samples = 128
    scene.render.filepath = str(ROOT / 'Previews' / filename)
    bpy.ops.render.render(scene=scene.name, write_still=True)
    outputs.append(scene.render.filepath)
    print('SHIP_REVIEW_RENDER ' + scene.render.filepath, flush=True)
(ROOT / ('render_' + '_'.join(requested) + '.json')).write_text(json.dumps({'success': True, 'outputs': outputs}, indent=2), encoding='utf-8')
