"""Package authored rigid parts, atlas, skeleton and FBX without reading Tripo."""
import bpy,bmesh,sys,json,math
from pathlib import Path
from mathutils import Vector,Matrix
import numpy as np
sys.path.insert(0,str(Path(__file__).parent))
import build_tactical_from_drawings as author
ROOT=author.OUT

def apply_modifiers(o):
    author.active(o)
    for m in list(o.modifiers):bpy.ops.object.modifier_apply(modifier=m.name)

def remove_hidden_faces(parts,scale):
    """Remove faces wholly contained by another convex part of the same rigid group.

    Convex half-space membership guarantees that the entire polygon is inside,
    rather than relying on a camera-dependent visibility or centroid-only test.
    """
    solids=[]
    for o in parts:
        coords=np.array([v.co[:] for v in o.data.vertices],dtype=np.float64)
        normals=np.array([p.normal[:] for p in o.data.polygons],dtype=np.float64)
        centers=np.array([p.center[:] for p in o.data.polygons],dtype=np.float64)
        plane=(normals*centers).sum(1)
        if np.max(coords@normals.T-plane)>scale*1e-5:continue
        solids.append((o,coords.min(0),coords.max(0),normals,plane))
    removed=0
    for o in parts:
        kill=[]
        for p in o.data.polygons:
            v=np.array([o.data.vertices[i].co[:] for i in p.vertices],dtype=np.float64)
            lo,hi=v.min(0),v.max(0)
            for other,b0,b1,n,d in solids:
                if other==o or o.get('motion')!=other.get('motion'):continue
                if np.any(lo<b0-scale*1e-6) or np.any(hi>b1+scale*1e-6):continue
                signed=v@n.T-d
                if np.max(signed)<=scale*1e-6 and np.max(v.mean(0)@n.T-d)<-scale*1e-5:
                    kill.append(p.index);removed+=len(p.vertices)-2;break
        if kill:
            bm=bmesh.new();bm.from_mesh(o.data);bm.faces.ensure_lookup_table()
            bmesh.ops.delete(bm,geom=[bm.faces[i] for i in kill],context='FACES');bm.to_mesh(o.data);bm.free();o.data.update()
    return removed

def atlas(o,report,unit):
    me=o.data
    while me.uv_layers:me.uv_layers.remove(me.uv_layers[0])
    bm=bmesh.new();bm.from_mesh(me)
    color_edges=[e for e in bm.edges if len(e.link_faces)==2 and e.link_faces[0].material_index!=e.link_faces[1].material_index]
    bmesh.ops.split_edges(bm,edges=color_edges);bm.to_mesh(me);bm.free();me.update()
    me.uv_layers.new(name='Atlas');me.uv_layers.active_index=0
    author.active(o);bpy.ops.object.mode_set(mode='EDIT');bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(58),island_margin=.008,area_weight=.0,scale_to_bounds=False)
    bpy.ops.object.mode_set(mode='OBJECT');uv=me.uv_layers.active;me.calc_loop_triangles()
    size=2048;pal=np.array([[int(c[i:i+2],16)/255 for i in (0,2,4)] for c in author.PALETTE],dtype=np.float32)
    data=np.ones((size,size,4),dtype=np.float32);data[...,:3]=pal[2];filled=np.zeros((size,size),bool)
    group_names={g.index:g.name for g in o.vertex_groups};wheel=report['wheel_pivots_m']
    scale=report['source_scale']
    for t in me.loop_triangles:
        xy=np.array([uv.data[l].uv[:] for l in t.loops])*size;a,b=xy[1]-xy[0],xy[2]-xy[0];det=a[0]*b[1]-a[1]*b[0]
        if abs(det)<1e-8:continue
        x0,y0=np.maximum(np.floor(xy.min(0)).astype(int),0);x1,y1=np.minimum(np.ceil(xy.max(0)).astype(int),size)
        if x0>=x1 or y0>=y1:continue
        dx=np.arange(x0,x1)[None,:]+.5-xy[0,0];dy=np.arange(y0,y1)[:,None]+.5-xy[0,1]
        s=(dx*b[1]-dy*b[0])/det;u=(a[0]*dy-a[1]*dx)/det;inside=(s>=0)&(u>=0)&(s+u<=1)
        if not inside.any():continue
        ci=me.polygons[t.polygon_index].material_index;values=np.tile(pal[ci],(inside.sum(),1))
        v0=me.vertices[t.vertices[0]];tag=group_names[v0.groups[0].group]
        if tag.startswith('Wheel') and ci==2:
            vv=np.array([me.vertices[i].co[:] for i in t.vertices]);p=vv[0]+s[inside,None]*(vv[1]-vv[0])+u[inside,None]*(vv[2]-vv[0]);q=(p-np.array(wheel[tag]))/scale
            radius=.96 if '_F' in tag else 1.04;r=np.hypot(q[:,0],q[:,2]);ang=np.arctan2(q[:,2],q[:,0])
            # Regular tread joints are baked into color, never displaced into dents.
            period=2*math.pi/16;phase=np.mod(ang+np.abs(q[:,1])*.13,period)
            coverage=np.clip((.020-np.minimum(phase,period-phase))/.010,0,1)*(r>radius*.90)
            values*=1-.24*coverage[:,None]
        data[y0:y1,x0:x1,:3][inside]=values;filled[y0:y1,x0:x1][inside]=True
    for _ in range(7):
        n=np.zeros((size,size),np.float32);v=np.zeros((size,size,3),np.float32)
        for ax,shift in ((0,1),(0,-1),(1,1),(1,-1)):
            f=np.roll(filled,shift,ax);n+=f;v+=np.roll(data[...,:3],shift,ax)*f[...,None]
        grow=(~filled)&(n>0);data[grow,:3]=v[grow]/n[grow,None];filled|=grow
    path=ROOT/('T_'+unit+'_BaseColor.png');im=bpy.data.images.new('T_'+unit+'_BaseColor_Raster',width=size,height=size,alpha=True);im.colorspace_settings.name='Non-Color';im.pixels.foreach_set(data.ravel());im.filepath_raw=str(path);im.file_format='PNG';im.save()
    im=bpy.data.images.load(str(path),check_existing=False);im.colorspace_settings.name='sRGB';im.pack()
    toon=author.material(0).copy();toon.name='M_'+unit+'_ToonPreview';nodes=toon.node_tree.nodes;links=toon.node_tree.links
    mul=next(n for n in nodes if n.type=='MIX_RGB' and n.blend_type=='MULTIPLY' and n.inputs[0].default_value==1)
    tex=nodes.new('ShaderNodeTexImage');tex.image=im;links.new(tex.outputs['Color'],mul.inputs[1])
    for p in me.polygons:p.material_index=0
    me.materials.clear();me.materials.append(toon)
    pbr=bpy.data.materials.new('M_'+unit+'_Export');pbr.use_nodes=True;bsdf=next(n for n in pbr.node_tree.nodes if n.type=='BSDF_PRINCIPLED');bsdf.inputs['Roughness'].default_value=.78;bsdf.inputs['Metallic'].default_value=0
    tex=pbr.node_tree.nodes.new('ShaderNodeTexImage');tex.image=im;pbr.node_tree.links.new(tex.outputs['Color'],bsdf.inputs['Base Color'])
    return toon,pbr

def metadata(o,report):
    me=o.data;u1=me.uv_layers.new(name='RigidPivotXY_UE');u2=me.uv_layers.new(name='RigidPivotZ_Radius_UE');color=me.color_attributes.new(name='RigidPart',type='FLOAT_COLOR',domain='CORNER')
    names={g.index:g.name for g in o.vertex_groups}
    for p in me.polygons:
        for li in p.loop_indices:
            v=me.vertices[me.loops[li].vertex_index];tag=names[v.groups[0].group];pivot=(0,0,0);part=0;ratio=1
            if tag.startswith('Wheel'):
                pivot=report['wheel_pivots_m'][tag];part=.5;ratio=1 if '_F' in tag else .96/1.04
            elif tag=='Gun_Pitch':pivot=report['sockets_m']['Rig_GunPitch'];part=1
            # FBX reader flips the V coordinate. The target attributes are centimeters.
            u1.data[li].uv=(pivot[0]*100,1-pivot[1]*100);u2.data[li].uv=(pivot[2]*100,1-ratio);color.data[li].color=(part,0,0,1)
    me.uv_layers.active_index=0

def rig(o,report):
    a=bpy.data.armatures.new('Rig_'+report['unit']);arm=bpy.data.objects.new('Rig_'+report['unit'],a);bpy.context.collection.objects.link(arm)
    author.active(arm);bpy.ops.object.mode_set(mode='EDIT')
    root=a.edit_bones.new('root');root.head=(0,0,0);root.tail=(0,0,.5)
    body=a.edit_bones.new('Body');body.head=(0,0,0);body.tail=(0,0,1);body.parent=root
    for name,point in report['wheel_pivots_m'].items():
        bone=a.edit_bones.new(name);bone.head=point;bone.tail=Vector(point)+Vector((0,.5,0));bone.parent=body
    if 'Rig_GunPitch' in report['sockets_m']:
        bone=a.edit_bones.new('Gun_Pitch');bone.head=report['sockets_m']['Rig_GunPitch'];bone.tail=Vector(bone.head)+Vector((0,.6,0));bone.parent=body
    bpy.ops.object.mode_set(mode='OBJECT');mod=o.modifiers.new('Rigid mechanical bones','ARMATURE');mod.object=arm;o.parent=arm
    if 'Gun_Pitch' in arm.pose.bones:
        pb=arm.pose.bones['Gun_Pitch'];pb.rotation_mode='XYZ';con=pb.constraints.new('LIMIT_ROTATION');con.owner_space='LOCAL';con.use_limit_y=True;con.min_y=math.radians(-60);con.max_y=math.radians(15)
    return arm

def export(o,arm,report,unit,pbr,toon):
    o.data.materials[0]=pbr
    author.active(o)
    bpy.ops.export_scene.fbx(filepath=str(ROOT/('SM_'+unit+'_Crowd.fbx')),use_selection=True,object_types={'MESH'},global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE',use_mesh_modifiers=True,add_leaf_bones=False,bake_anim=False,path_mode='COPY',embed_textures=False)
    arm.select_set(True)
    bpy.ops.export_scene.fbx(filepath=str(ROOT/('SK_'+unit+'.fbx')),use_selection=True,object_types={'MESH','ARMATURE'},global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',axis_forward='-Y',axis_up='Z',mesh_smooth_type='FACE',add_leaf_bones=False,use_armature_deform_only=True,bake_anim=False,path_mode='COPY')
    bpy.ops.export_scene.gltf(filepath=str(ROOT/(unit+'_Handbuilt.glb')),use_selection=True,use_active_scene=True,export_format='GLB',export_animations=False,export_skins=True,export_yup=True)
    o.data.materials[0]=toon

def inspect_rigid_poses(o,arm,report):
    result=[];names={g.index:g.name for g in o.vertex_groups}
    for angle in (-15,0,35,60):
        arm.pose.bones['Gun_Pitch'].rotation_euler.y=math.radians(-angle)
        for name in report['wheel_pivots_m']:arm.pose.bones[name].rotation_mode='XYZ';arm.pose.bones[name].rotation_euler.y=math.radians(55)
        bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get();ev=o.evaluated_get(dg);me=ev.to_mesh();error=0;body_error=0
        for v,after in zip(o.data.vertices,me.vertices):
            name=names[v.groups[0].group];bone=arm.pose.bones[name]
            expected=bone.matrix@bone.bone.matrix_local.inverted()@v.co
            error=max(error,(expected-after.co).length)
            if name=='Body':body_error=max(body_error,(v.co-after.co).length)
        ev.to_mesh_clear();result.append({'gun_pitch_degrees':angle,'rigid_skin_error_m':error,'body_movement_error_m':body_error})
        if angle in (-15,60):author.render(report['unit']+'_Pitch'+str(angle),'three_quarter')
    for bone in arm.pose.bones:bone.rotation_euler=(0,0,0)
    bpy.context.view_layer.update();return result

def main(unit):
    bpy.ops.wm.open_mainfile(filepath=str(ROOT/(unit+'_Handbuilt_Editable.blend')))
    report=json.loads((ROOT/(unit+'_handbuilt_report.json')).read_text(encoding='utf8'))
    parts=[o for o in bpy.context.scene.objects if o.type=='MESH'];scale=report['source_scale']
    for o in parts:apply_modifiers(o)
    removed=remove_hidden_faces(parts,scale)
    bpy.ops.object.select_all(action='DESELECT')
    for o in parts:
        g=o.vertex_groups.new(name=o.get('motion','Body'));g.add(list(range(len(o.data.vertices))),1,'REPLACE');o.select_set(True)
    bpy.context.view_layer.objects.active=parts[0];bpy.ops.object.join();o=bpy.context.object;o.name='SM_'+unit+'_Crowd'
    # Joining repeats palette slots. Collapse them back to the author's indices.
    slot_ids=[int(m.name.split('_')[-1].split('.')[0]) for m in o.data.materials]
    for p in o.data.polygons:p.material_index=slot_ids[p.material_index]
    # Strip empty vertices left by fully enclosed faces before binding/export.
    bm=bmesh.new();bm.from_mesh(o.data);bmesh.ops.delete(bm,geom=[v for v in bm.verts if not v.link_faces],context='VERTS');bm.to_mesh(o.data);bm.free();o.data.update()
    report['hidden_triangles_removed']=removed;report['triangles']=sum(len(p.vertices)-2 for p in o.data.polygons)
    report['vertices']=len(o.data.vertices)
    toon,pbr=atlas(o,report,unit);metadata(o,report);arm=rig(o,report)
    report['vertices']=len(o.data.vertices)
    report['bone_names']=[b.name for b in arm.data.bones];report['rigid_weights_valid']=all(len(v.groups)==1 and abs(v.groups[0].weight-1)<1e-6 for v in o.data.vertices)
    report['material_slots']=len(o.data.materials);report['texture_size']=[2048,2048];report['uv_layers']=[u.name for u in o.data.uv_layers]
    assert report['rigid_weights_valid']
    author.PARTS=[o]
    for view in ('three_quarter','front','side','top'):author.render(unit+'_Packaged',view)
    if 'Gun_Pitch' in arm.pose.bones:
        pb=arm.pose.bones['Gun_Pitch'];pb.rotation_euler.y=math.radians(-35)
        for name in report['wheel_pivots_m']:arm.pose.bones[name].rotation_mode='XYZ';arm.pose.bones[name].rotation_euler.y=math.radians(55)
        bpy.context.view_layer.update();author.render(unit+'_Pose35','three_quarter')
        for bone in arm.pose.bones:bone.rotation_euler=(0,0,0)
        bpy.context.view_layer.update()
        report['pose_inspection']=inspect_rigid_poses(o,arm,report)
    export(o,arm,report,unit,pbr,toon)
    author.render(unit+'_Packaged','three_quarter')
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/(unit+'_Handbuilt_Rigged.blend')))
    (ROOT/(unit+'_delivery.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print('HANDBUILT_PACKAGE_READY',json.dumps(report,ensure_ascii=False),flush=True)

if __name__=='__main__':
    unit=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'all'
    for u in ('Sweeper','WarMachine') if unit=='all' else (unit,):main(u)
