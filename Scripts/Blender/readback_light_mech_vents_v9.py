"""Read the saved exhaust fix: actual seating, symmetry and original binding."""
import bpy, bmesh, json, hashlib
import numpy as np
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919')
OUT=ROOT/'Production_v9_VentMount'
assert Path(bpy.data.filepath).resolve()==(OUT/'Mechs_VentMount_v9.blend').resolve()
production=json.loads((OUT/'mount_report.json').read_text())
scene=bpy.data.scenes['Review_Mech_Lightest'];bpy.context.window.scene=scene
collection=bpy.data.collections['WORK_Mech_Lightest']
bpy.context.view_layer.update();deps=bpy.context.evaluated_depsgraph_get()

def digest(me):
    h=hashlib.sha256()
    for data,prop,n,dtype in ((me.vertices,'co',len(me.vertices)*3,np.float32),(me.loops,'vertex_index',len(me.loops),np.int32)):
        a=np.empty(n,dtype);data.foreach_get(prop,a);h.update(a.tobytes())
    return h.hexdigest()

for name,h in production['unchanged_mesh_hashes'].items():
    assert digest(bpy.data.objects[name].data)==h,name
baseline=json.loads((ROOT/'Production_v3_InkCel/production_report.json').read_text())
for key,asset in baseline['assets'].items():
    if key=='Mech_Lightest':continue
    for row in asset['meshes']:
        assert digest(bpy.data.objects[row['object']].data)==row['geometry_before'],row['object']

body=bpy.data.objects['WORK_Mech_Lightest__Cockpit_Jet']
ev=body.evaluated_get(deps);me=ev.to_mesh()
support=BVHTree.FromPolygons([v.co for v in me.vertices],[tuple(p.vertices) for p in me.polygons])
ev.to_mesh_clear()
N=Vector((0,-.87461971,.48480962));N.normalize()
contact={};closed=[];attachments={}
for name in production['modified_existing_meshes']+production['new_contact_housings']:
    o=bpy.data.objects[name];bm=bmesh.new();bm.from_mesh(o.data)
    assert all(e.is_manifold for e in bm.edges),name
    assert all(f.calc_area()>1e-9 for f in bm.faces),name
    bm.free();closed.append(name)
    c=o.constraints['Original_Blueprint_Attachment']
    assert (c.target.name,c.subtarget)==('WORK_Mech_Lightest__HIPS','Mount_top')
    assert max(abs(o.matrix_world[r][c]-body.matrix_world[r][c]) for r in range(4) for c in range(4))<1e-6
    attachments[name]={'target':c.target.name,'bone':c.subtarget}
    if name in production['new_contact_housings']:
        rear=list(o.data.vertices)[production['housing_ring_size']:]
        overlaps=[]
        for v in rear:
            hit,normal,index,distance=support.ray_cast(v.co+N*50,-N,130)
            assert hit is not None,(name,tuple(v.co))
            overlaps.append((hit-v.co).dot(N))
        assert min(overlaps)>1.0,(name,min(overlaps))
        contact[name]={'samples':len(rear),'unsupported':0,'min_overlap_cm':min(overlaps),'max_overlap_cm':max(overlaps)}

def mirror_error(a,b):
    tree=KDTree(len(b.data.vertices))
    for i,v in enumerate(b.data.vertices):tree.insert(v.co,i)
    tree.balance()
    return max(tree.find(Vector((-v.co.x,v.co.y,v.co.z)))[2] for v in a.data.vertices)
symmetry={}
for tail in ['Frame']+['Louver_'+str(i) for i in range(6)]:
    symmetry[tail]=mirror_error(bpy.data.objects['WORK_Mech_Lightest__V8_FrontVent_-1_'+tail],bpy.data.objects['WORK_Mech_Lightest__V8_FrontVent_1_'+tail])
symmetry['housing']=mirror_error(bpy.data.objects['WORK_Mech_Lightest__V9_FrontVent_Housing_L'],bpy.data.objects['WORK_Mech_Lightest__V9_FrontVent_Housing_R'])
assert max(symmetry.values())<.0001,symmetry

body_tri=shell_tri=0
for o in collection.objects:
    if o.type!='MESH':continue
    ev=o.evaluated_get(deps);me=ev.to_mesh();me.calc_loop_triangles()
    if o.get('ink_layer'):
        shell_tri+=len(me.loop_triangles);assert not o.visible_shadow
    else:
        body_tri+=len(me.loop_triangles)
        for mat in me.materials:
            ramp=mat.node_tree.nodes['THREE_TONE_Shadow_Mid_Light'].color_ramp
            assert ramp.interpolation=='CONSTANT' and len(ramp.elements)==3
    ev.to_mesh_clear()

result={'success':True,'file':bpy.data.filepath,'scope':'Seat the paired front exhausts with continuous shell-conforming housings',
        'contact_on_saved_geometry':contact,'symmetry_max_error_cm':max(symmetry.values()),'symmetry':symmetry,
        'closed_revised_meshes':len(closed),'non_manifold_edges':0,'degenerate_faces':0,'attachments':attachments,
        'light_body_evaluated_triangles':body_tri,'light_outline_evaluated_triangles':shell_tri,
        'other_light_meshes_unchanged':True,'other_four_asset_geometry_unchanged':True,
        'three_tone_materials':True,'ue_validation':'not_run'}
(OUT/'saved_readback.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print('VENT_MOUNT_V9_READBACK_OK',flush=True)
