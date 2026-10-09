"""Saved asset readback only; no gameplay test or formal UE asset operation."""
import bpy
import importlib.util
import hashlib
import json
from collections import Counter
from pathlib import Path

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'ArtSource/SweeperTeamColor_v1_20261008'
report=json.loads((OUT/'build-report.json').read_text(encoding='utf8'))
# Load only the invariant definitions, without running the production script.
code=(ROOT/'Scripts/Blender/build_sweeper_orange_team_review.py').read_text(encoding='utf8')
namespace={}
exec(compile(code.split('\ntry:\n    run()')[0],'<invariant definitions>','exec'),namespace)
invariant=namespace['invariant']
ROLE=namespace['ROLE'];ALPHA=namespace['ALPHA']
bpy.ops.wm.open_mainfile(filepath=report['source'])
baseline={e['object']:invariant(bpy.data.objects[e['object']]) for e in report['meshes']}
bpy.ops.wm.open_mainfile(filepath=report['blend'])
errors=[];checks=[]
for entry in report['meshes']:
    obj=bpy.data.objects[entry['object']];mesh=obj.data;mesh.calc_loop_triangles()
    actual=invariant(obj)
    if actual!=baseline[obj.name]:errors.append(obj.name+': original source invariant changed: '+','.join(k for k in actual if actual[k]!=baseline[obj.name][k]))
    if actual!=entry['invariant']:errors.append(obj.name+': report hash mismatch: '+','.join(k for k in actual if actual[k]!=entry['invariant'][k]))
    regions=mesh.attributes[ROLE];colors=mesh.color_attributes[ALPHA];original=mesh.color_attributes['Attribute']
    for face in mesh.polygons:
        role=regions.data[face.index].value
        if role not in (0,3):errors.append(obj.name+': unexpected region role');break
        for i in face.loop_indices:
            if abs(colors.data[i].color[3]*255-role)>1e-5:errors.append(obj.name+': encoded alpha mismatch');break
            if list(colors.data[i].color[:3])!=list(original.data[i].color[:3]):errors.append(obj.name+': original corner RGB changed');break
    checks.append({'object':obj.name,'lod':entry['lod'],'triangles':len(mesh.loop_triangles),
        'roles':dict(Counter(regions.data[p.index].value for p in mesh.polygons)),
        'source_invariant':'equal' if actual==baseline[obj.name] else 'different'})
blue=bpy.data.objects['Blue_Review_LOD0'];red=bpy.data.objects['Red_Review_LOD0'];original=bpy.data.objects['Original_Review_LOD0']
if not (blue.data==red.data==original.data):errors.append('Team previews do not share one canonical mesh')
if blue.matrix_world!=red.matrix_world or blue.matrix_world!=original.matrix_world:errors.append('Same-view comparison transforms differ')
for key,expected in [('Blue',1),('Red',1),('Original',0)]:
    mat=bpy.data.materials[f'M_Sweeper_{key}_OrangeOnly']
    if mat.node_tree.nodes['TeamEnabled'].outputs[0].default_value!=expected:errors.append(key+': enable mismatch')
    if mat.node_tree.nodes['OriginalThreeTone'].color_ramp.interpolation!='CONSTANT':errors.append(key+': three-tone missing')
    if not mat.node_tree.nodes['TeamPrimary'].outputs[0].is_linked:errors.append(key+': team parameter not connected')
    for node in mat.node_tree.nodes:
        if node.type=='TEX_IMAGE' and not node.image.packed_file:errors.append(key+': image not packed')
if hashlib.sha256(Path(report['source']).read_bytes()).hexdigest()!=report['source_sha256']:errors.append('Original .blend file changed')
if hashlib.sha256(Path(report['source_atlas']).read_bytes()).hexdigest()!=report['source_atlas_sha256']:errors.append('Original atlas changed')
for preview in report['previews']:
    if not (OUT/preview).is_file():errors.append('Missing preview '+preview)
result={'version':report['version'],'saved_blend_sha256':hashlib.sha256(Path(report['blend']).read_bytes()).hexdigest(),
    'lod_checks':checks,'blue_red_share_one_mesh':blue.data==red.data,
    'source_geometry_uv_normals_weights_transform_original_rgb':'equal',
    'packed_images':True,'reference_A':'User waived new drawings and selected original orange',
    'B_approval':'pending','formal_ue_import':False,'runtime_verified':False,'errors':errors}
(OUT/'blender-saved-readback.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print('SWEEPER_SAVED_READBACK',json.dumps(result,ensure_ascii=False),flush=True)
if errors:raise RuntimeError(errors)
