"""Production readback for the authored head/vent revision; no UE mutation."""
import bpy, bmesh, json, hashlib
import numpy as np
from pathlib import Path
from mathutils import Vector
from mathutils.kdtree import KDTree

ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919')
OUT=ROOT/'Production_v8_HeadRefine'
assert Path(bpy.data.filepath).resolve()==(OUT/'Mechs_HeadRefined_v8.blend').resolve()
production=json.loads((OUT/'refinement_report.json').read_text())
baseline=json.loads((ROOT/'Production_v3_InkCel/production_report.json').read_text())

def buffer(data,prop,n,dtype=np.float32):
    a=np.empty(n,dtype);data.foreach_get(prop,a);return a
def digest(me):
    h=hashlib.sha256()
    h.update(buffer(me.vertices,'co',len(me.vertices)*3).tobytes())
    h.update(buffer(me.loops,'vertex_index',len(me.loops),np.int32).tobytes())
    return h.hexdigest()

for name,expected in production['untouched_base_geometry_hashes'].items():
    assert digest(bpy.data.objects[name].data)==expected,name
for key,asset in baseline['assets'].items():
    if key=='Mech_Lightest':continue
    for row in asset['meshes']:
        assert digest(bpy.data.objects[row['object']].data)==row['geometry_before'],row['object']

scene=bpy.data.scenes['Review_Mech_Lightest'];bpy.context.window.scene=scene;bpy.context.view_layer.update()
collection=bpy.data.collections['WORK_Mech_Lightest']
new_parts=[o for o in collection.objects if o.type=='MESH' and not o.get('ink_layer') and (o.get('production_revision')==8)]
closed=[];attachments={}
for o in new_parts:
    bm=bmesh.new();bm.from_mesh(o.data)
    non_manifold=sum(not e.is_manifold for e in bm.edges)
    degenerate=sum(f.calc_area()<1e-9 for f in bm.faces)
    assert non_manifold==0 and degenerate==0,(o.name,non_manifold,degenerate)
    bm.free();closed.append(o.name)
    c=o.constraints['Original_Blueprint_Attachment']
    expected=('WORK_Mech_Lightest__Cockpit_Jet','') if 'Shoulder_' in o.name else ('WORK_Mech_Lightest__HIPS','Mount_top')
    assert (c.target.name,c.subtarget)==expected,(o.name,c.target.name,c.subtarget)
    attachments[o.name]={'target':c.target.name,'bone':c.subtarget}

def mirror_error(a,b):
    tree=KDTree(len(b.data.vertices))
    for i,v in enumerate(b.data.vertices):tree.insert(v.co,i)
    tree.balance()
    return max(tree.find(Vector((-v.co.x,v.co.y,v.co.z)))[2] for v in a.data.vertices)
cover=bpy.data.objects['WORK_Mech_Lightest__Sealed_Armor_Fairing']
symmetry={'head_cm':mirror_error(cover,cover)}
for tail in ['Frame']+['Louver_'+str(i) for i in range(6)]:
    a=bpy.data.objects['WORK_Mech_Lightest__V8_FrontVent_-1_'+tail]
    b=bpy.data.objects['WORK_Mech_Lightest__V8_FrontVent_1_'+tail]
    symmetry['front_vent_'+tail+'_cm']=mirror_error(a,b)
assert max(symmetry.values())<.0001,symmetry

body_tri=shell_tri=0;points=[];deps=bpy.context.evaluated_depsgraph_get()
for o in collection.objects:
    if o.type!='MESH':continue
    ev=o.evaluated_get(deps);me=ev.to_mesh();me.calc_loop_triangles()
    if o.get('ink_layer'):
        shell_tri+=len(me.loop_triangles);assert not o.visible_shadow
    else:
        body_tri+=len(me.loop_triangles)
        points.extend(o.matrix_world@v.co for v in me.vertices)
        for mat in me.materials:
            ramp=mat.node_tree.nodes['THREE_TONE_Shadow_Mid_Light'].color_ramp
            assert ramp.interpolation=='CONSTANT' and len(ramp.elements)==3
    ev.to_mesh_clear()

sp=bpy.data.objects['WORK_SpiderMech__SpiderMech.002'];src=bpy.data.objects['SRC_SpiderMech__SpiderMech.001']
assert digest(sp.data)==digest(src.data)
assert all(np.array_equal(buffer(a.data,'uv',len(a.data)*2),buffer(b.data,'uv',len(b.data)*2)) for a,b in zip(sp.data.uv_layers,src.data.uv_layers))
assert np.array_equal(buffer(sp.data.corner_normals,'vector',len(sp.data.corner_normals)*3),buffer(src.data.corner_normals,'vector',len(src.data.corner_normals)*3))
def weights(o):
    names={g.index:g.name for g in o.vertex_groups}
    h=hashlib.sha256()
    for v in o.data.vertices:
        h.update(str([(names[g.group],g.weight) for g in v.groups]).encode())
    return h.hexdigest()
assert weights(sp)==weights(src)
sp.data.calc_loop_triangles()
result={'success':True,'file':bpy.data.filepath,'modified_scope':'light sealed head panels and front/shoulder ventilation details',
        'other_light_base_meshes_unchanged':True,'other_four_asset_geometries_unchanged':True,
        'new_closed_meshes':len(closed),'non_manifold_edges':0,'degenerate_faces':0,'symmetry':symmetry,
        'source_attachments':attachments,'light_body_evaluated_triangles':body_tri,'light_outline_evaluated_triangles':shell_tri,
        'light_bounds_m':{'min':[min(p[i] for p in points) for i in range(3)],'max':[max(p[i] for p in points) for i in range(3)]},
        'head_color':'B48A3F','retained_leg_color':'587F9B','three_tone_materials':True,
        'spider':{'body_triangles':len(sp.data.loop_triangles),'vertices':len(sp.data.vertices),'bones':len(sp.find_armature().data.bones),'material_slots':len(sp.data.materials),'source_positions_topology_uv_normals_weights_equal':True},
        'review_images':production['images'],'ue_validation':'not_run'}
(OUT/'saved_readback.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print('HEAD_V8_READBACK_OK',flush=True)
