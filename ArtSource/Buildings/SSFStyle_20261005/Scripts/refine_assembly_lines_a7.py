"""Use thinner screen-space reference strokes in the full-size multi-building assembly."""
from pathlib import Path
import json
import bpy
from mathutils import Vector
ROOT=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');OUT=ROOT/'References_A_v7'
assert not (OUT/'reference_manifest.json').exists()
bpy.ops.wm.open_mainfile(filepath=str(OUT/'SSF_ReferenceDesign_v7.blend'))
group=next(s for s in bpy.data.scenes if 'ReferenceAssembly' in s.name)
other_styles={ls.linestyle.name for s in bpy.data.scenes if s!=group for vl in s.view_layers for ls in vl.freestyle_settings.linesets if ls.linestyle is not None}
for ls in group.view_layers[0].freestyle_settings.linesets:
    assert ls.linestyle.name not in other_styles
    if ls.name=='StructuralLines':ls.linestyle.thickness=1.6
    if ls.name=='OuterContour':ls.linestyle.thickness=3.0
bpy.context.window.scene=group;cam=group.camera
points=[o.matrix_world@Vector(p) for o in group.objects if o.type=='MESH' for p in o.bound_box]
lo=Vector([min(p[a] for p in points) for a in range(3)]);hi=Vector([max(p[a] for p in points) for a in range(3)])
center=(lo+hi)/2;extent=max(hi-lo)
for view,direction in [('Hero',(.7,-1.2,1.4)),('Top',(0,0,1))]:
    cam.location=center+Vector(direction).normalized()*extent*4
    cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
    group.render.filepath=str(OUT/'Renders'/f'Assembly_{view}.png');bpy.ops.render.render(write_still=True)
bpy.context.preferences.filepaths.save_version=0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'SSF_ReferenceDesign_v7.blend'))
style=json.loads((OUT/'style_revision.json').read_text(encoding='utf-8'))
style.update(assembly_structure_width_4096=1.6,assembly_outer_width_4096=3.0)
(OUT/'style_revision.json').write_text(json.dumps(style,ensure_ascii=False,indent=2),encoding='utf-8')
render=json.loads((OUT/'render_manifest.json').read_text(encoding='utf-8'));render['style_revision']=style
(OUT/'render_manifest.json').write_text(json.dumps(render,ensure_ascii=False,indent=2),encoding='utf-8')
p=OUT/'style_preservation_validation.json';v=json.loads(p.read_text(encoding='utf-8'))
for r in v['line_checks']:
    if r['scene']==group.name:r['widths']={'StructuralLines':1.6,'OuterContour':3.0}
p.write_text(json.dumps(v,ensure_ascii=False,indent=2),encoding='utf-8')
print('ASSEMBLY_REFERENCE_STROKES_REFINED',flush=True)
