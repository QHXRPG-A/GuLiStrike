"""Install a checked baked mask in live Blender without changing the user's view."""
import bpy
import importlib.util
import json
from pathlib import Path

import numpy as np

SCRIPT = Path('D:/UE5.7/test1/Scripts/Blender/bake_ship_lineart_mask.py')
spec = importlib.util.spec_from_file_location('ship_ink_bake', SCRIPT)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
ship = bpy.data.objects['SS_Dreadnought_Stylized']
report_path = module.ROOT / 'lineart_bake_report.json'
report = json.loads(report_path.read_text(encoding='utf-8'))
assert report['success']
assert module.geometry_hash(ship.data) == report['body_geometry_sha256']
assert module.geometry_hash(bpy.data.objects['SS_Ink_Outer_Contour'].data) == report['outline_geometry_sha256']
assert module.UV_NAME not in ship.data.uv_layers, 'Mask UV already installed; inspect before reapplying.'
assert (module.ROOT / 'GuLiStrike_Ship_AnimeStudy_GeometricLines.blend').exists()
old_active = ship.data.uv_layers.active_index
old_render = next(uv.name for uv in ship.data.uv_layers if uv.active_render)
uv = ship.data.uv_layers.new(name=module.UV_NAME)
coords = np.load(module.UV_PATH)['uv']
assert len(coords) == len(uv.data) * 2
uv.data.foreach_set('uv', coords)
ship.data.uv_layers.active_index = old_active
ship.data.uv_layers[old_render].active_render = True
image = bpy.data.images.load(str(module.MASK_PATH), check_existing=False)
image.name = module.IMAGE_NAME
image.colorspace_settings.name = 'Non-Color'
image.pack()
image.filepath = '//Textures/' + module.MASK_PATH.name
module.install_mask(ship, image)
ship.data.update()
bpy.context.view_layer.update()
assert module.geometry_hash(ship.data) == report['body_geometry_sha256']
assert module.geometry_hash(bpy.data.objects['SS_Ink_Outer_Contour'].data) == report['outline_geometry_sha256']
assert not any(bpy.data.objects.get(name + suffix) for name in module.CURVE_NAMES for suffix in ('', '_Compare'))
bpy.ops.wm.save_as_mainfile(filepath=str(module.ROOT / 'GuLiStrike_Ship_AnimeStudy.blend'))
result = {'saved': bpy.data.filepath, 'body_geometry_unchanged': True,
          'outline_geometry_unchanged': True, 'mask_packed': bool(image.packed_file),
          'triangles': report['total_triangles'], 'mask': str(module.MASK_PATH)}
