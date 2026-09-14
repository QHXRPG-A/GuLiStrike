"""Prepare PBR assets, rigid cannon rig, exports, and reproducible checks.
Run in a fresh background Blender process.
"""
import sys
import math
import json
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from blender_common import *
import bmesh
import numpy as np
from mathutils.bvhtree import BVHTree
from mathutils.geometry import barycentric_transform

OUT=ROOT/'delivery'
OUT.mkdir(exist_ok=True)
REPORT={'assets':{},'validation':{}}

def pixel_array(image):
    values=np.empty(len(image.pixels),dtype=np.float32)
    image.pixels.foreach_get(values)
    return values.reshape(-1,4)

def image_from_pixels(name,wh,pixels,colorspace='Non-Color'):
    image=bpy.data.images.new(name,width=wh[0],height=wh[1],alpha=False)
    image.colorspace_settings.name=colorspace
    image.pixels.foreach_set(pixels.reshape(-1))
    image.update()
    return image

def save_texture(image,destination):
    image.filepath_raw=str(destination)
    image.file_format='PNG'
    image.save()
    image.pack()

def prepare_pbr(name,mat):
    folder=OUT/name/'Textures'
    folder.mkdir(parents=True,exist_ok=True)
    mat.name='M_'+name
    nodes=mat.node_tree.nodes
    links=mat.node_tree.links
    bs=next(n for n in nodes if n.type=='BSDF_PRINCIPLED')
    image_nodes=[n for n in nodes if n.type=='TEX_IMAGE' and n.image]
    maps={}
    for n in image_nodes:
        old=n.image.name.lower()
        if 'normal' in old:
            role='NormalGL'
        elif 'orm' in old:
            role='ORM'
        else:
            role='BaseColor'
        n.image.name='T_'+name+'_'+role
        n.label=role
        maps[role]=n.image
    orm=maps['ORM']
    values=pixel_array(orm)
    # The generated surfaces are too polished relative to the worn painted references.
    values[:,1]=np.clip(.38+.55*values[:,1],.38,.90)
    values[:,2]=np.minimum(values[:,2],.8)
    corrected=image_from_pixels('T_'+name+'_ORM_Final',orm.size[:],values)
    for n in image_nodes:
        if n.image==orm:
            n.image=corrected
    maps['ORM']=corrected
    for n in nodes:
        if n.type=='NORMAL_MAP':
            n.inputs['Strength'].default_value=.5
    # Native normal textures for both Blender/glTF (OpenGL) and Unreal (DirectX).
    normal=maps['NormalGL']
    normal_values=pixel_array(normal)
    normal_values[:,0:2]=.5+(normal_values[:,0:2]-.5)*.5
    corrected_normal=image_from_pixels('T_'+name+'_NormalGL_Final',normal.size[:],normal_values)
    for n in image_nodes:
        if n.image==normal:
            n.image=corrected_normal
    maps['NormalGL']=corrected_normal
    for n in nodes:
        if n.type=='NORMAL_MAP':
            n.inputs['Strength'].default_value=1.0
    dx=normal_values.copy()
    dx[:,1]=1-dx[:,1]
    maps['NormalDX']=image_from_pixels('T_'+name+'_NormalDX',normal.size[:],dx)
    # Separate scalar maps let FBX consumers reconstruct the same PBR material.
    for channel,role in [(1,'Roughness'),(2,'Metallic')]:
        channel_pixels=values.copy()
        channel_pixels[:,:3]=values[:,channel,None]
        maps[role]=image_from_pixels('T_'+name+'_'+role,orm.size[:],channel_pixels)
    # Only the bright cyan / lime status lamps become emissive. Painted teal stays opaque.
    color=maps['BaseColor']
    p=pixel_array(color)
    r,g,b=p[:,0],p[:,1],p[:,2]
    cyan=(g>.62)&(b>.55)&(r<g*.62)
    lime=(g>.65)&(g>r*1.25)&(b<g*.52)
    mask=cyan|lime
    em=np.zeros_like(p)
    em[:,3]=1
    em[mask,:3]=p[mask,:3]
    maps['Emissive']=image_from_pixels('T_'+name+'_Emissive',color.size[:],em,'sRGB')
    e=nodes.new('ShaderNodeTexImage')
    e.image=maps['Emissive']
    e.label='Status lamps'
    e.location=(-500,-620)
    links.new(e.outputs['Color'],bs.inputs['Emission Color'])
    bs.inputs['Emission Strength'].default_value=1.8
    for role,image in maps.items():
        save_texture(image,folder/('T_'+name+'_'+role+'.png'))
    return {'material':mat.name,'texture_resolution':list(color.size),'textures':{
        role:'Textures/T_'+name+'_'+role+'.png' for role in maps},'emissive_pixel_fraction':float(mask.mean())}

def restore_atlas(source,parts):
    """Segmentation preserves triangles; transfer original 4K UVs, avoiding 16 small atlases."""
    mesh=source.data
    mesh.calc_loop_triangles()
    triangles=list(mesh.loop_triangles)
    verts=[v.co.copy() for v in mesh.vertices]
    tree=BVHTree.FromPolygons(verts,[tuple(t.vertices) for t in triangles],all_triangles=True)
    original_uv=mesh.uv_layers.active.data
    max_error=0.0
    for ob in parts:
        uv=ob.data.uv_layers.active or ob.data.uv_layers.new(name='UVMap')
        for poly in ob.data.polygons:
            center=sum((ob.data.vertices[i].co for i in poly.vertices),Vector())/len(poly.vertices)
            loc,norm,idx,dist=tree.find_nearest(center)
            if idx is None:
                raise RuntimeError('UV transfer found no original triangle')
            max_error=max(max_error,dist)
            tri=triangles[idx]
            points=[verts[i] for i in tri.vertices]
            coords=[Vector((*original_uv[i].uv,0)) for i in tri.loops]
            for li in poly.loop_indices:
                v=ob.data.vertices[ob.data.loops[li].vertex_index].co
                uv.data[li].uv=barycentric_transform(v,*points,*coords).xy
        ob.data.materials.clear()
        ob.data.materials.append(source.active_material)
        for poly in ob.data.polygons:
            poly.material_index=0
    return max_error

def join_parts(parts,name):
    activate(parts)
    bpy.ops.object.join()
    ob=bpy.context.object
    ob.name=name
    bm=bmesh.new()
    bm.from_mesh(ob.data)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=.000015)
    bm.to_mesh(ob.data)
    bm.free()
    ob.data.update()
    return ob

def cap_connections(ob,interior):
    # Only inter-group openings remain after welding all static seams within a group.
    if interior.name not in ob.data.materials:
        ob.data.materials.append(interior)
    interior_index=list(ob.data.materials).index(interior)
    bm=bmesh.new()
    bm.from_mesh(ob.data)
    # Generated meshes may contain tiny triangle flaps glued onto otherwise manifold edges.
    flaps=[f for f in bm.faces if any(e.is_boundary for e in f.edges)
           and any(len(e.link_faces)>2 for e in f.edges) and f.calc_area()<.0001]
    if flaps:
        bmesh.ops.delete(bm,geom=flaps,context='FACES')
    loose=[e for e in bm.edges if not e.link_faces]
    if loose:
        bmesh.ops.delete(bm,geom=loose,context='EDGES')
    seen=set()
    made=[]
    for edge in list(bm.edges):
        if not edge.is_valid or not edge.is_boundary or edge in seen:
            continue
        stack=[edge]
        component=set()
        while stack:
            e=stack.pop()
            if e in component:
                continue
            component.add(e)
            for v in e.verts:
                stack.extend(x for x in v.link_edges if x.is_boundary and x not in component)
        seen|=component
        vertices={v for e in component for v in e.verts}
        filled=[]
        if all(sum(e in component for e in v.link_edges)==2 for v in vertices):
            start=next(iter(vertices)); cur=start; previous=None; ordered=[]
            while True:
                ordered.append(cur)
                options=[e.other_vert(cur) for e in cur.link_edges if e in component and e.other_vert(cur)!=previous]
                if not options:
                    break
                nxt=options[0]
                if nxt==start:
                    break
                previous,cur=cur,nxt
            try:
                filled=[bm.faces.new(ordered)]
            except ValueError:
                filled=[]
        if not filled:
            filled=bmesh.ops.holes_fill(bm,edges=list(component),sides=0).get('faces',[])
        made.extend(filled)
    uv=bm.loops.layers.uv.active
    for f in made:
        f.material_index=interior_index
        f.smooth=False
        if uv:
            for loop in f.loops:
                loop[uv].uv=(0.5,0.5)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bm.to_mesh(ob.data)
    ob.data.update()
    remaining=sum(e.is_boundary for e in bm.edges)
    nonmanifold=sum(not e.is_manifold for e in bm.edges)
    count=len(made)
    bm.free()
    return {'filled_connection_faces':count,'remaining_boundary_edges':remaining,'nonmanifold_edges':nonmanifold}

def seal_connections(ob,interior):
    history=[]
    for iteration in range(5):
        check=cap_connections(ob,interior)
        history.append(check)
        if check['remaining_boundary_edges']==0 and check['nonmanifold_edges']==0:
            break
    return {**history[-1],'filled_connection_faces':sum(x['filled_connection_faces'] for x in history),'passes':len(history)}

def bearing_collar():
    # A machined stationary housing conceals the irregular seam between the generated turntable and foundation.
    count=96
    coords=[]
    for z,r in [(.53,1.69),(.68,1.69),(.68,1.43),(.53,1.43)]:
        coords.extend((r*math.cos(2*math.pi*i/count),r*math.sin(2*math.pi*i/count),z) for i in range(count))
    faces=[]
    for ring in range(4):
        for i in range(count):
            j=(i+1)%count
            faces.append((ring*count+i,ring*count+j,((ring+1)%4)*count+j,((ring+1)%4)*count+i))
    mesh=bpy.data.meshes.new('BearingHousingMesh')
    mesh.from_pydata(coords,[],faces)
    mesh.update()
    ob=bpy.data.objects.new('BearingHousing',mesh)
    bpy.context.scene.collection.objects.link(ob)
    ob.data.materials.append(material('M_Cannon_BearingHousing',(.16,.12,.065),.72,.58))
    activate([ob])
    bevel=ob.modifiers.new('Machined edges','BEVEL')
    bevel.width=.016
    bevel.segments=2
    bpy.ops.object.modifier_apply(modifier=bevel.name)
    uv=ob.data.uv_layers.new(name='UVMap')
    for p in ob.data.polygons:
        for li in p.loop_indices:
            v=ob.data.vertices[ob.data.loops[li].vertex_index].co
            uv.data[li].uv=(v.x/3.5+.5,v.y/3.5+.5)
    return ob

def make_rig(scene,groups):
    arm=bpy.data.armatures.new('SKEL_HeavyDefenseCannon')
    rig=bpy.data.objects.new('Armature',arm)
    scene.collection.objects.link(rig)
    rig.show_in_front=True
    arm.display_type='STICK'
    activate([rig])
    bpy.ops.object.mode_set(mode='EDIT')
    for name,head,parent in [('root',(0,0,0),None),('base_yaw',(0,0,.60),'root'),
                             ('barrel_pitch',(0,0,2.12),'base_yaw')]:
        bone=arm.edit_bones.new(name)
        bone.head=head
        bone.tail=Vector(head)+Vector((0,.65,0))
        if parent:
            bone.parent=arm.edit_bones[parent]
            bone.use_connect=False
    bpy.ops.object.mode_set(mode='OBJECT')
    for name,pb in rig.pose.bones.items():
        pb.rotation_mode='XYZ'
        pb.lock_location=(True,True,True)
        pb.lock_scale=(True,True,True)
        pb.lock_rotation=(True,True,True) if name=='root' else ((True,True,False) if name=='base_yaw' else (True,False,True))
    rig.pose.bones['base_yaw'].color.palette='THEME04'
    rig.pose.bones['barrel_pitch'].color.palette='THEME03'
    pitch=rig.pose.bones['barrel_pitch']
    limit=pitch.constraints.new('LIMIT_ROTATION')
    limit.name='Mechanical travel: -10 to +40 degrees elevation'
    limit.owner_space='LOCAL'
    limit.use_limit_x=True
    limit.use_limit_y=True
    limit.use_limit_z=True
    limit.min_y=-math.radians(40)
    limit.max_y=math.radians(10)
    for bone,ob in groups.items():
        group=ob.vertex_groups.new(name=bone)
        group.add(list(range(len(ob.data.vertices))),1.0,'REPLACE')
        modifier=ob.modifiers.new('Rigid mechanical binding','ARMATURE')
        modifier.object=rig
        modifier.use_vertex_groups=True
        modifier.use_deform_preserve_volume=False
        ob.parent=rig
        ob.matrix_parent_inverse=Matrix.Identity(4)
        ob['RigidBone']=bone
    rig['RigDescription']='root fixed feet; base_yaw local Z unrestricted; barrel_pitch local Y, negative = raise barrels.'
    rig['ElevationMinimumDegrees']=-10.0
    rig['ElevationMaximumDegrees']=40.0
    rig['YawMinimumDegrees']=-180.0
    rig['YawMaximumDegrees']=180.0
    rig['YawWraps']=True
    rig['ForwardAxis']='+X'
    rig['UpAxis']='+Z'
    rig['Units']='meters; dimensions are authoring assumptions, not measured from drawings'
    return rig

def pose(rig,yaw=0,elevation=0):
    rig.pose.bones['root'].rotation_euler=(0,0,0)
    rig.pose.bones['base_yaw'].rotation_euler=(0,0,math.radians(yaw))
    rig.pose.bones['barrel_pitch'].rotation_euler=(0,-math.radians(elevation),0)
    bpy.context.view_layer.update()

def validate_rig(rig,groups):
    result={'bone_hierarchy':{b.name:b.parent.name if b.parent else None for b in rig.data.bones},
            'unweighted_vertices':0,'non_rigid_vertices':0,'poses':[]}
    for name,ob in groups.items():
        for v in ob.data.vertices:
            valid=[g for g in v.groups if g.weight>0]
            result['unweighted_vertices']+=not valid
            result['non_rigid_vertices']+=not(len(valid)==1 and abs(valid[0].weight-1)<1e-6)
    for yaw in [0,90,180,270]:
        for elevation in [-10,0,20,40]:
            pose(rig,yaw,elevation)
            dg=bpy.context.evaluated_depsgraph_get()
            errors=[]
            ground_min=1000
            for name,ob in groups.items():
                evaluated=ob.evaluated_get(dg)
                mesh=evaluated.to_mesh()
                expected=rig.pose.bones[name].matrix @ rig.data.bones[name].matrix_local.inverted()
                for idx in range(0,len(ob.data.vertices),max(1,len(ob.data.vertices)//80)):
                    errors.append((mesh.vertices[idx].co-expected @ ob.data.vertices[idx].co).length)
                if name=='barrel_pitch':
                    ground_min=min(v.co.z for v in mesh.vertices)
                evaluated.to_mesh_clear()
            result['poses'].append({'yaw_deg':yaw,'elevation_deg':elevation,
                                    'max_rigid_transform_error_m':max(errors),'barrel_lowest_z_m':ground_min})
    pose(rig)
    result['passed']=(result['unweighted_vertices']==0 and result['non_rigid_vertices']==0
                      and max(p['max_rigid_transform_error_m'] for p in result['poses'])<.0001
                      and min(p['barrel_lowest_z_m'] for p in result['poses'])>0)
    return result

def export_asset(scene,objects,name,rig=None,animation=False):
    bpy.context.window.scene=scene
    folder=OUT/name
    folder.mkdir(exist_ok=True)
    selected=([rig] if rig else [])+objects
    activate(selected)
    base=('SK_' if rig else 'SM_')+name+('_AimDemo' if animation else '')
    bpy.ops.export_scene.fbx(filepath=str(folder/(base+'.fbx')),use_selection=True,
        object_types={'MESH','ARMATURE'} if rig else {'MESH'},global_scale=1,apply_unit_scale=True,
        apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',
        bake_space_transform=False,add_leaf_bones=False,use_armature_deform_only=True,
        bake_anim=animation,bake_anim_use_nla_strips=False,bake_anim_use_all_actions=False,
        bake_anim_simplify_factor=0,mesh_smooth_type='FACE',use_tspace=True,
        use_triangles=True,path_mode='COPY',embed_textures=True)
    bpy.ops.export_scene.gltf(filepath=str(folder/(base+'.glb')),export_format='GLB',use_selection=True,use_active_scene=True,
        export_animations=animation,export_skins=True,export_yup=True,
        export_apply=False,export_materials='EXPORT',export_extras=True)
    return base

def create_demo(scene,rig):
    scene.render.fps=30
    scene.frame_start=1
    scene.frame_end=181
    keys=[(1,0,0),(31,90,15),(61,180,35),(91,270,15),(121,360,0),(151,360,-10),(181,360,0)]
    for f,y,p in keys:
        scene.frame_set(f)
        pose(rig,y,p)
        for name in ['root','base_yaw','barrel_pitch']:
            rig.pose.bones[name].keyframe_insert(data_path='rotation_euler',frame=f,group=name)
    rig.animation_data.action.name='A_HeavyDefenseCannon_AimDemo'
    rig.animation_data.action.use_fake_user=True
    for f,label in [(1,'Neutral / bind'),(31,'Yaw 90 / elevate 15'),(61,'Yaw 180 / elevate 35'),
                    (91,'Yaw 270 / elevate 15'),(121,'Yaw 360'),(151,'Depress -10'),(181,'Neutral / loop')]:
        marker=scene.timeline_markers.new(label)
        marker.frame=f
    scene.frame_set(1)

def main():
    completed=[]
    # Static assets are grounded, normalized, and retain their original full-resolution atlas.
    for name,span,axis in [('RedOreRefinery',12.0,None),('ShieldGenerator',5.0,2)]:
        scene,objects=load_asset(name,ROOT/'raw'/(name+'.glb'),span,axis)
        objects[0].name='SM_'+name
        REPORT['assets'][name]=describe(objects)
        REPORT['assets'][name].update(prepare_pbr(name,objects[0].active_material))
        setup_studio(scene,objects,res=(1500,1200),samples=48)
        export_asset(scene,objects,name)
        activate(objects)
        pack_save(scene,OUT/name/(name+'.blend'))
        render(scene,OUT/name/(name+'_Preview.png'))
        completed.append((scene,objects,None))
        print('FINISHED '+name,flush=True)
    name='HeavyDefenseCannon'
    source_scene,source_objects=load_asset('UV_Source',ROOT/'raw'/(name+'.glb'),8)
    source=source_objects[0]
    scene,parts=load_asset(name,ROOT/'raw'/(name+'_Segmented.glb'),8)
    REPORT['validation']['atlas_transfer_max_distance_m']=restore_atlas(source,parts)
    mat=source.active_material
    REPORT['assets'][name]=prepare_pbr(name,mat)
    # Original generation points down -Y. Rotate to +X and put the turntable center at X/Y zero.
    # Inspection measured the circular bearing center at original (0, 1.41) and axle Z=2.12 m.
    rotation=Matrix.Rotation(math.pi/2,4,'Z')
    center=Vector((0,1.41,0))
    for ob in parts:
        for v in ob.data.vertices:
            v.co=rotation @ (v.co-center)
        ob.data.update()
    groups={
        'root':join_parts([parts[i] for i in (6,11)],'Cannon_FixedFoundation'),
        'base_yaw':join_parts([parts[i] for i in (5,7,8)],'Cannon_RotatingBase'),
        'barrel_pitch':join_parts([parts[i] for i in (0,1,2,3,4,9,10,12,13,14,15)],'Cannon_PitchAssembly')
    }
    groups['root']=join_parts([groups['root'],bearing_collar()],'Cannon_FixedFoundation')
    interior=material('M_Cannon_JointInterior',(.05,.063,.077),.72,.56)
    REPORT['validation']['connection_caps']={k:seal_connections(o,interior) for k,o in groups.items()}
    rig=make_rig(scene,groups)
    objects=list(groups.values())
    REPORT['validation']['rig']=validate_rig(rig,groups)
    if not REPORT['validation']['rig']['passed']:
        raise RuntimeError('Rig validation failed')
    REPORT['assets'][name].update(describe(objects))
    REPORT['assets'][name]['bones']=['root','base_yaw','barrel_pitch']
    REPORT['assets'][name]['pitch_limits_deg']=[-10,40]
    REPORT['assets'][name]['yaw']='continuous 360 degrees'
    camera,target,size,stage=setup_studio(scene,objects,res=(1500,1200),samples=48)
    camera.location=target+Vector((1.35,1.9,1.2))*size
    aim(camera,target)
    export_asset(scene,objects,name,rig)
    render(scene,OUT/name/(name+'_Preview.png'))
    pose(rig,35,35)
    render(scene,OUT/name/(name+'_Aimed.png'))
    pose(rig)
    create_demo(scene,rig)
    export_asset(scene,objects,name,rig,animation=True)
    activate([rig])
    pack_save(scene,OUT/name/(name+'_Rigged.blend'))
    completed.append((scene,objects,rig))
    # Save a convenient master file containing a dedicated scene for each asset.
    for sc in list(bpy.data.scenes):
        if sc not in [x[0] for x in completed]:
            bpy.data.scenes.remove(sc)
    bpy.context.window.scene=scene
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'GuLiStrike_ThreeModels.blend'),compress=True)
    (OUT/'Validation_Report.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
    print('ALL_ASSETS_COMPLETE '+json.dumps({k:v.get('dimensions_m') for k,v in REPORT['assets'].items()}),flush=True)

if __name__=='__main__':
    main()
