"""Apply the user's approved production color amendment without geometry edits."""
import bpy
import bmesh
import hashlib
import json
import math
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree

ROOT = Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
OUT = ROOT/'Production_B_v1'
assert not (OUT/'review_manifest.json').exists(), 'Published B version is frozen'
CID = 28551
obj = bpy.data.objects['RSGMech_LOD0_Body']
mesh = obj.data
def geometry_digest(data):
    values = [[tuple(v.co) for v in data.vertices], [tuple(p.vertices) for p in data.polygons]]
    return hashlib.sha256(json.dumps(values).encode()).hexdigest()
before = geometry_digest(mesh)
index = next(i for i,m in enumerate(mesh.materials) if m.name=='Reference_SkyBlue')
yellow = next(i for i,m in enumerate(mesh.materials) if m.name=='Reference_PaleYellow')
white = next(i for i,m in enumerate(mesh.materials) if m.name=='Reference_WarmWhite')
attribute = mesh.attributes['RSG_Component']
parts=json.loads((ROOT/'Baseline/source_connected_parts.json').read_text(encoding='utf-8'))
part=next(p for p in parts if p['component']==CID)
source=bpy.data.objects['SK_FPS_Mech.001']
ids=part['vertex_ids']; remap={vi:i for i,vi in enumerate(ids)}
points=[source.matrix_world @ source.data.vertices[vi].co for vi in ids]
polygons=[[remap[vi] for vi in source.data.polygons[pi].vertices] for pi in part['polygon_ids']]
temp=bpy.data.meshes.new('Head_Source_Panel_Classification')
temp.from_pydata(points,[],polygons)
bm=bmesh.new(); bm.from_mesh(temp)
original=bm.faces.layers.int.new('OriginalHeadPolygon')
for face in bm.faces: face[original]=face.index
bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.000005)
bm.normal_update()
visited=set(); candidates=[]
for seed in bm.faces:
    if seed in visited: continue
    stack=[seed]; patch=[]; visited.add(seed)
    while stack:
        face=stack.pop(); patch.append(face)
        for edge in face.edges:
            if len(edge.link_faces)!=2 or edge.calc_face_angle()>math.radians(20): continue
            for other in edge.link_faces:
                if other not in visited: visited.add(other); stack.append(other)
    if len(patch)>100:
        center=sum((f.calc_center_median() for f in patch),Vector())/len(patch)
        if abs(center.x)<.01 and center.y<-3.2:
            candidates.append(patch)
assert len(candidates)==1, 'User-marked central head panel must be a unique source crease-bounded patch'
panel_polygons={face[original] for face in candidates[0]}
assert len(panel_polygons)==170
bvh=BVHTree.FromPolygons(points,polygons,all_triangles=True)
def is_blue_panel(center):
    hit=bvh.find_nearest(center)
    return hit[2] in panel_polygons
changed = 0
restored=0
for face in mesh.polygons:
    if attribute.data[face.index].value==CID:
        face.material_index=index if is_blue_panel(face.center) else yellow
        changed += int(face.material_index==index)
    elif attribute.data[face.index].value==8853:
        face.material_index=white
        restored+=1
master = next(o for o in bpy.data.objects if o.get('SourceComponent')==CID)
master_index = next(i for i,m in enumerate(master.data.materials) if m.name=='Reference_SkyBlue')
master_yellow=next(i for i,m in enumerate(master.data.materials) if m.name=='Reference_PaleYellow')
for face in master.data.polygons:
    face.material_index=master_index if is_blue_panel(face.center) else master_yellow
master['ApprovedColor']='PaleYellow outer shell / SkyBlue recessed front panel'
master['ColorAmendment']='User marked the central front recessed panel in screenshot 2026-10-03'
mistaken=next(o for o in bpy.data.objects if o.get('SourceComponent')==8853)
mistaken_white=next(i for i,m in enumerate(mistaken.data.materials) if m.name=='Reference_WarmWhite')
for face in mistaken.data.polygons: face.material_index=mistaken_white
mistaken['ApprovedColor']='WarmWhite'
mistaken.name=mistaken.name.replace('SkyBlue','WarmWhite')
assert before==geometry_digest(mesh)
assert changed>0
report={
    'date':'2026-10-03', 'user_instruction':'头部中间那一块凹下去的那一块也改天蓝色',
    'reference_basis':'A-v3 approved; explicit user color amendment during production',
    'component':CID, 'description':'source crease-bounded central front panel on the round head, explicitly marked by the user',
    'source_panel_faces':len(panel_polygons), 'source_head_local_polygon_ids':sorted(panel_polygons),
    'from':'PaleYellow', 'to':'SkyBlue', 'srgb':'#87CEEB',
    'body_polygons_changed':changed, 'geometry_unchanged':True,
    'wrongly_assumed_hood_component_restored':8853, 'hood_restored_polygons':restored,
    'supersedes':'color_amendment_20261003.json',
    'user_annotation':'UserAnnotations/HeadFrontRecess_SkyBlue_User.png',
    'user_annotation_sha256':hashlib.sha256((OUT/'UserAnnotations/HeadFrontRecess_SkyBlue_User.png').read_bytes()).hexdigest(),
    'geometry_sha256_before':before, 'geometry_sha256_after':geometry_digest(mesh),
    'pale_yellow_outer_head_component':28551, 'outer_head_shell_kept_pale_yellow':True,
    'old_A_v3_artifacts_unchanged':True, 'B_approval':'pending', 'UE_saved':False
}
(OUT/'color_amendment_confirmed_20261003.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
bpy.context.scene['RSG_UserColorAmendment']=json.dumps(report,ensure_ascii=False)
bpy.data.meshes.remove(temp); bm.free()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'RSGMech_Editable_Geometry.blend'))
print(json.dumps({k:v for k,v in report.items() if k!='source_head_local_polygon_ids'},ensure_ascii=False),flush=True)
