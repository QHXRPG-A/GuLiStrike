"""Store the released Blue/Red mesh/color variants; reuse immutable compatible v1 dependencies."""
import unreal,json,hashlib,traceback,math,runpy
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');D=R/'UE_Delivery_Team_v2';BASE='/Game/GuLiStrike/Buildings/SSFStylized'
OWNER='GuLi.SSFStylized.Team_B_v2.20261007'
L=unreal.EditorAssetLibrary;T=unreal.AssetToolsHelpers.get_asset_tools();E=unreal.MaterialEditingLibrary
SK=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
REPORT={'success':False,'source_version':'SSF_TeamPalette_B_v2','assets':[],'saved_packages':[],
        'shared_dependencies':[],'animations':[],'gameplay_integrated':False,'maps_saved':[],'source_packages_modified':False}
def dump():(D/'ue_import.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
def allowed(path):return any(path.startswith(BASE+'/'+team+'/') for team in ['Blue','Red'])
def owned(path):
    assert allowed(path),path
    if L.does_asset_exist(path):assert L.get_metadata_tag(unreal.load_asset(path),'GuLi.StyleOwner')==OWNER,('unowned target',path)
def save(o):
    p=o.get_path_name();assert allowed(p),p
    L.set_metadata_tag(o,'GuLi.StyleOwner',OWNER);L.set_metadata_tag(o,'GuLi.SourceVersion','SSF_TeamPalette_B_v2')
    L.set_metadata_tag(o,'GuLi.ReleaseRecord',str(R/'approval_B_v2_import_20261007.json'))
    L.set_metadata_tag(o,'GuLi.StyleSourceSHA256',auth['source_blend_sha256'])
    assert L.save_loaded_asset(o,False),p
    if p not in REPORT['saved_packages']:REPORT['saved_packages'].append(p)
    dump()
    return o
def import_file(file,path,ui=None):
    owned(path);folder,name=path.rsplit('/',1);L.make_directory(folder)
    task=unreal.AssetImportTask()
    for k,v in dict(filename=str(file),destination_path=folder,destination_name=name,automated=True,async_=False,replace_existing=True,replace_existing_settings=True,save=False).items():task.set_editor_property(k,v)
    task.factory=unreal.FbxFactory() if ui else unreal.TextureFactory()
    if ui:task.options=ui
    T.import_asset_tasks([task]);o=unreal.load_asset(path);assert o,(path,list(task.imported_object_paths))
    L.set_metadata_tag(o,'GuLi.StyleOwner',OWNER);L.set_metadata_tag(o,'GuLi.ImportFileSHA256',hashlib.sha256(Path(file).read_bytes()).hexdigest());return o
def linear(code):
    rgb=[int(code[i:i+2],16)/255 for i in (1,3,5)]
    return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in rgb)+(1.,)
def instance(folder,name,parent,theme,mask=None):
    path=folder+'/Materials/'+name;owned(path)
    if L.does_asset_exist(path):o=unreal.load_asset(path)
    else:o=T.create_asset(name,folder+'/Materials',unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    assert o,path
    E.set_material_instance_parent(o,parent)
    E.set_material_instance_vector_parameter_value(o,'Base Color',unreal.LinearColor(1,1,1,1))
    E.set_material_instance_vector_parameter_value(o,'Team Color',unreal.LinearColor(*linear(theme['Accent'])))
    if mask:E.set_material_instance_texture_parameter_value(o,'Internal Line Mask',mask)
    E.update_material_instance(o);return save(o)
def options(skeleton):
    ui=unreal.FbxImportUI();kind=unreal.FBXImportType.FBXIT_SKELETAL_MESH
    for k,v in dict(import_materials=False,import_textures=False,import_mesh=True,import_as_skeletal=True,import_animations=False,create_physics_asset=False,automated_import_should_detect_type=False,mesh_type_to_import=kind,original_import_type=kind).items():ui.set_editor_property(k,v)
    ui.skeleton=skeleton;data=ui.skeletal_mesh_import_data
    for k,v in dict(convert_scene=True,convert_scene_unit=True,force_front_x_axis=False,import_uniform_scale=1.,normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS,import_mesh_lo_ds=False,update_skeleton_reference_pose=False,use_t0_as_ref_pose=False,preserve_smoothing_groups=True).items():data.set_editor_property(k,v)
    return ui
def exact_reference_pose(mesh,source):
    expected=list(unreal.SkeletonService.list_bones(source.get_path_name()));before=list(unreal.SkeletonService.list_bones(mesh.get_path_name()))
    assert [(b.bone_name,b.parent_bone_name) for b in before]==[(b.bone_name,b.parent_bone_name) for b in expected]
    modifier=unreal.SkeletonModifier();assert modifier.set_skeletal_mesh(mesh)
    assert modifier.set_bones_transforms([b.bone_name for b in expected],[b.local_transform for b in expected],True)
    committed=modifier.commit_skeleton_to_skeletal_mesh();forced=False
    if not committed and any(a.local_transform.export_text()!=b.local_transform.export_text() for a,b in zip(before,expected)):
        temporary=expected[0].local_transform.copy();temporary.translation+=unreal.Vector(.1,0,0)
        assert modifier.set_bone_transform(expected[0].bone_name,temporary,True);assert modifier.commit_skeleton_to_skeletal_mesh()
        modifier=unreal.SkeletonModifier();assert modifier.set_skeletal_mesh(mesh)
        assert modifier.set_bones_transforms([b.bone_name for b in expected],[b.local_transform for b in expected],True)
        assert modifier.commit_skeleton_to_skeletal_mesh();forced=True
    after=list(unreal.SkeletonService.list_bones(mesh.get_path_name()));errors=[0.,0.,0.]
    for a,b in zip(after,expected):
        x,y=a.local_transform,b.local_transform;q,r=x.rotation,y.rotation
        dot=abs(q.x*r.x+q.y*r.y+q.z*r.z+q.w*r.w)/(math.sqrt(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w)*math.sqrt(r.x*r.x+r.y*r.y+r.z*r.z+r.w*r.w))
        values=[(x.translation-y.translation).length(),(x.scale3d-y.scale3d).length(),math.degrees(2*math.acos(min(1,dot)))]
        errors=[max(i,j) for i,j in zip(errors,values)]
    assert errors[0]<.0001 and errors[1]<.00001 and errors[2]<.001,(mesh.get_name(),errors)
    return {'bones':len(after),'error_cm_scale_degrees':errors,'committed':committed,'forced_below_epsilon_exact_commit':forced,'root_scale':list(after[0].local_transform.scale3d.to_tuple())}
def export_mesh(mesh,key):
    folder=D/'UE_Readback';folder.mkdir(exist_ok=True);path=folder/(key+'.fbx')
    opt=unreal.FbxExportOption();opt.ascii=False;opt.collision=False;opt.level_of_detail=True;opt.export_morph_targets=False;opt.bake_material_inputs=unreal.FbxMaterialBakeMode.DISABLED
    task=unreal.AssetExportTask()
    for k,v in dict(object=mesh,filename=str(path),options=opt,automated=True,replace_identical=True,prompt=False,exporter=unreal.SkeletalMeshExporterFBX()).items():task.set_editor_property(k,v)
    assert unreal.Exporter.run_asset_export_task(task);return str(path)
def run():
    global auth
    assert '-SSFTeamImportWorker' in unreal.SystemLibrary.get_command_line()
    auth=json.loads((R/'approval_B_v2_import_20261007.json').read_text(encoding='utf8'))
    assert auth['status']=='released_for_formal_UE_storage' and not auth['gameplay_integration_authorized']
    assert hashlib.sha256(Path(auth['source_blend']).read_bytes()).hexdigest()==auth['source_blend_sha256']
    probe=json.loads((D/'ue_probe.json').read_text(encoding='utf8'));assert probe['success']
    export=json.loads((D/'export_report.json').read_text(encoding='utf8'));back=json.loads((D/'fbx_readback.json').read_text(encoding='utf8'));assert export['success'] and back['success']
    checks={(a['asset'],a['LOD']):a for a in back['checks']}
    old=json.loads((R/'UE_Delivery_v1/ue_import.json').read_text(encoding='utf8'));assert old['success'];previous={a['key']:a for a in old['assets']}
    parents={role:unreal.load_asset(BASE+'/Shared/Materials/'+name) for role,name in [('SSF_Body','M_SSF_ThreeToneLine'),('SSF_Outline','M_SSF_Outline'),('SSF_Display','M_SSF_SourceDisplay')]}
    assert all(parents.values())
    skeleton_snapshot={a['building']:list(unreal.SkeletonService.list_bones(previous[a['building']]['skeleton'])) for a in export['assets']}
    for a in export['assets']:
        team,key,variant=a['team'],a['building'],a['key'];REPORT['stage']=variant;dump()
        folder=BASE+'/'+team+'/'+key;L.make_directory(folder+'/Materials');L.make_directory(folder+'/Meshes');L.make_directory(folder+'/Textures')
        old_record=previous[key];original=unreal.load_asset(old_record['path']);skeleton=original.skeleton;physics=original.physics_asset
        atlas=import_file(a['base_color_texture'],folder+'/Textures/T_'+variant+'_BaseColor_2K')
        atlas.set_editor_property('srgb',True);atlas.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_DEFAULT)
        atlas.set_editor_property('address_x',unreal.TextureAddress.TA_CLAMP);atlas.set_editor_property('address_y',unreal.TextureAddress.TA_CLAMP);save(atlas)
        mask=unreal.load_asset(BASE+'/'+key+'/Textures/T_'+key+'_InternalLineMask_2K');assert mask
        mats={'SSF_Body':instance(folder,'MI_'+variant+'_Body',parents['SSF_Body'],a['theme'],mask),'SSF_Outline':parents['SSF_Outline']}
        if 'SSF_Display' in a['lods'][0]['materials']:mats['SSF_Display']=instance(folder,'MI_'+variant+'_Display',parents['SSF_Display'],a['theme'])
        path=folder+'/Meshes/SK_'+variant;mesh=import_file(a['lods'][0]['fbx'],path,options(skeleton))
        assert mesh.skeleton==skeleton
        for row in a['lods']:
            assert row['sha256']==checks[(variant,row['LOD'])]['fbx_sha256']
            if row['LOD']:assert SK.import_lod(mesh,row['LOD'],row['fbx'])==row['LOD']
        assert SK.get_lod_count(mesh)==3
        infos=list(mesh.get_editor_property('source_models'))
        for i,info in enumerate(infos):
            screen=info.get_editor_property('screen_size');screen.set_editor_property('default',(1.,.10,.035)[i]);info.set_editor_property('screen_size',screen)
        mesh.set_editor_property('source_models',infos)
        for lod in range(3):
            settings=SK.get_lod_build_settings(mesh,lod)
            for k,v in dict(recompute_normals=False,recompute_tangents=False,use_full_precision_u_vs=True).items():settings.set_editor_property(k,v)
            SK.set_lod_build_settings(mesh,lod,settings)
        slots=list(mesh.get_editor_property('materials'));slot_names=[]
        for slot in slots:
            name=str(slot.get_editor_property('imported_material_slot_name'));assert name in mats,name
            slot.set_editor_property('material_interface',mats[name]);slot_names.append(name)
        mesh.set_editor_property('materials',slots);mesh.set_editor_property('physics_asset',physics)
        reference=exact_reference_pose(mesh,original)
        L.set_metadata_tag(mesh,'GuLi.SourceAsset',original.get_path_name());L.set_metadata_tag(mesh,'GuLi.TeamPalette',team)
        L.set_metadata_tag(mesh,'GuLi.BaseColorAtlas',atlas.get_path_name())
        bounds=mesh.get_imported_bounds();dimensions=list((bounds.box_extent*2).to_tuple())
        want=[100*(a['lods'][0]['bounds_m']['max'][i]-a['lods'][0]['bounds_m']['min'][i]) for i in range(3)]
        assert max(abs(x-y) for x,y in zip(dimensions,want))<.1,(variant,dimensions,want)
        save(mesh)
        REPORT['assets'].append({'key':variant,'building':key,'team':team,'path':path,'type':'SkeletalMesh','source':original.get_path_name(),
            'skeleton':skeleton.get_path_name(),'physics':physics.get_path_name() if physics else None,'LOD_count':3,
            'screen_sizes':[1.,.10,.035],'dimensions_cm':dimensions,'reference_contract':reference,'theme':a['theme'],
            'body_and_outline_triangles':[{'body':r['body_triangles'],'outline':r['outline_triangles']} for r in a['lods']],
            'materials':{k:v.get_path_name() for k,v in mats.items()},'material_slots':slot_names,'base_color_atlas':atlas.get_path_name(),
            'line_mask':mask.get_path_name(),'UE_export_readback':export_mesh(mesh,variant)})
        dump();print('SSF_TEAM_IMPORTED',variant,flush=True)
    for key,before in skeleton_snapshot.items():
        after=list(unreal.SkeletonService.list_bones(previous[key]['skeleton']))
        assert [(b.bone_name,b.parent_bone_name,b.local_transform.export_text()) for b in before]==[(b.bone_name,b.parent_bone_name,b.local_transform.export_text()) for b in after],('shared skeleton modified',key)
    REPORT['animations']=[a for a in old['animations'] if a['key'] in skeleton_snapshot]
    assert len(REPORT['assets'])==12 and len(REPORT['animations'])==21
    REPORT['shared_dependencies']=sorted({a['skeleton'] for a in REPORT['assets']}|{a['physics'] for a in REPORT['assets'] if a['physics']}|{a['path'] for a in REPORT['animations']}|{m.get_path_name() for m in parents.values()}|{a['line_mask'] for a in REPORT['assets']})
    REPORT.update(success=True,stage='saved_pending_independent_reload',engine=unreal.SystemLibrary.get_engine_version(),previous_v1_preserved=True,
        animation_policy='Reuse the 21 unchanged formal building animations and original six compatible formal skeletons/physics assets. The existing Drone and its 22nd animation remain untouched.')
    dump()
if __name__=='__main__':
    assert '-SSFTeamImportWorker' in unreal.SystemLibrary.get_command_line()
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world();flag='Interchange.FeatureFlags.Import.Enable';before=unreal.SystemLibrary.get_console_variable_int_value(flag)
    unreal.SystemLibrary.execute_console_command(world,flag+' 0')
    try:run()
    except Exception:REPORT['error']=traceback.format_exc();dump();unreal.log_error(REPORT['error'])
    finally:unreal.SystemLibrary.execute_console_command(world,flag+' '+str(before));unreal.SystemLibrary.quit_editor()
