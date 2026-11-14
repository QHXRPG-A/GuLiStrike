"""Blender-only authoring from the exported formal meshes. No renders or UE writes."""
import bpy, json, math, hashlib
import numpy as np
from pathlib import Path
from mathutils import Vector, Quaternion, Matrix

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/CommanderRecoil_20261114'
SOURCES=json.loads((OUT/'Sources/formal-sources.json').read_text())
BOUNDS=json.loads((OUT/'Sources/ue-part-bounds.json').read_text())
F=33
REFLECT=Matrix.Diagonal(Vector((1,-1,1)))

def envelope(t):
    if t<0 or t>=1:return 0.
    if t<.08:
        x=t/.08;return x*x*(3-2*x)
    if t<.2:return 1.
    if t<.86:
        x=(t-.2)/.66;return 1-x*x*(3-2*x)
    return 0.

def save_texture(path, pixels):
    a=np.asarray(pixels,np.float32);im=bpy.data.images.new(path.stem,width=a.shape[1],height=a.shape[0],alpha=True,float_buffer=True)
    # Blender stores pixels bottom-up; UE texture source rows are top-down.
    # Put frame 0 in the file's top row, matching native Frame()/Sample().
    im.colorspace_settings.name='Non-Color';im.pixels.foreach_set(a[::-1].copy().ravel())
    scene=bpy.context.scene;scene.render.image_settings.file_format='OPEN_EXR';scene.render.image_settings.color_depth='32';scene.render.image_settings.exr_codec='ZIP'
    im.save_render(str(path),scene=scene);bpy.data.images.remove(im)
    read=bpy.data.images.load(str(path),check_existing=False);read.colorspace_settings.name='Non-Color'
    values=np.empty(a.size,np.float32);read.pixels.foreach_get(values);error=float(np.max(np.abs(values.reshape(a.shape)[::-1]-a)));bpy.data.images.remove(read)
    assert error<.0001,(path,error)
    return {'width':a.shape[1],'height':a.shape[0],'max_readback_error':error,'file_rows':'UE top-down frame order'}

def make_material(name, contour=False):
    mat=bpy.data.materials.new(name);mat.use_nodes=True;n=mat.node_tree.nodes;n.clear();l=mat.node_tree.links
    out=n.new('ShaderNodeOutputMaterial');bs=n.new('ShaderNodeBsdfPrincipled');bs.inputs['Roughness'].default_value=.78
    if contour:bs.inputs['Base Color'].default_value=(.005,.007,.012,1)
    else:
        c=n.new('ShaderNodeVertexColor');c.layer_name='FormalColor';l.new(c.outputs['Color'],bs.inputs['Base Color'])
    l.new(bs.outputs['BSDF'],out.inputs['Surface']);return mat

reports={}
for name in ('WarMachine','Pioneer'):
    folder=OUT/name;folder.mkdir(exist_ok=True);(folder/'Textures').mkdir(exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=str(OUT/'Sources'/f'{name}.fbx'),use_anim=False)
    source=list(o for o in bpy.data.objects if o.type=='MESH');spec=SOURCES[name];heavy=name=='WarMachine'
    pivot=spec['sockets']['Rigid_UpperYaw']['position'] if heavy else spec['vat']['upper_pivot']
    if heavy:
        gun_pivots=[spec['sockets'][f'Rigid_GunPitch_0{i+1}']['position'][:] for i in range(2)]
        muzzles=[spec['sockets'][f'FX_Muzzle_Basic_0{i+1}']['position'][:] for i in range(2)]
        # Formal FBX geometry was reflected on import but the UV pivot payload and
        # sockets were not. Calibrate both to the actual gun part before baking.
        for p in gun_pivots+muzzles:p[1]*=-1
    else:
        gun_pivots=[m['pivot'] for m in spec['vat']['muzzles']];muzzles=[m['position'] for m in spec['vat']['muzzles']]
    gun_distance=[(Vector(m)-Vector(p)).length*.25 for m,p in zip(muzzles,gun_pivots)]
    body=BOUNDS[name]['1'];travel=(body['max'][0]-body['min'][0])*.06
    cycle=.5 if heavy else 1.;factor=1 if heavy else .5
    samples=[]
    for side in range(2):
        for frame in range(F):
            w=envelope(frame/(F-1));pitch=math.radians(8 if heavy else 10)*w*factor
            yaw=math.radians((6 if side==0 else -6) if heavy else 0)*w
            q=Quaternion((0,0,1),yaw)@Quaternion((0,1,0),-pitch)
            samples.append({'upper_translation':[-travel*w*factor,0,0],
                'upper_rotation':[q.x,q.y,q.z,q.w],
                'left_gun_translation':[-gun_distance[0]*w if side==0 else 0,0,0],
                'right_gun_translation':[-gun_distance[1]*w if side==1 else 0,0,0]})
    for i in (0,F-1,F,F*2-1):assert samples[i]['upper_translation']==[0,0,0] and samples[i]['upper_rotation']==[0,0,0,1]
    position=[[[*s[k],1] for k in ('upper_translation','left_gun_translation','right_gun_translation')] for s in samples]
    rotation=[[[*s['upper_rotation']]] for s in samples]
    texture_info={'position':save_texture(folder/'Textures/T_RecoilPosition.exr',position),'rotation':save_texture(folder/'Textures/T_RecoilRotation.exr',rotation)}
    parts=[];lod_report=[];bodymat=make_material('Approved formal vertex colors');linemat=make_material('Existing contour',True)
    for obj in sorted(source,key=lambda o:o.name):
        lod=int(obj.name.split('LOD')[-1][0]);mesh=obj.data;mesh.calc_loop_triangles()
        col=bpy.data.collections.new(f'LOD{lod}');bpy.context.scene.collection.children.link(col)
        if lod:col.hide_viewport=True;col.hide_render=True
        groups={};mixed=0
        for p in mesh.polygons:
            ids=[round(mesh.uv_layers[2].data[i].uv.y) if not heavy else round(1-mesh.uv_layers[2].data[i].uv.y) for i in p.loop_indices]
            assert len(set(ids))==1,(name,lod,'mixed rigid triangle',ids)
            groups.setdefault((ids[0],p.material_index),[]).append(p)
        lod_report.append({'lod':lod,'vertices':len(mesh.vertices),'triangles':len(mesh.loop_triangles),'parts':sorted(set(k[0] for k in groups)),'mixed_triangles':mixed})
        for (part,slot),polys in groups.items():
            verts=sorted({v for p in polys for v in p.vertices});lookup={v:i for i,v in enumerate(verts)}
            data=bpy.data.meshes.new(f'{name}_LOD{lod}_Part{part:02}_{slot}')
            data.from_pydata([mesh.vertices[i].co[:] for i in verts],[],[[lookup[i] for i in p.vertices] for p in polys]);data.update()
            is_contour='Contour' in mesh.materials[slot].name
            data.materials.append(linemat if is_contour else bodymat)
            colors=data.color_attributes.new(name='FormalColor',type='FLOAT_COLOR',domain='CORNER');src=mesh.color_attributes.active_color
            oldloops=[i for p in polys for i in p.loop_indices]
            for j,k in enumerate(oldloops):colors.data[j].color=src.data[k].color if src else (.3,.3,.3,1)
            o=bpy.data.objects.new(data.name,data);col.objects.link(o);o.matrix_world=obj.matrix_world.copy();o.rotation_mode='QUATERNION'
            parts.append((o,part,Matrix(obj.matrix_world),np.array([mesh.vertices[i].co[:] for i in verts],np.float64)))
        bpy.data.objects.remove(obj,do_unlink=True)
    # Review one whole accepted group and its extra rest, with exact neutral endpoints.
    scene=bpy.context.scene;scene.name='重防号_4发后额外2秒' if heavy else '先驱者_8轮齐射后额外2秒'
    scene.render.fps=60;group_seconds=4 if heavy else 10;scene.frame_start=1;scene.frame_end=int(group_seconds*60)
    scene['动作说明']='枪管25%回缩 / 上身6%后移；按真实射击组演示；空格播放。仅动作审核，游戏原材质保留。'
    scene['采样来源']='当前正式UE网格导出；没有改动模型外形或配色'
    def pose(side,t):
        shot=next((s for s in reversed([j*(.5 if heavy else 1) for j in range(4 if heavy else 8)]) if s<=t and (not heavy or int(round(s*2))%2==side)),None)
        w=envelope((t-shot)/cycle) if shot is not None else 0
        p=math.radians(8 if heavy else 10)*w*factor;y=math.radians((6 if side==0 else -6) if heavy else 0)*w
        return np.array([-travel*w*factor,0,0]),Quaternion((0,0,1),y)@Quaternion((0,1,0),-p),w
    low=np.full(3,1.e20);high=-low;rigid_error=0.
    for f in range(1,scene.frame_end+2):
        t=(f-1)/60;a,qa,wl=pose(0,t);b,qb,wr=pose(1,t);q=qb@qa;rot=np.array(q.to_matrix());tr=a+b;p=np.array(pivot)
        for o,part,base,vertices in parts:
            upper=(part in (1,2,3,4,5,10,11)) if heavy else part>=1
            side=0 if part in ((2,4) if heavy else (2,)) else 1 if part in ((3,5) if heavy else (3,)) else -1
            weight=.25 if heavy and part in (2,3) else 1
            shift=np.array([-gun_distance[side]*(wl if side==0 else wr)*weight,0,0]) if side>=0 else np.zeros(3)
            R=rot if upper else np.eye(3);T=p-R@p+tr+R@shift if upper else shift
            # FBX export is reflected in Y relative to UE; preserve its untouched geometry.
            br=REFLECT@Matrix(R)@REFLECT;bt=REFLECT@Vector(T)
            o.location=bt*.01;o.rotation_quaternion=br.to_quaternion();o.scale=(.01,.01,.01)
            if upper:
                o.keyframe_insert('location',frame=f);o.keyframe_insert('rotation_quaternion',frame=f)
            v=vertices*np.array([1,-1,1]);v=v@R.T+T
            low=np.minimum(low,v.min(axis=0));high=np.maximum(high,v.max(axis=0));rigid_error=max(rigid_error,float(np.max(np.abs(R.T@R-np.eye(3)))))
    # CPU trajectory is evaluated from the same two samples used by GPU interpolation.
    muzzle_samples=[]
    for s in samples:
        q=Quaternion((s['upper_rotation'][3],*s['upper_rotation'][:3]));p=Vector(pivot)
        muzzle_samples.append([list(p+q@(Vector(m)+Vector(s['left_gun_translation' if side==0 else 'right_gun_translation'])-p)+Vector(s['upper_translation'])) for side,m in enumerate(muzzles)])
    for screen in bpy.data.screens:
        for area in screen.areas:
            if area.type=='VIEW_3D':
                area.spaces.active.shading.type='MATERIAL';area.spaces.active.region_3d.view_distance=75 if heavy else 8
                area.spaces.active.region_3d.view_location=Vector((0,0,18 if heavy else 2))
                area.spaces.active.region_3d.view_rotation=Quaternion((.820,.424,.176,.340)).normalized()
    scene.frame_set(1);bpy.context.preferences.filepaths.save_version=0
    blend=folder/f'{name}_Recoil_v1.blend';bpy.ops.wm.save_as_mainfile(filepath=str(blend),check_existing=False,compress=True)
    report={'unit':name,'source_mesh':spec['mesh'],'source_sha256':hashlib.sha256((OUT/'Sources'/f'{name}.fbx').read_bytes()).hexdigest(),
        'frames_per_side':F,'cycle_seconds':cycle,'samples':samples,'upper_pivot':pivot,'gun_pivots':gun_pivots,'reference_muzzles':muzzles,
        'muzzle_samples':muzzle_samples,'upper_parts':[1,2,3,4,5,10,11] if heavy else [1,2,3],
        'left_parts':[2,4] if heavy else [2],'right_parts':[3,5] if heavy else [3],
        'render_bounds':{'min':(low-10).tolist(),'max':(high+10).tolist()},'lods':lod_report,'texture_readback':texture_info,
        'peak_gun_translation_cm':gun_distance,'peak_upper_translation_cm':travel,'maximum_rigid_orthogonality_error':rigid_error,
        'gun_pivot_y_corrected':heavy,'visual_review':'Pending user B; no assistant image inspection','blend':str(blend)}
    assert rigid_error<1.e-5
    (folder/'recoil.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8');reports[name]={k:v for k,v in report.items() if k not in ('samples','muzzle_samples')}
    print('RECOIL_BAKED',name,lod_report,flush=True)
(OUT/'recoil-authoring-report.json').write_text(json.dumps(reports,ensure_ascii=False,indent=2),encoding='utf8')
