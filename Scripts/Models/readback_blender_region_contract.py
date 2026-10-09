"""Read the saved independent Alpha candidate and original B source in background Blender.

This is asset readback, not a gameplay test. No save, mesh edit, or interactive-session reload.
"""
import bpy
import hashlib
import json
from pathlib import Path

ROOT=Path(r'D:/UE5.7/test1')
OUT=ROOT/'ArtSource/ModelInterface_B_20261008'
contract=json.loads((OUT/'region-contract.json').read_text(encoding='utf8'))
ROLE='Bv1_ColorRegion'

def digest(value):return hashlib.sha256(json.dumps(value,separators=(',',':')).encode()).hexdigest()
def snapshot():
    values={}
    for scene in bpy.data.scenes:
        if not scene.name.startswith('Blue_'):continue
        for obj in scene.objects:
            if obj.type!='MESH' or ROLE not in obj.data.attributes:continue
            mesh=obj.data;colors=mesh.color_attributes.active_color
            values[obj.name]=dict(
                geometry_rgb_uv=digest(dict(positions=[list(v.co) for v in mesh.vertices],faces=[list(p.vertices) for p in mesh.polygons],
                    uv=[[list(d.uv) for d in layer.data] for layer in mesh.uv_layers],rgb=[list(c.color[:3]) for c in colors.data])),
                normals_weights_transform=digest(dict(normals=[list(v.normal) for v in mesh.vertices],
                    weights=[[(g.group,g.weight) for g in v.groups] for v in mesh.vertices],matrix=[list(r) for r in obj.matrix_world],
                    modifiers=[(m.name,m.type) for m in obj.modifiers])),
                roles=[mesh.attributes[ROLE].data[p.index].value for p in mesh.polygons])
    return values

bpy.ops.wm.open_mainfile(filepath=contract['source'])
original=snapshot()
bpy.ops.wm.open_mainfile(filepath=str(OUT/contract['blend']))
candidate=snapshot();errors=[]
if candidate!=original:errors.append('Saved candidate geometry/RGB/UV/normals/weights/transforms/roles differ from saved B source')
for obj in bpy.data.objects:
    if obj.name not in candidate:continue
    colors=obj.data.color_attributes.active_color
    for p in obj.data.polygons:
        role=obj.data.attributes[ROLE].data[p.index].value
        for index in p.loop_indices:
            if abs(colors.data[index].color[3]*255-role)>1e-5:errors.append(obj.name+': alpha role mismatch');break
    if any('Red_'==s.name[:4] for s in bpy.data.scenes):errors.append('Unexpected duplicated team mesh scenes')
checks=[]
for m in contract['models']:
    for mesh in m['meshes']:
        if candidate[mesh['object']]['geometry_rgb_uv']!=mesh['geometry_rgb_uv_sha256']:errors.append(mesh['object']+': contract invariant hash mismatch')
    checks.append({'model':m['name'],'meshes':len(m['meshes']),'saved_source_parity':'compared'})
shield=next(m for m in contract['models'] if m['name']=='ShieldGenerator')
lamp_faces=sum(int(m['roles'].get('7',0)) for m in shield['meshes'])
if lamp_faces!=150:errors.append('Shield top three lamps must contain 150 team-lamp faces')
report={'models':len(checks),'meshes':len(candidate),'source_parity':checks,'alpha_roles':'read from saved blend',
    'shield_three_team_lamps_faces':lamp_faces,'geometry_rgb_uv_normals_weights_transforms':'compared',
    'interactive_blender_touched':False,'files_modified':False,'formal_ue_import':False,'errors':errors}
(OUT/'blender-saved-readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'models':len(checks),'meshes':len(candidate),'shield_lamp_faces':lamp_faces,'errors':errors}))
