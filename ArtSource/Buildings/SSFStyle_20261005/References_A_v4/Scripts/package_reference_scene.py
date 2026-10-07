"""Pack reference textures before freezing A-v4, without touching source geometry."""
from pathlib import Path
import bpy
import json
ROOT=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
OUT=ROOT/'References_A_v4'
assert not (OUT/'reference_manifest.json').exists(), 'Published reference version is frozen'
bpy.ops.wm.open_mainfile(filepath=str(OUT/'SSF_ReferenceDesign_v4.blend'))
packed=[]
for im in bpy.data.images:
    if im.source=='FILE' and Path(bpy.path.abspath(im.filepath)).is_file():
        # Images are lazily loaded after reopening a blend; force pixel data first.
        first_pixel=im.pixels[0]
        assert im.has_data, im.filepath
        im.pack(); packed.append(im.name)
assert len(packed)>=10, ('Expected eight masks plus logo and light texture',packed)
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'SSF_ReferenceDesign_v4.blend'))
(OUT/'packed_scene_report.json').write_text(json.dumps({'success':True,'packed_images':packed,
    'reference_only':True,'production_modeling_started':False},ensure_ascii=False,indent=2),encoding='utf-8')
print('REFERENCE_TEXTURES_PACKED',len(packed),flush=True)
