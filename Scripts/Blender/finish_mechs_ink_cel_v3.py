"""Read back the saved art candidate and render its remaining review views."""
import bpy,json,hashlib,math
import numpy as np
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree

OUT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/Production_v3_InkCel')
report=json.loads((OUT/'production_report.json').read_text())
assert report['success']
def digest(mesh):
    h=hashlib.sha256()
    for data,prop,n,kind in [(mesh.vertices,'co',len(mesh.vertices)*3,np.float32),(mesh.loops,'vertex_index',len(mesh.loops),np.int32)]:
        a=np.empty(n,dtype=kind);data.foreach_get(prop,a);h.update(a.tobytes())
    return h.hexdigest()
def buffer_hash(data,prop,count,dtype=np.float32):
    a=np.empty(count,dtype=dtype);data.foreach_get(prop,a);return hashlib.sha256(a.tobytes()).hexdigest()
def weights(o):
    names={g.index:g.name for g in o.vertex_groups}
    return [[(names[g.group],round(g.weight,7)) for g in v.groups] for v in o.data.vertices]
def symmetric_paint(o):
    me=o.data;attr=me.color_attributes['SourceRegion_CleanPaint']
    left=[f for f in me.polygons if f.center.x<-.001]
    tree=BVHTree.FromPolygons([v.co for v in me.vertices],[tuple(f.vertices) for f in left])
    tol=max(max(v.co[i] for v in me.vertices)-min(v.co[i] for v in me.vertices) for i in range(3))*.0015
    differences=[]
    for f in me.polygons:
        if f.center.x<=.001:continue
        near=tree.find_nearest(Vector((-f.center.x,f.center.y,f.center.z)))
        if near[3]>tol:continue
        other=left[near[2]]
        if Vector((-f.normal.x,f.normal.y,f.normal.z)).dot(other.normal)<.97:continue
        differences.append(max(abs(attr.data[f.loop_start].color[i]-attr.data[other.loop_start].color[i]) for i in range(4)))
    return {'matched_faces':len(differences),'max_color_difference':max(differences)}

readback={'source':bpy.data.filepath,'assets':{},'symmetry':{}}
for key,asset in report['assets'].items():
    bpy.context.window.scene=bpy.data.scenes['Review_'+key];bpy.context.view_layer.update()
    deps=bpy.context.evaluated_depsgraph_get();base_tri=0;shell_tri=0
    for row in asset['meshes']:
        o=bpy.data.objects[row['object']]
        assert digest(o.data)==row['geometry_before'],o.name
        evaluated=o.evaluated_get(deps);me=evaluated.to_mesh();me.calc_loop_triangles();base_tri+=len(me.loop_triangles);evaluated.to_mesh_clear()
        for mat in o.data.materials:
            ramp=mat.node_tree.nodes['THREE_TONE_Shadow_Mid_Light'].color_ramp
            assert ramp.interpolation=='CONSTANT' and len(ramp.elements)==3
        if 'outline' in row:
            shell=bpy.data.objects[row['outline']];assert not shell.visible_shadow
            shell.data.calc_loop_triangles();shell_tri+=len(shell.data.loop_triangles)
    readback['assets'][key]={'body_geometry_hashes_match':True,'body_evaluated_triangles':base_tri,'outline_triangles':shell_tri,'three_tone_materials':True}

src=bpy.data.objects['SRC_SpiderMech__SpiderMech.001'];sp=bpy.data.objects['WORK_SpiderMech__SpiderMech.002']
sp.data.calc_loop_triangles()
readback['spider']={'vertices':len(sp.data.vertices),'body_triangles':len(sp.data.loop_triangles),'source_positions_topology_equal':digest(src.data)==digest(sp.data),
    'uv_equal':all(buffer_hash(a.data,'uv',len(a.data)*2)==buffer_hash(b.data,'uv',len(b.data)*2) for a,b in zip(src.data.uv_layers,sp.data.uv_layers)),
    'material_indices_equal':buffer_hash(src.data.polygons,'material_index',len(src.data.polygons),np.int32)==buffer_hash(sp.data.polygons,'material_index',len(sp.data.polygons),np.int32),
    'weights_equal':weights(src)==weights(sp),
    'corner_normals_equal':buffer_hash(src.data.corner_normals,'vector',len(src.data.corner_normals)*3)==buffer_hash(sp.data.corner_normals,'vector',len(sp.data.corner_normals)*3),
    'material_slots':len(sp.data.materials),'body_modifiers':[m.type for m in sp.modifiers],'bones':len(sp.find_armature().data.bones)}
assert all(readback['spider'][k] for k in ('source_positions_topology_equal','uv_equal','material_indices_equal','weights_equal','corner_normals_equal'))
for name in ('WORK_Mech_Lightest__Mech_Legs_Lt.001','WORK_Mech_Lightest__Cockpit_Jet'):
    readback['symmetry'][name]=symmetric_paint(bpy.data.objects[name]);assert readback['symmetry'][name]['max_color_difference']==0
(OUT/'saved_readback.json').write_text(json.dumps(readback,indent=2),encoding='utf-8')
print('SAVED_READBACK_OK',flush=True)

def frame(key,view,close=False):
    scene=bpy.data.scenes['Review_'+key];bpy.context.window.scene=scene;bpy.context.view_layer.update();cam=scene.camera
    bounds=report['assets'][key]['bounds'];lo=Vector(bounds['min']);hi=Vector(bounds['max']);center=(lo+hi)/2;span=max(hi-lo)
    d={'Hero':Vector((1.5,-2,1.05)),'Front':Vector((0,-1,0)),'Side':Vector((1,0,0)),'Rear':Vector((0,1,0))}[view].normalized()
    cam.location=center+d*span*3;rot=(center-cam.location).to_track_quat('-Z','Y');cam.rotation_euler=rot.to_euler();cam.data.type='ORTHO'
    pts=[Vector((x,y,z)) for x in (lo.x,hi.x) for y in (lo.y,hi.y) for z in (lo.z,hi.z)]
    right=rot@Vector((1,0,0));up=rot@Vector((0,1,0))
    w=max(p.dot(right) for p in pts)-min(p.dot(right) for p in pts);h=max(p.dot(up) for p in pts)-min(p.dot(up) for p in pts)
    cam.data.ortho_scale=max(w,h*scene.render.resolution_x/scene.render.resolution_y)*1.12
    if close:cam.data.ortho_scale*=.62
    return scene
def render(key,view,suffix=None,close=False):
    scene=frame(key,view,close);scene.render.filepath=str(OUT/'Previews'/(key+'_'+(suffix or view)+'.png'));bpy.ops.render.render(write_still=True)
for key in report['assets']:
    for view in ('Front','Side','Rear'):
        if key=='Mech_Lightest' and view=='Rear':continue
        render(key,view)
render('SpiderMech','Hero','Close',True)

# Rear paint proof: bypass the three lighting bands only for this evidence render.
scene=frame('Mech_Lightest','Rear');changed=[]
for o in bpy.data.collections['WORK_Mech_Lightest'].objects:
    if o.type!='MESH' or o.get('ink_layer'):continue
    for mat in o.data.materials:
        ramp=mat.node_tree.nodes['THREE_TONE_Shadow_Mid_Light'].color_ramp
        for e in ramp.elements:changed.append((e,e.color[:]));e.color=(1,1,1,1)
scene.render.filepath=str(OUT/'Previews/Mech_Lightest_Rear_Paint.png');bpy.ops.render.render(write_still=True)
for e,c in changed:e.color=c
frame('Mech_Lightest','Rear')
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.shading.type='MATERIAL';area.spaces.active.region_3d.view_perspective='CAMERA';area.spaces.active.overlay.show_overlays=False
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Mechs_InkCel_v3.blend'),check_existing=False)
readback['review_images']=sorted(p.name for p in (OUT/'Previews').glob('*.png'))
readback['render_delivery_complete']=True
(OUT/'saved_readback.json').write_text(json.dumps(readback,indent=2),encoding='utf-8')
print('INK_CEL_REVIEW_COMPLETE',flush=True)
