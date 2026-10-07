"""Authorized B-v4 delivery in a dedicated normal UE Editor process."""
import unreal, json, math, hashlib, traceback
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004');O=R/'UE_Delivery_v1'
BASE='/Game/GuLiStrike/Mechs/ControlRigMech';SRC='/Game/Assets/ControlRig/Characters/Mech'
OWNER='GuLiStrike.ControlRigMech.B-v4.UE-v1'
LIB=unreal.EditorAssetLibrary;TOOLS=unreal.AssetToolsHelpers.get_asset_tools();EDIT=unreal.MaterialEditingLibrary
REPORT={'success':False,'source_version':'B-v4','assets':[],'budget':'over_budget; not FPS acceptance'}
def dump(name,value):(O/name).write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')
def owned(path):
    a=unreal.load_asset(path) if LIB.does_asset_exist(path) else None
    assert not a or LIB.get_metadata_tag(a,'GuLi.Owner')==OWNER,('unowned target',path)
    return a
def save(a):
    assert a and a.get_path_name().startswith(BASE+'/'),a
    LIB.set_metadata_tag(a,'GuLi.Owner',OWNER);LIB.set_metadata_tag(a,'GuLi.ApprovedVersion','B-v4')
    LIB.set_metadata_tag(a,'GuLi.ApprovedSHA256','bf566b78fb3fd03c516016ed0e32bc7ee8412929c303283985340d5d5b5d853b')
    assert LIB.save_loaded_asset(a,False),a.get_path_name()
    if a.get_path_name() not in REPORT['assets']:REPORT['assets'].append(a.get_path_name())
def create(path,cls,factory):
    a=owned(path)
    if not a:
        folder,name=path.rsplit('/',1);LIB.make_directory(folder);a=TOOLS.create_asset(name,folder,cls,factory)
    assert a,path;save(a);return a
def copy(source,target):
    a=owned(target)
    if not a:
        LIB.make_directory(target.rsplit('/',1)[0]);a=LIB.duplicate_asset(source,target)
    assert a,target;LIB.set_metadata_tag(a,'GuLi.SourceAsset',source);save(a);return a
def imported(file,path,options=None):
    folder,name=path.rsplit('/',1);LIB.make_directory(folder);a=owned(path)
    digest=hashlib.sha256(Path(file).read_bytes()).hexdigest()
    if a and LIB.get_metadata_tag(a,'GuLi.SourceSHA256')==digest:save(a);return a
    task=unreal.AssetImportTask()
    for k,v in dict(filename=str(file),destination_path=folder,destination_name=name,automated=True,async_=False,replace_existing=bool(a),replace_existing_settings=True,save=False).items():task.set_editor_property(k,v)
    if options:task.set_editor_property('options',options);task.set_editor_property('factory',unreal.FbxFactory())
    TOOLS.import_asset_tasks([task]);a=unreal.load_asset(path);assert a,(path,list(task.imported_object_paths))
    LIB.set_metadata_tag(a,'GuLi.SourceSHA256',hashlib.sha256(Path(file).read_bytes()).hexdigest());save(a);return a
def opts(skeleton):
    ui=unreal.FbxImportUI();kind=unreal.FBXImportType.FBXIT_SKELETAL_MESH
    for k,v in dict(import_materials=False,import_textures=False,import_mesh=True,import_as_skeletal=True,import_animations=False,create_physics_asset=False,automated_import_should_detect_type=False,mesh_type_to_import=kind,original_import_type=kind,skeleton=skeleton).items():ui.set_editor_property(k,v)
    d=ui.get_editor_property('skeletal_mesh_import_data')
    for k,v in dict(convert_scene=True,convert_scene_unit=True,force_front_x_axis=False,import_uniform_scale=1.,import_mesh_lo_ds=False,normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS,vertex_color_import_option=unreal.VertexColorImportOption.REPLACE,preserve_smoothing_groups=True,update_skeleton_reference_pose=False,use_t0_as_ref_pose=False).items():d.set_editor_property(k,v)
    return ui
def node(mat,cls,**props):
    n=EDIT.create_material_expression(mat,cls)
    for k,v in props.items():n.set_editor_property(k,v)
    return n
def link(a,pin,b,name):assert EDIT.connect_material_expressions(a,pin,b,name),(a,name)
def linear(h):
    rgb=[int(h[i:i+2],16)/255 for i in (0,2,4)]
    return tuple(x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4 for x in rgb)+(1.,)
def material(path):
    m=create(path,unreal.Material,unreal.MaterialFactoryNew());EDIT.delete_all_material_expressions(m)
    for k,v in dict(shading_model=unreal.MaterialShadingModel.MSM_UNLIT,blend_mode=unreal.BlendMode.BLEND_OPAQUE,two_sided=False,used_with_skeletal_mesh=True).items():m.set_editor_property(k,v)
    return m
def make_materials(textures):
    body=material(BASE+'/Materials/M_ControlRigMech_Toon3_SparseLines')
    normal=node(body,unreal.MaterialExpressionPixelNormalWS);vertex=node(body,unreal.MaterialExpressionVertexColor)
    uv1=node(body,unreal.MaterialExpressionTextureCoordinate,coordinate_index=1);uv2=node(body,unreal.MaterialExpressionTextureCoordinate,coordinate_index=2)
    mask=node(body,unreal.MaterialExpressionTextureSample,texture=textures['InternalLineMask'],sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    light=node(body,unreal.MaterialExpressionVectorParameter,parameter_name='ArtLightDirection',default_value=unreal.LinearColor(.35,.55,.76,0))
    ink=node(body,unreal.MaterialExpressionVectorParameter,parameter_name='InkColor',default_value=unreal.LinearColor(*linear('1B2422')))
    strength=node(body,unreal.MaterialExpressionScalarParameter,parameter_name='InternalLineStrength',default_value=.3)
    width=node(body,unreal.MaterialExpressionScalarParameter,parameter_name='InternalLineHalfWidthMeters',default_value=.006)
    code='float3 s=round(saturate(Palette.rgb)*255)/255; float3 c=lerp(s/12.92,pow((s+.055)/1.055,2.4),step(.04045,s)); float d=dot(normalize(N),normalize(L)); float band=d<.12?.42:(d<.55?.74:1); float dist=min(min(D1.x,D1.y),D2.x); float edge=1-smoothstep(Width-.0015,Width+.0015,dist); float internalInk=max(edge,Mask*.35); return lerp(c*band,Ink,saturate(internalInk*Strength));'
    custom=node(body,unreal.MaterialExpressionCustom,code=code,output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3,desc='B-v4 palette + 3 bands + sparse selected structural edges; no triangle wireframe')
    inputs={'Palette':(vertex,''),'N':(normal,''),'L':(light,'RGB'),'Ink':(ink,'RGB'),'D1':(uv1,''),'D2':(uv2,''),'Mask':(mask,'R'),'Strength':(strength,''),'Width':(width,'')}
    pins=[]
    for name in inputs:
        pin=unreal.CustomInput();pin.set_editor_property('input_name',name);pins.append(pin)
    custom.set_editor_property('inputs',pins)
    for name,(n,p) in inputs.items():link(n,p,custom,name)
    assert EDIT.connect_material_property(custom,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    EDIT.layout_material_expressions(body);EDIT.recompile_material(body);save(body)
    outline=material(BASE+'/Materials/M_ControlRigMech_BaseOutline')
    n=node(outline,unreal.MaterialExpressionConstant3Vector,constant=unreal.LinearColor(*linear('1B2422')))
    assert EDIT.connect_material_property(n,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR);EDIT.recompile_material(outline);save(outline)
    bodies=[]
    for i in range(4):
        m=create(BASE+f'/Materials/MI_ControlRigMech_LOD{i}',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
        EDIT.set_material_instance_parent(m,body)
        EDIT.set_material_instance_scalar_parameter_value(m,'InternalLineStrength',[.30,.22,.10,0][i])
        EDIT.set_material_instance_scalar_parameter_value(m,'InternalLineHalfWidthMeters',[.006,.007,.008,.006][i]);save(m);bodies.append(m)
    REPORT['shader']={'lighting':'unlit art light, same normal dot and 3 constant bands as Blender','light_UE':[.35,.55,.76],'coefficients':[.42,.74,1.],'thresholds':[.12,.55],'line_strength':[.30,.22,.10,0],'line_half_width_m':[.006,.007,.008,.006],'uv_channels':3,'vertex_palette':'sRGB FBX RGB -> explicit linear decode','outline':'evaluated inverted skinned hull from approved GN, backface culling, LOD3 disabled'}
    return bodies,outline
def tr(t):return {'translation_cm':list(t.translation.to_tuple()),'rotation_xyzw':list(t.rotation.to_tuple()),'scale':list(t.scale3d.to_tuple())}
def compare_bones(mesh):
    source=list(unreal.SkeletonService.list_bones(SRC+'/Meshes/SKM_Mech'));dest=list(unreal.SkeletonService.list_bones(mesh.get_path_name()))
    a={str(b.bone_name):b for b in source};b={str(b.bone_name):b for b in dest};assert set(a)==set(b) and len(b)==152
    position=angle=scale=0.; worst=[]
    for n,x in a.items():
        y=b[n];assert str(x.parent_bone_name)==str(y.parent_bone_name),n
        u=x.local_transform;v=y.local_transform
        p=(u.translation-v.translation).length();q0=u.rotation;q1=v.rotation
        dot=abs(sum(x*y for x,y in zip(q0.to_tuple(),q1.to_tuple())));dot/=math.sqrt(sum(x*x for x in q0.to_tuple())*sum(x*x for x in q1.to_tuple()))
        ang=math.degrees(2*math.acos(min(1.,dot)));sc=(u.scale3d-v.scale3d).length()
        position=max(position,p);angle=max(angle,ang);scale=max(scale,sc)
        if p>.01 or ang>.1 or sc>.0001:worst.append({'bone':n,'position_cm':p,'angle_deg':ang,'scale_delta':sc,'source':tr(u),'imported':tr(v)})
    data={'count':152,'names_and_parents_equal':True,'order_equal':[str(x.bone_name) for x in source]==[str(x.bone_name) for x in dest],'max_local_position_error_cm':position,'max_local_angle_error_deg':angle,'max_local_scale_delta':scale,'worst':worst}
    REPORT['reference_pose']=data;dump('ue_import_report.json',REPORT)
    assert position<.02 and angle<.1 and scale<.0001,data
def graph_signature(cr):
    return [{'name':g.get_name(),'nodes':[{'name':n.get_name(),'class':n.get_class().get_name(),'pins':[(p.get_name(),p.get_cpp_type(),p.get_default_value()) for p in n.get_pins()]} for n in g.get_nodes()],'links':[(x.get_source_pin().get_pin_path(),x.get_target_pin().get_pin_path()) for x in g.get_links()]} for g in cr.get_all_models()]
def transfer_animation(name,skeleton,mesh):
    """Rebuild a formal animation on the copied skeleton from frozen native TQS.

    UE's animation Skeleton property is read-only in Python. This uses the
    supported factory and data controller, retaining all real T/Q/S curves.
    No FBX animation unit conversion or generic curve normalization is applied.
    """
    target=BASE+'/Animations/'+name;capture=R/'Production_B_v4/AnimationSource'/f'{name}_FullPose.json'
    digest=hashlib.sha256(capture.read_bytes()).hexdigest();old=owned(target)
    if old and old.get_editor_property('skeleton')==skeleton and LIB.get_metadata_tag(old,'GuLi.AnimationSourceTQS_SHA256')==digest:return old
    if old:
        assert old.get_path_name().split('.')[0]==target and target.startswith(BASE+'/Animations/')
        assert LIB.delete_loaded_asset(old),target
    factory=unreal.AnimSequenceFactory();factory.set_editor_property('target_skeleton',skeleton);factory.set_editor_property('preview_skeletal_mesh',mesh)
    a=TOOLS.create_asset(name,BASE+'/Animations',unreal.AnimSequence,factory);assert a,target
    data=json.loads(capture.read_text(encoding='utf-8'));assert len(data['bone_names'])==152
    original=unreal.load_asset(SRC+'/Animations/'+name)
    for p in ['rate_scale','interpolation','enable_root_motion','force_root_lock','root_motion_root_lock','use_normalized_root_motion_scale','bone_compression_settings','curve_compression_settings','compression_error_threshold_scale','loop']:
        a.set_editor_property(p,original.get_editor_property(p))
    ctrl=a.get_editor_property('controller');ctrl.open_bracket('Preserve native Mech TQS and source reference units',False)
    ctrl.set_frame_rate(unreal.FrameRate(30,1),False);ctrl.set_number_of_frames(unreal.FrameNumber(data['frame_count']-1),False)
    for i,n in enumerate(data['bone_names']):
        keys=[f['local_tqs'][i] for f in data['frames']]
        assert ctrl.add_bone_curve(n,False),n
        assert ctrl.set_bone_track_keys(n,[unreal.Vector(*t[:3]) for t in keys],[unreal.Quat(*t[3:7]) for t in keys],[unreal.Vector(*t[7:10]) for t in keys],False),n
    ctrl.close_bracket(False);a.set_preview_skeletal_mesh(mesh)
    LIB.set_metadata_tag(a,'GuLi.SourceAsset',SRC+'/Animations/'+name);LIB.set_metadata_tag(a,'GuLi.AnimationSourceTQS_SHA256',digest)
    LIB.set_metadata_tag(a,'GuLi.AnimationTransfer','Native source local TQS at 30fps; full152 bones; no displacement/scale normalization')
    save(a);assert abs(a.get_play_length()-data['duration_s'])<1e-5;return a
def install():
    meta=json.loads((O/'blender_export_readback.json').read_text(encoding='utf-8'));assert meta['success']
    assert json.loads((R/'review_decisions.json').read_text(encoding='utf-8'))['B']['status']=='approved_for_UE_import'
    for a in unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(BASE,recursive=True):owned(str(a.package_name))
    dump('import_status.json',{'state':'running','stage':'skeleton_mesh'})
    sk=copy(SRC+'/Meshes/SK_Mech',BASE+'/Skeleton/SKEL_ControlRigMech')
    mesh=imported(O/meta['lods'][0]['fbx_path'],BASE+'/Meshes/SKM_ControlRigMech',opts(sk));compare_bones(mesh)
    sub=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    for i in range(1,4):
        if sub.get_lod_count(mesh)<=i:assert sub.import_lod(mesh,i,str(O/meta['lods'][i]['fbx_path']))==i
    for i in range(4):
        b=sub.get_lod_build_settings(mesh,i)
        for k,v in dict(use_full_precision_u_vs=True,use_high_precision_tangent_basis=True,use_high_precision_skin_weights=True,recompute_normals=False,recompute_tangents=False,remove_degenerates=False,threshold_position=0.,threshold_uv=0.).items():b.set_editor_property(k,v)
        sub.set_lod_build_settings(mesh,i,b)
    models=list(mesh.get_editor_property('source_models'))
    assert len(models)==4
    for i,m in enumerate(models):m.set_editor_property('screen_size',unreal.PerPlatformFloat(default=[1.,.40,.16,.06][i]))
    mesh.set_editor_property('source_models',models)
    dump('import_status.json',{'state':'running','stage':'textures_and_materials'})
    textures={}
    for role in ['BaseColor','InternalLineMask','FunctionalMask','ORM']:
        tex=imported(R/'Production_B_v4/Textures'/f'ControlRigMech_{role}_2K.png',BASE+f'/Textures/T_ControlRigMech_{role}_2K')
        tex.set_editor_property('srgb',role=='BaseColor');tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_DEFAULT if role=='BaseColor' else unreal.TextureCompressionSettings.TC_MASKS);save(tex);textures[role]=tex
    bodies,outline=make_materials(textures)
    slots=list(mesh.get_editor_property('materials'))
    for slot in slots:
        name=str(slot.material_slot_name)
        if 'Outline' in name:mat=outline
        else:assert 'Body_LOD' in name,name;mat=bodies[int(name.split('Body_LOD')[1][0])]
        slot.set_editor_property('material_interface',mat)
    mesh.set_editor_property('materials',slots);save(mesh);save(sk)
    REPORT['mesh']=mesh.get_path_name();REPORT['skeleton']=sk.get_path_name();REPORT['lods']=[]
    for i,m in enumerate(mesh.get_editor_property('source_models')):
        ns=sub.get_num_sections(mesh,i);indices=[sub.get_lod_material_slot(mesh,i,j) for j in range(ns)]
        REPORT['lods'].append({'lod':i,'source_triangles':meta['lods'][i]['triangles'],'source_sections':meta['lods'][i]['sections'],'actual_UE_sections':ns,'render_vertices':sub.get_num_verts(mesh,i),'screen_size':m.get_editor_property('screen_size').default,'material_indices':indices,'materials':[slots[j].material_interface.get_path_name() for j in indices]})
        assert ns==(2 if i<3 else 1),(i,ns)
    bounds=mesh.get_imported_bounds();REPORT['dimensions_cm']=list((bounds.box_extent*2).to_tuple())
    assert max(abs(a-b*100) for a,b in zip(REPORT['dimensions_cm'],meta['lods'][0]['dimensions_m']))<.02
    w=unreal.SkinWeightModifier();assert w.set_skeletal_mesh(mesh)
    rigid=flex=invalid=0;used=set();bone_names={str(b.bone_name) for b in unreal.SkeletonService.list_bones(mesh.get_path_name())}
    for i in range(w.get_num_vertices()):
        ws={str(n):float(v) for n,v in w.get_vertex_weights(i).items() if v>1e-7};used.update(ws)
        if not ws or abs(sum(ws.values())-1)>.0001 or not set(ws).issubset(bone_names):invalid+=1
        if len(ws)==1:rigid+=1
        else:flex+=1
    assert invalid==0,invalid;REPORT['skin']={'vertices_checked':w.get_num_vertices(),'rigid_vertices':rigid,'flexible_vertices':flex,'invalid_vertices':invalid,'weighted_bones':len(used)}
    REPORT['physics']=None;REPORT['sockets']=len(unreal.SkeletonService.list_sockets(mesh.get_path_name()));assert mesh.physics_asset is None and REPORT['sockets']==0
    dump('import_status.json',{'state':'running','stage':'native_animation_copies_and_controlrig'})
    REPORT['animations']=[]
    for name in ['Mech_Deploy','Mech_Idle','Mech_Walk']:
        a=transfer_animation(name,sk,mesh)
        save(a);assert a.get_editor_property('skeleton')==sk
        poses=[]
        for t in [0.,a.get_play_length()*.5,a.get_play_length()]:
            original=unreal.AnimSequenceService.get_pose_at_time(SRC+'/Animations/'+name,t,False);copied=unreal.AnimSequenceService.get_pose_at_time(a.get_path_name(),t,False)
            assert len(original)==len(copied)==152
            max_t=max((x.transform.translation-y.transform.translation).length() for x,y in zip(original,copied));max_s=max((x.transform.scale3d-y.transform.scale3d).length() for x,y in zip(original,copied))
            max_q=max(1-abs(sum(v*u for v,u in zip(x.transform.rotation.to_tuple(),y.transform.rotation.to_tuple()))) for x,y in zip(original,copied))
            assert max_t<.0001 and max_s<.00001 and max_q<.00001
            selected={str(x.bone_name):tr(x.transform) for x in copied if str(x.bone_name) in ['root','base','cannon_02']}
            poses.append({'time_s':t,'all_152_bone_translation_max_delta_cm':max_t,'scale_max_delta':max_s,'quaternion_one_minus_dot_max':max_q,'selected_bones':selected})
        REPORT['animations'].append({'path':a.get_path_name(),'skeleton':sk.get_path_name(),'seconds':a.get_play_length(),'samples':poses,'transfer':'Source native 30fps TQS reconstructed by UE AnimationDataController on copied skeleton','bone_tracks':152,'native_displacement_and_scale_preserved':True,'curve_normalization':False})
    cr=copy(SRC+'/Rigs/CR_Mech',BASE+'/Rigs/CR_ControlRigMech');original=unreal.load_asset(SRC+'/Rigs/CR_Mech');before=graph_signature(original)
    cr.set_preview_mesh(mesh);cr.recompile_vm();save(cr)
    after=graph_signature(cr);assert before==after
    REPORT['control_rig']={'path':cr.get_path_name(),'preview_mesh':cr.get_preview_mesh().get_path_name(),'graphs':len(after),'nodes':sum(len(x['nodes']) for x in after),'links':sum(len(x['links']) for x in after),'all_graph_nodes_pins_links_match_source':True}
    try:mesh.set_editor_property('default_animating_rig',cr);save(mesh)
    except Exception as e:REPORT['default_animating_rig_note']=str(e)
    REPORT['textures']=[{'path':t.get_path_name(),'srgb':t.srgb,'compression':str(t.compression_settings)} for t in textures.values()]
    REPORT['success']=True;dump('ue_import_report.json',REPORT);dump('import_status.json',{'state':'complete'})
def main():
    assert '-ControlRigMechImportWorker' in unreal.SystemLibrary.get_command_line()
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world();cvar='Interchange.FeatureFlags.Import.Enable';old=unreal.SystemLibrary.get_console_variable_int_value(cvar)
    unreal.SystemLibrary.execute_console_command(world,cvar+' 0')
    try:install()
    except Exception:REPORT['error']=traceback.format_exc();dump('ue_import_report.json',REPORT);dump('import_status.json',{'state':'failed','error':REPORT['error']});unreal.log_error(REPORT['error'])
    finally:unreal.SystemLibrary.execute_console_command(world,cvar+' '+str(old));unreal.SystemLibrary.quit_editor()
if __name__=='__main__':main()
