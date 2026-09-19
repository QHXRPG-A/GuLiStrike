"""Seat both front exhaust cartridges on the actual evaluated cockpit shell."""
import bpy, bmesh, json, hashlib, math
from pathlib import Path
from mathutils import Vector, Quaternion
from mathutils.bvhtree import BVHTree
import numpy as np

ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919')
OUT=ROOT/'Production_v9_VentMount'
(OUT/'Previews').mkdir(parents=True,exist_ok=True)
scene=bpy.data.scenes['Review_Mech_Lightest'];bpy.context.window.scene=scene
collection=bpy.data.collections['WORK_Mech_Lightest']
collection.hide_viewport=collection.hide_render=False;bpy.context.view_layer.update()
N=Vector((0,-.87461971,.48480962));N.normalize()
ink=bpy.data.materials['INK3_Outer_NoShadow']
gold=bpy.data.materials['V8_Head_Original_Ochre']
body=bpy.data.objects['WORK_Mech_Lightest__Cockpit_Jet']
baseline=json.loads((ROOT/'Production_v3_InkCel/production_report.json').read_text())
feedback_direction=Quaternion((-.23115986585617065,-.046226710081100464,.19056953489780426,.952948808670044))@Vector((0,0,1))

def digest(me):
    h=hashlib.sha256()
    for data,prop,n,dtype in ((me.vertices,'co',len(me.vertices)*3,np.float32),(me.loops,'vertex_index',len(me.loops),np.int32)):
        a=np.empty(n,dtype);data.foreach_get(prop,a);h.update(a.tobytes())
    return h.hexdigest()

original_hashes={o.name:digest(o.data) for o in collection.objects if o.type=='MESH'}
def frame(direction,center=None,scale=None):
    bounds=baseline['assets']['Mech_Lightest']['bounds'];lo,hi=Vector(bounds['min']),Vector(bounds['max'])
    center=Vector(center) if center is not None else (lo+hi)/2
    cam=scene.camera;cam.location=center+Vector(direction).normalized()*12
    rotation=(center-cam.location).to_track_quat('-Z','Y');cam.rotation_euler=rotation.to_euler()
    if scale is None:
        points=[Vector((x,y,z)) for x in (lo.x,hi.x) for y in (lo.y,hi.y) for z in (lo.z,hi.z)]
        right,up=rotation@Vector((1,0,0)),rotation@Vector((0,1,0))
        w=max(p.dot(right) for p in points)-min(p.dot(right) for p in points)
        h=max(p.dot(up) for p in points)-min(p.dot(up) for p in points)
        scale=max(w,h*scene.render.resolution_x/scene.render.resolution_y)*1.12
    cam.data.ortho_scale=scale

def render(label,direction,center=None,scale=None):
    frame(direction,center,scale)
    scene.render.filepath=str(OUT/'Previews'/('Mech_Lightest_'+label+'.png'))
    bpy.ops.render.render(write_still=True);print('VENT_SEAT_RENDERED '+label,flush=True)

render('BackTop_Before',feedback_direction,(0,-.23,3.15),3.10)
render('Mount_R_Before',feedback_direction,(.47,-.54,3.10),1.35)

evaluated=body.evaluated_get(bpy.context.evaluated_depsgraph_get());me=evaluated.to_mesh()
support=BVHTree.FromPolygons([v.co for v in me.vertices],[tuple(p.vertices) for p in me.polygons])
evaluated.to_mesh_clear()

def mesh(name,vertices,faces):
    data=bpy.data.meshes.new(name);data.from_pydata(vertices,[],faces);data.update()
    bm=bmesh.new();bm.from_mesh(data);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(data);bm.free()
    return data

def outline(obj,width=.24):
    bpy.context.view_layer.update()
    ev=obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
    data=bpy.data.meshes.new_from_object(ev)
    pos=np.empty(len(data.vertices)*3,np.float32);normals=np.empty_like(pos)
    data.vertices.foreach_get('co',pos);data.vertices.foreach_get('normal',normals)
    data.vertices.foreach_set('co',pos+normals*width);data.update()
    o=obj.copy();o.data=data;o.name='INK9_Outline_'+obj.name;o.modifiers.clear();collection.objects.link(o)
    data.materials.clear();data.materials.append(ink)
    for f in data.polygons:f.material_index=0
    o['ink_layer']='outer';o['width_world_m']=width*.01;o.visible_shadow=False;o.hide_select=True
    return o

modified=[];replaced_outlines=[]
for sign in (-1,1):
    prefix='WORK_Mech_Lightest__V8_FrontVent_'+str(sign)+'_'
    objects=[o for o in collection.objects if o.type=='MESH' and o.name.startswith(prefix)]
    assert len(objects)==7
    for o in objects:
        for v in o.data.vertices:
            v.co.x=sign*47+(v.co.x-sign*53.5)*.75
            v.co-=N*4
        o.data.update();o['production_revision']=9;modified.append(o.name)
    old=bpy.data.objects['INK8_Outline_'+prefix+'Frame'];replaced_outlines.append(old.name)
    bpy.data.objects.remove(old,do_unlink=True)
    outline(bpy.data.objects[prefix+'Frame'])

right=bpy.data.objects['WORK_Mech_Lightest__V8_FrontVent_1_Frame']
perimeter=[v.co.copy() for v in list(right.data.vertices)[:40]]
front=[];rear=[];surface_points=[];gaps=[]
for i,a in enumerate(perimeter):
    b=perimeter[(i+1)%len(perimeter)]
    steps=max(1,math.ceil((b-a).length/2))
    for j in range(steps):
        p=a.lerp(b,j/steps)
        point,normal,index,distance=support.ray_cast(p+N*40,-N,120)
        assert point is not None,tuple(p)
        # The front ring overlaps the closed cartridge back; the rear ring
        # follows the real mounting skin, with 1.8 cm of buried overlap.
        front.append(p+N*.35);rear.append(point-N*1.8);surface_points.append(point)
        gaps.append(distance-40)
count=len(front)
faces=[tuple(reversed(range(count))),tuple(count+i for i in range(count))]
faces.extend((i,(i+1)%count,count+(i+1)%count,count+i) for i in range(count))
mounts=[]
for sign,side in ((1,'R'),(-1,'L')):
    coords=[(sign*v.x,v.y,v.z) for v in front+rear]
    mount=right.copy();mount.data=mesh('V9_Conforming_Exhaust_Housing_'+side,coords,faces)
    mount.name='WORK_Mech_Lightest__V9_FrontVent_Housing_'+side;mount.modifiers.clear();collection.objects.link(mount)
    mount.data.materials.append(gold);mount['production_revision']=9
    mount['source_basis']='Continuous duct housing; rear perimeter projected into evaluated original cockpit shell'
    mount['contact_embed_cm']=1.8
    for p in mount.data.polygons:p.use_smooth=p.index>=2
    mod=mount.modifiers.new('Housing_Edge_Radius','BEVEL');mod.width=.28;mod.segments=2;mod.limit_method='ANGLE';mod.angle_limit=math.radians(35)
    outline(mount,.20);mounts.append(mount.name)

# Inspect actual contact on both sides, independent of object parenting.
contact={}
for sign,side in ((1,'R'),(-1,'L')):
    penetrations=[]
    for point in rear:
        p=Vector((sign*point.x,point.y,point.z))
        location,normal,index,distance=support.ray_cast(p+N*50,-N,130)
        assert location is not None,(side,tuple(p))
        penetrations.append((location-p).dot(N))
    assert min(penetrations)>1.0,(side,min(penetrations))
    contact[side]={'perimeter_samples':count,'unsupported_samples':0,'min_overlap_cm':min(penetrations),'max_overlap_cm':max(penetrations)}

closed=[]
for name in modified+mounts:
    bm=bmesh.new();bm.from_mesh(bpy.data.objects[name].data)
    assert all(e.is_manifold for e in bm.edges),name
    assert all(f.calc_area()>1e-9 for f in bm.faces),name
    bm.free();closed.append(name)

unchanged={name:h for name,h in original_hashes.items() if name not in modified and name not in replaced_outlines}
for name,h in unchanged.items():assert digest(bpy.data.objects[name].data)==h,name

for name,d in [('Hero',(1.5,-2,1.05)),('Front',(0,-1,0)),('Side',(1,0,0)),('Rear',(0,1,0))]:render(name,d)
render('Head_Close',(1.15,-2,1.0),(0,-.40,3.00),2.35)
render('Head_Profile',(1,0,0),(0,-.38,3.03),2.7)
render('Shoulder_Close',(2,-.60,.65),(1.10,.22,3.22),1.80)
render('Vent_Front',(0,-1,.32),(0,-.60,3.12),1.95)
render('BackTop',feedback_direction,(0,-.23,3.15),3.10)
render('Mount_R',feedback_direction,(.47,-.54,3.10),1.35)
render('Mount_L',(-feedback_direction.x,feedback_direction.y,feedback_direction.z),(-.47,-.54,3.10),1.35)
frame((1.5,-2,1.05))
for screen in bpy.data.screens:
    for area in screen.areas:
        if area.type=='VIEW_3D':
            area.spaces.active.shading.type='MATERIAL';area.spaces.active.region_3d.view_perspective='CAMERA';area.spaces.active.overlay.show_overlays=False
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Mechs_VentMount_v9.blend'),check_existing=False)
report={'success':True,'source_session_snapshot':str(OUT/'Source/SessionBeforeVentFix.blend'),'revision':9,
        'modified_existing_meshes':modified,'new_contact_housings':mounts,'old_outlines_replaced':replaced_outlines,
        'unchanged_mesh_hashes':unchanged,'side_center_x_before_cm':53.5,'side_center_x_after_cm':47,
        'width_factor':.75,'rearward_offset_cm':4,'rim_before_housing_gap_cm':{'min':min(gaps),'max':max(gaps)},
        'housing_ring_size':count,'contact':contact,'closed_revised_meshes':len(closed),
        'reference_direction':list(feedback_direction),'review_images':sorted(p.name for p in (OUT/'Previews').glob('*.png'))}
(OUT/'mount_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('VENT_MOUNT_V9_READY',flush=True)
