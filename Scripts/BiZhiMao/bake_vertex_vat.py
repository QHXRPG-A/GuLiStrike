"""Bake actual deformed vertices. The runtime derivative carries no bone data."""
import bpy,json,math,hashlib,zlib,struct,argparse,sys,shutil,copy
import numpy as np
from pathlib import Path
from mathutils import Matrix,Vector

SOURCE_ART=Path('D:/UE5.7/test1/ArtSource/Mechs/BiZhiMao_20261005')
CURRENT_ART=Path('D:/UE5.7/test1/ArtSource/CommanderLOD_20261005/BiZhiMao')
parser=argparse.ArgumentParser();parser.add_argument('--art-dir',type=Path,default=CURRENT_ART)
parser.add_argument('--source-blend',type=Path,default=CURRENT_ART/'BiZhiMao_3Tier.blend')
parser.add_argument('--version',default='BiZhiMao_CommanderLOD_3Tier_v1')
parser.add_argument('--reuse-lod0',action='store_true',default=True)
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
ART=args.art_dir
for folder in ['Textures','FBX','Reports']:(ART/folder).mkdir(parents=True,exist_ok=True)
old=json.loads((SOURCE_ART/'vat_metadata.json').read_text())
bpy.ops.wm.open_mainfile(filepath=str(args.source_blend))
scene=bpy.context.scene;rig=bpy.data.objects['Armature']
bpy.data.objects['BiZhiMao_PresentationScale_2x'].scale=(1,1,1)
names=[b.name for b in rig.data.bones];inverse={b.name:b.matrix_local.inverted() for b in rig.data.bones}
C=Matrix(((0,-100,0,0),(-100,0,0,0),(0,0,100,0),(0,0,0,1)));CI=C.inverted()
WIDTH=4096
clips=['Idle','Forward','Backward','Left','Right']
report=dict(version=args.version,lod_count=3,screen_sizes=[1,.4,.06],runtime_bones=0,runtime_skeletal_components=0,
    source_sha256=old['source_sha256'],presentation_scale=2,upper_pivot_cm=old['upper_pivot_cm'],pitch_pivot_cm=old['pitch_pivot_cm'],
    pitch_axis=old['pitch_axis'],pitch_limits=old['pitch_limits'],upper_turn_rate=30,pitch_turn_rate=15,
    gameplay_bounds_cm=old['gameplay_bounds_cm'],runtime_render_bounds_cm=old['runtime_render_bounds_cm'],
    origin_footprint_radius_cm=old['origin_footprint_radius_cm'],gun_linkages=old['gun_linkages'],lods=[],clips=[])
render_radius=math.ceil((report['origin_footprint_radius_cm']+80)/100)*100
report['runtime_render_bounds_cm']=dict(min=[-render_radius,-render_radius,-100],max=[render_radius,render_radius,1600])
for i,name in enumerate(clips):
    source=next(c for c in old['clips'] if c['name']==name)
    report['clips'].append(dict(name=name,first_frame=i*32,frame_count=32,duration_seconds=source['duration_seconds'],
        stride_cm=source['stride_cm'],loop=True))
report['clips'].append(dict(name='Death',first_frame=160,frame_count=2,duration_seconds=1/30,stride_cm=0,loop=False))

def values(collection,prop,n):
    a=np.empty(len(collection)*n,dtype=np.float32);collection.foreach_get(prop,a);return a.reshape((-1,n))

def png(path,rgba):
    h,w,_=rgba.shape
    def chunk(tag,data):return struct.pack('>I',len(data))+tag+data+struct.pack('>I',zlib.crc32(tag+data)&0xffffffff)
    data=b''.join(b'\0'+row.tobytes() for row in rgba)
    # Top-down file rows equal texture frame-major rows. No color-space transform.
    path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+
        chunk(b'IDAT',zlib.compress(data,6))+chunk(b'IEND',b''))

def exr(path,rgba):
    h,w,_=rgba.shape;im=bpy.data.images.new(path.stem,width=w,height=h,alpha=True,float_buffer=True)
    im.colorspace_settings.name='Non-Color';im.alpha_mode='STRAIGHT';im.pixels.foreach_set(rgba.ravel())
    scene.render.image_settings.file_format='OPEN_EXR';scene.render.image_settings.color_depth='32'
    im.save_render(str(path),scene=scene);bpy.data.images.remove(im)

report['authoring_blend']=str(args.source_blend)
report['authoring_blend_sha256']=hashlib.sha256(args.source_blend.read_bytes()).hexdigest()
export=bpy.data.scenes.new('BiZhiMao_VertexExport');export.unit_settings.system='METRIC';export.unit_settings.scale_length=.01
for lod,samples in enumerate([32,24,8]):
    if lod==0 and args.reuse_lod0:
        item=copy.deepcopy(json.loads((SOURCE_ART/'vertex_metadata.json').read_text())['lods'][0])
        for key,folder in [('position','Textures'),('rotation','Textures'),('fbx','FBX')]:
            original=Path(item[key]);target=ART/folder/original.name
            assert original.resolve()!=target.resolve()
            shutil.copy2(original,target);item[key]=str(target)
            assert hashlib.sha256(target.read_bytes()).hexdigest()==item['sha256'][target.name]
        item['reused_unchanged_from_v1']=True;report['lods'].append(item)
        print('LOD0_BYTES_REUSED_UNCHANGED',flush=True);continue
    bpy.context.window.scene=scene;rig.data.pose_position='REST';bpy.context.view_layer.update()
    originals=[bpy.data.objects[f'ControlRigMech_LOD{lod}_Body']]
    if lod<2:originals.append(bpy.data.objects[f'ControlRigMech_B_v4_LOD{lod}_Outline'])
    for original in originals:original.hide_set(False);original.hide_viewport=False
    copies=[];parts=[];offset=0
    bpy.context.window.scene=export
    for section,original in enumerate(originals):
        ob=original.copy();ob.data=original.data.copy();ob.parent=None;ob.matrix_world=Matrix.Identity(4)
        export.collection.objects.link(ob);ob.hide_set(False);ob.hide_viewport=False;ob.hide_render=False
        bpy.ops.object.select_all(action='DESELECT');ob.select_set(True);bpy.context.view_layer.objects.active=ob
        for modifier in list(ob.modifiers):
            if modifier.type=='ARMATURE':ob.modifiers.remove(modifier)
            else:bpy.ops.object.modifier_apply(modifier=modifier.name)
        me=ob.data;rest=values(me.vertices,'co',3)
        native=np.column_stack([-rest[:,1]*100,-rest[:,0]*100,rest[:,2]*100])
        ids=np.zeros((len(me.vertices),4),dtype=np.int32);weights=np.zeros((len(me.vertices),4),dtype=np.float32)
        aim=np.zeros((len(me.vertices),2),dtype=np.float32);flags=np.zeros(len(me.vertices),dtype=np.float32)
        for v in me.vertices:
            ws=sorted([(names.index(ob.vertex_groups[g.group].name),g.weight) for g in v.groups
                if ob.vertex_groups[g.group].name in names and g.weight>1e-7],key=lambda x:-x[1])
            assert 1<=len(ws)<=4
            total=sum(w for _,w in ws)
            for j,(bone,w) in enumerate(ws):
                ids[v.index,j]=bone;weights[v.index,j]=w/total
                aim[v.index,0]+=w/total*old['bones'][bone]['upper_aim']
                aim[v.index,1]+=w/total*bool(old['bones'][bone]['gun_pitch'])
            main=names[ws[0][0]]
            for pair,side in enumerate(['l','r']):
                if main==f'piston_01_{side}':flags[v.index]=3+pair*2
                if main==f'piston_02_{side}':flags[v.index]=4+pair*2
        parts.append(dict(original=original,copy=ob,count=len(me.vertices),offset=offset,rest=native,ids=ids,weights=weights,aim=aim,flags=flags,section=section))
        offset+=len(me.vertices);copies.append(ob)
    rows=math.ceil(offset/WIDTH);frames=samples*5+2
    positions=np.zeros((frames*rows,WIDTH,4),dtype=np.float32);positions[:,:,3]=1
    rotations=np.empty((frames*rows,WIDTH,4),dtype=np.uint8);rotations[:]=[128,128,128,255]
    half_error=0.;rotation_error=0.;native_samples=[]
    bpy.context.window.scene=scene;rig.data.pose_position='POSE'
    jobs=[(name,f) for name in clips for f in np.linspace(1,261 if name=='Idle' else 91,samples)]
    jobs += [('Idle',1),('Idle',1)]
    for frame,(name,source_frame) in enumerate(jobs):
        rig.animation_data.action=bpy.data.actions['BiZhiMao_Idle_Deployed' if name=='Idle' else 'BiZhiMao_'+name]
        whole=math.floor(source_frame);scene.frame_set(whole,subframe=source_frame-whole);bpy.context.view_layer.update()
        quats=[]
        for n in names:
            q=(C@rig.pose.bones[n].matrix@inverse[n]@CI).to_quaternion();quats.append([q.x,q.y,q.z,q.w])
        quats=np.asarray(quats,dtype=np.float32)
        for part in parts:
            evaluated=part['original'].evaluated_get(bpy.context.evaluated_depsgraph_get());me=evaluated.to_mesh()
            assert len(me.vertices)==part['count'],part['original'].name
            co=values(me.vertices,'co',3);evaluated.to_mesh_clear()
            posed=np.column_stack([-co[:,1]*100,-co[:,0]*100,co[:,2]*100]);delta=posed-part['rest']
            qs=quats[part['ids']];reference=qs[:,0:1,:]
            qs=np.where((qs*reference).sum(2,keepdims=True)<0,-qs,qs)
            q=(qs*part['weights'][:,:,None]).sum(1);q/=np.maximum(np.linalg.norm(q,axis=1)[:,None],1e-8)
            packed=np.round(np.clip(q*.5+.5,0,1)*255).astype(np.uint8)
            decoded=packed.astype(np.float32)/255*2-1;decoded/=np.linalg.norm(decoded,axis=1)[:,None]
            half_error=max(half_error,float(np.abs(delta.astype(np.float16).astype(np.float32)-delta).max()*2))
            rotation_error=max(rotation_error,float(np.degrees(2*np.arccos(np.minimum(np.abs((q*decoded).sum(1)),1))).max()))
            linear=np.arange(part['offset'],part['offset']+part['count']);yy=frame*rows+linear//WIDTH;xx=linear%WIDTH
            positions[yy,xx,:3]=delta;rotations[yy,xx]=packed
            if frame in (0,samples//2,samples,samples*3,frames-1):
                for vi in [0,part['count']//2,part['count']-1]:
                    v=part['offset']+vi
                    native_samples.append(dict(vertex=v,frame=frame,pixel=[int(v%WIDTH),int(frame*rows+v//WIDTH)],delta_cm=delta[vi].tolist(),rotation_rgba=packed[vi].tolist()))
    posfile=ART/'Textures'/f'T_BiZhiMao_VertexPosition_LOD{lod}.exr'
    rotfile=ART/'Textures'/f'T_BiZhiMao_VertexRotation_LOD{lod}.png'
    exr(posfile,positions);png(rotfile,rotations)
    # Verify the actual saved EXR, including frame/pixel orientation.
    im=bpy.data.images.load(str(posfile),check_existing=False);im.colorspace_settings.name='Non-Color'
    read=np.asarray(im.pixels[:],dtype=np.float32).reshape(positions.shape)
    assert np.max(np.abs(read-positions))<.001
    bpy.data.images.remove(im)
    del read,positions,rotations
    bpy.context.window.scene=export
    stats=[]
    for part in parts:
        ob=part['copy'];me=ob.data;normals=np.asarray([tuple(n.vector) for n in me.corner_normals])
        # Keep the three art channels; discard the superseded authoring bone payload.
        while len(me.uv_layers)>3:me.uv_layers.remove(me.uv_layers[-1])
        while len(me.uv_layers)<3:
            uv=me.uv_layers.new(name=f'GuLi_ArtUV{len(me.uv_layers)}');uv.data.foreach_set('uv',np.tile((8,8),(len(me.loops),1)).ravel())
        # Blender joins channels by name, so body and contour must share all six names.
        for i,name in enumerate(['ControlRigMech_AtlasUV','GuLi_StructuralDistance_RG','GuLi_StructuralDistance_B']):
            me.uv_layers[i].name=name
        colour=me.color_attributes.get('GuLi_PaletteLinear')
        srgb=values(colour.data,'color_srgb',4) if colour else np.tile((27/255,36/255,34/255,1),(len(me.loops),1))
        for attr in list(me.color_attributes):me.color_attributes.remove(attr)
        colour=me.color_attributes.new('GuLi_PaletteSRGB','BYTE_COLOR','CORNER');colour.data.foreach_set('color_srgb',srgb.ravel())
        me.color_attributes.active_color=colour;me.color_attributes.render_color_index=0
        vertex=np.asarray([l.vertex_index for l in me.loops],dtype=np.int32)
        index=np.column_stack([(np.arange(part['count'])+part['offset']+.5)/offset,part['flags']])
        for name,payload in [('GuLi_VertexAnimationIndex',index),('GuLi_VertexAimWeights',part['aim'])]:
            uv=me.uv_layers.new(name=name);uv.data.foreach_set('uv',payload[vertex].astype(np.float32).ravel())
        uv=me.uv_layers.new(name='GuLi_VertexAnimationLOD')
        uv.data.foreach_set('uv',np.tile((lod,0),(len(me.loops),1)).astype(np.float32).ravel())
        assert len(me.uv_layers)==6
        rotation=Matrix.Rotation(math.pi/2,4,'Z');me.transform(Matrix.Scale(100,4)@rotation)
        me.normals_split_custom_set([rotation.to_3x3()@Vector(n) for n in normals])
        me.materials.clear();me.materials.append(bpy.data.materials.get(f'BiZhiMao_Export_{part["section"]}') or bpy.data.materials.new(f'BiZhiMao_Export_{part["section"]}'))
        for p in me.polygons:p.material_index=0
        me.calc_loop_triangles();stats.append(dict(section=part['section'],triangles=len(me.loop_triangles),vertices=part['count']))
    bpy.ops.object.select_all(action='DESELECT')
    for ob in copies:ob.select_set(True)
    bpy.context.view_layer.objects.active=copies[0]
    if len(copies)>1:bpy.ops.object.join()
    file=ART/'FBX'/f'SM_BiZhiMao_Vertex_LOD{lod}.fbx'
    bpy.ops.export_scene.fbx(filepath=str(file),use_selection=True,object_types={'MESH'},global_scale=1,
        apply_unit_scale=True,apply_scale_options='FBX_SCALE_UNITS',axis_forward='-Y',axis_up='Z',
        use_mesh_modifiers=True,mesh_smooth_type='FACE',use_tspace=True,colors_type='SRGB',bake_anim=False,path_mode='STRIP')
    copies[0].hide_set(True)
    item=dict(lod=lod,vertex_count=offset,texture_width=WIDTH,rows_per_frame=rows,frames_per_clip=samples,texture_frames=frames,
        width=WIDTH,height=rows*frames,position=str(posfile),rotation=str(rotfile),fbx=str(file),sections=stats,
        position_gpu_bytes=WIDTH*rows*frames*8,rotation_gpu_bytes=WIDTH*rows*frames*4,
        maximum_position_half_precision_error_cm_at_scale2=half_error,maximum_rotation_quantization_error_degrees=rotation_error,
        source_exr_readback_passed=True,samples=native_samples,
        sha256={p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in [posfile,rotfile,file]})
    report['lods'].append(item)
    print('VERTEX_LOD_READY',json.dumps({k:v for k,v in item.items() if k not in ['samples','sha256']}),flush=True)
report['gpu_animation_bytes']=sum(l['position_gpu_bytes']+l['rotation_gpu_bytes'] for l in report['lods'])
report['actual_version_approval']='pending'
(ART/'vertex_metadata.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('VERTEX_VAT_READY',report['gpu_animation_bytes'],flush=True)
