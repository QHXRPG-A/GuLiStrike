"""Scoped production asset authoring. No PIE, screenshots or native compilation.

Set GROUND_HUD_STAGE to 'art', 'cards', 'tables', 'scene' or 'loaded' in the editor caller.
Tables require the current native schema to be loaded.
"""
import json
import hashlib
import sys
import runpy
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'ArtSource/UI/GroundMech/HUD_v1_20261101/Integration'
OUT.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(ROOT/'Scripts/Cards'))
from card_reveal_graph import B,Graph,Node,variable,compile_save,require
from card_artwork_config import DIRECTOR,write_update_card_function

def art():
    path='/Game/GuLiStrike/GroundMech/UI/Textures/T_GroundHUD_Icons'
    task=unreal.AssetImportTask();task.filename=str(ROOT/'ArtSource/UI/GroundMech/HUD_v1_20261101/04_Runtime_Icons.png')
    task.destination_path=path.rsplit('/',1)[0];task.destination_name=path.rsplit('/',1)[1]
    task.automated=True;task.replace_existing=True;task.save=False
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    tex=unreal.load_asset(path);require(tex,'Imported atlas')
    tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    tex.set_editor_property('lod_group',unreal.TextureGroup.TEXTUREGROUP_UI)
    tex.set_editor_property('mip_gen_settings',unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    tex.set_editor_property('srgb',True)
    require(unreal.EditorAssetLibrary.save_loaded_asset(tex),'Save atlas')
    frame='/Game/Assets/UI/HUDuiPRO/Textures/Others/Basic_Elements/Squares/Boxes/T_HUD_Box_1'
    require(unreal.EditorAssetLibrary.does_asset_exist(frame),'HUDuiPRO frame')
    return {'atlas':tex.get_path_name(),'size':[tex.blueprint_get_size_x(),tex.blueprint_get_size_y()],'frame':frame,'saved':True}

def cards():
    variable(DIRECTOR,'DirectSelection','bool',False,True,'Live Presentation')
    from card_presentation_size import ensure_size_variables, write_pointer_function
    ensure_size_variables()
    compile_save(DIRECTOR)
    g=Graph(DIRECTOR,'ActivateHit',True)
    g.branch(g.both(g.native('Not_PreBool',A=g.get('AwaitingConfirmation')),g.compare('GreaterEqual',g.arg('HitIndex'),0,'Int'),g.compare('Less',g.arg('HitIndex'),g.get('ActiveCardCount'),'Int')))
    stable=g.branch(g.compare('Equal',g.get('Phase'),1,'Int'))
    g.set('SelectedIndex',g.arg('HitIndex'))
    direct=g.branch(g.get('DirectSelection'))
    g.invoke('SetPhase',NewPhase=3);g.set('AwaitingConfirmation',True);g.set('HoveredIndex',-1)
    event=Node(g,B.add_call_delegate_node(DIRECTOR,g.name,'OnConfirmationRequested',*g.pos()));event.put('execute',g.tail);event.put('SelectedIndex',g.get('SelectedIndex'))
    g.tail=direct['else'];g.invoke('SetPhase',NewPhase=2)
    g.tail=stable['else']
    g.branch(g.both(g.compare('Equal',g.get('Phase'),3,'Int'),g.compare('Equal',g.arg('HitIndex'),g.get('SelectedIndex'),'Int')))
    external=g.branch(g.get('ExternalConfirmation'))
    g.set('AwaitingConfirmation',True);g.set('HoveredIndex',-1)
    event=Node(g,B.add_call_delegate_node(DIRECTOR,g.name,'OnConfirmationRequested',*g.pos()));event.put('execute',g.tail);event.put('SelectedIndex',g.get('SelectedIndex'))
    g.tail=external['else'];g.invoke('SetPhase',NewPhase=4);g.layout()
    write_pointer_function()
    write_update_card_function()
    result=compile_save(DIRECTOR)
    cdo=unreal.get_default_object(unreal.load_asset(DIRECTOR).generated_class())
    return {'director':DIRECTOR,'compile_save':str(result),'direct_default':cdo.get_editor_property('DirectSelection'),'activate_nodes':len(B.get_nodes_in_graph(DIRECTOR,'ActivateHit')),'update_nodes':len(B.get_nodes_in_graph(DIRECTOR,'UpdateCard'))}

def tables():
    require(bool(unreal.load_object(None,'/Script/GuLiStrike.GuLiStrikeGroundHUDElementsRow')),'Current native HUD schema is not loaded; compile/load before table import')
    names={'DT_GuLiStrikeGroundHUD_Elements','DT_GuLiStrikeGameTexts_Texts'}
    runpy.run_path(str(ROOT/'Scripts/import_data_to_engine.py'),init_globals={'GULI_TABLE_FILTER':names,'GULI_IMPORT_REPORT':str(OUT/'table-import.json'),'GULI_IMPORT_PROGRESS':str(OUT/'table-import.log')})
    report=json.loads((OUT/'table-import.json').read_text(encoding='utf-8'))
    require(not report.get('errors'),'HUD table import errors')
    strings=unreal.load_asset('/Game/GuLiStrike/Data/ST_GuLiStrikeGameTexts')
    require(strings,'Existing shared StringTable')
    require(unreal.GuLiRogueCardPresentationLibrary.rebuild_game_text_string_table(strings,unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeGameTexts_Texts')),'Rebuild shared StringTable')
    require(unreal.EditorAssetLibrary.save_loaded_asset(strings,False),'Save shared StringTable')
    table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeGroundHUD_Elements')
    rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    require(len(rows)==len(json.loads((ROOT/'data/Json/DT_GuLiStrikeGroundHUD_Elements.json').read_text(encoding='utf-8'))),'HUD presentation row count')
    for row in rows:
        ref=row.get('Texture','')
        if ref and ref!='None':require(bool(unreal.load_asset(ref)),f'HUD texture reference {ref}')
    report['hud_rows']=rows;report['string_table']=strings.get_path_name()
    return report

def certify_scene(world, targets):
    """Use the production PIE gate and preserve the certified resource geometry."""
    def bake_result(value):
        keys=('success','message','source_hash','layout_hash','territory_count',
              'cluster_count','node_count','initial_soldier_count','validated_initial_soldier_count')
        return {**{key:value.get_editor_property(key) for key in keys},
                'issues':list(map(str,value.get_editor_property('issues')))}
    definition=unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap')
    require(definition,'Existing certified resource layout')
    def geometry_digest():
        digest=hashlib.sha256()
        for key in ('spawn_anchors','territories','clusters','nodes'):
            value=definition.get_editor_property(key)
            for item in value if key!='spawn_anchors' else [value]:
                digest.update((key+'\0'+item.export_text()+'\n').encode('utf-8'))
        return digest.hexdigest()
    report={'before':bake_result(unreal.GuLiResourceAuthoringLibrary.validate_current_bake()),
            'geometry_before':geometry_digest()}
    if not report['before']['success']:
        require(report['before']['message']=='GuLiMapAuthoring source changed after the last resource bake; rebake before PIE.',
                'Resource gate: '+report['before']['message'])
        # This API first proves that excluding only these two additions reproduces
        # the certified source, then audits every current deployment against it.
        report['dry_run']=bake_result(unreal.GuLiResourceAuthoringLibrary.revalidate_added_deployments(targets,False))
        require(report['dry_run']['success'],'Added HUD targets: '+report['dry_run']['message'])
        report['applied']=bake_result(unreal.GuLiResourceAuthoringLibrary.revalidate_added_deployments(targets,True))
        require(report['applied']['success'],'Save deployment certification: '+report['applied']['message'])
    report['after']=bake_result(unreal.GuLiResourceAuthoringLibrary.validate_current_bake())
    require(report['after']['success'],'PIE resource gate: '+report['after']['message'])
    report['geometry_after']=geometry_digest()
    require(report['geometry_before']==report['geometry_after'],'Preserve certified resource geometry')
    report['manifests']=[]
    for population in (200,600):
        path=ROOT/f'Config/Hybrid/RuntimeManifest-{population}.json'
        exported=json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population,str(path)))
        require(exported.get('success'),'Manifest export: '+json.dumps(exported))
        validated=json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(path)))
        require(validated.get('success'),'Manifest validation: '+json.dumps(validated))
        report['manifests'].append({'population':population,'path':str(path),'export':exported,
                                    'validation':validated,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
    navigation=unreal.GuLiNavigationBakeLibrary.validate_world_navigation(world)
    report['navigation']={'success':navigation.get_editor_property('success'),'message':navigation.get_editor_property('message')}
    require(report['navigation']['success'],'PIE navigation gate: '+report['navigation']['message'])
    return report


def scene():
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    require(world and world.get_path_name().startswith('/Game/Maps/LVL_CommanderMassPrototype.'),'Keep the current approved delivery map')
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing={a.get_actor_label():a for a in actors.get_all_level_actors()}
    require('GroundRogue_Entry' in existing and 'GroundMobility_Entry' in existing,'Reuse the authored Ground review area')
    bp=unreal.load_asset('/Game/GuLiStrike/GroundMech/BP_GroundMech_Light')
    require(bp,'Formal Ground character')
    cdo=unreal.get_default_object(bp.generated_class())
    model=cdo.get_editor_property('model_id');require(model==4001,'Current Ground model route')
    position=existing['GroundRogue_Entry'].get_actor_location()
    label='GroundHUD_v1_PlayerEntry'
    entry=existing.get(label) or actors.spawn_actor_from_class(unreal.Note,position+unreal.Vector(0,0,80))
    entry.set_actor_label(label)
    native_ready=hasattr(unreal,'GuLiGroundHUDWidget') and unreal.EditorAssetLibrary.does_asset_exist('/Game/GuLiStrike/Data/DT_GuLiStrikeGroundHUD_Elements')
    entry.set_editor_property('text',('Ground HUD v1：选择红队 Ground，使用 GroundRogue_Entry(-2800,54000,1149) 或相邻 GroundMobility_Entry。左键射击、右键瞄准、Space喷气、Shift闪现；M地图、I详情、F4正面单击选牌、Esc关闭。GroundHUD_ShieldTarget有500护盾/400生命，GroundHUD_KillTarget有20生命；均为静止靶。数据5Hz，事件同步。{load_status}；运行、性能、视觉待玩家验收。').format(load_status='新原生HUD及配置表已加载' if native_ready else '新原生代码和配置表待编译加载'))
    targets=[]
    for name,xyz,hp,shield in [('GroundHUD_ShieldTarget',(-5000,53000,905),400,500),('GroundHUD_KillTarget',(-6200,53000,905),20,0)]:
        target=existing.get(name) or actors.spawn_actor_from_class(unreal.GuLiCommanderDeploymentPoint,unreal.Vector(*xyz))
        target.set_actor_label(name)
        for prop,value in {'team':unreal.GuLiTeam.BLUE,'unit_type_id':1,'rows':1,'columns':1,'allow_automatic_fire':False,'start_idle':True,'use_in_population_manifest':True,'initial_max_health_override':hp,'initial_shield':shield}.items():
            target.set_editor_property(prop,value)
        targets.append(target)
    require(unreal.EditorLoadingAndSavingUtils.save_map(world,'/Game/Maps/LVL_CommanderMassPrototype'),'Save delivery map')
    certification=certify_scene(world,targets)
    def read(a):
        p=a.get_actor_location();r={'actor':a.get_path_name(),'label':a.get_actor_label(),'class':a.get_class().get_name(),'position':[p.x,p.y,p.z]}
        if isinstance(a,unreal.GuLiCommanderDeploymentPoint):
            r.update({key:str(a.get_editor_property(key)) for key in ['team','unit_type_id','rows','columns','allow_automatic_fire','start_idle','use_in_population_manifest','initial_max_health_override','initial_shield']})
        return r
    related=[entry,*targets,existing['GroundRogue_Entry'],existing['GroundMobility_Entry'],existing['GroundRogue_Wall'],existing['GroundRogue_Friendly']]
    return {'map':world.get_path_name(),'entities':[read(a) for a in related],'note':entry.get_editor_property('text'),'ground_blueprint':bp.get_path_name(),'model_id':model,'saved':True,'native_hud_loaded':hasattr(unreal,'GuLiGroundHUDWidget'),'certification':certification}


def loaded():
    classes={}
    for name in ['GuLiGroundHUDData','GuLiGroundHUDWidget','GuLiGroundMapWidget','GuLiStrikeGroundHUDElementsRow']:
        obj=unreal.load_object(None,'/Script/GuLiStrike.'+name);require(obj,'Loaded native '+name);classes[name]=obj.get_path_name()
    formal='/Game/GuLiStrike/GroundMech/BP_GroundMech_Light'
    compiled={formal:str(compile_save(formal)),DIRECTOR:str(compile_save(DIRECTOR))}
    bp=unreal.load_asset(formal);cdo=unreal.get_default_object(bp.generated_class())
    mesh=cdo.get_editor_property('mesh').get_skinned_asset();require(mesh,'Formal Ground mesh')
    require(mesh.get_path_name().startswith('/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/Meshes/SK_GroundMech_A4B2.'),'Current A4B2 mesh')
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    mode=world.get_world_settings().get_editor_property('default_game_mode');require(mode,'Authored GameMode')
    defaults=unreal.get_default_object(mode)
    hud=defaults.get_editor_property('hud_class');controller=defaults.get_editor_property('player_controller_class')
    require(hud and isinstance(unreal.get_default_object(hud),unreal.GuLiCommanderHUD),'Formal HUD route')
    require(controller and isinstance(unreal.get_default_object(controller),unreal.GuLiCommanderPlayerController),'Formal modal input route')
    grid=unreal.load_asset('/Game/GuLiStrike/Data/Navigation/DA_CommanderGroundGrid');require(grid,'Ground terrain source')
    cells=grid.get_editor_property('cells');samples=len(grid.get_editor_property('vertex_heights'))
    require(cells.x==567 and cells.y==567 and samples==(cells.x+1)*(cells.y+1),'Height field dimensions')
    table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeGroundHUD_Elements')
    rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    refresh=next(r for r in rows if r['Name']=='RefreshSeconds')['Value'];require(abs(refresh-.2)<.0001,'5 Hz refresh configuration')
    director=unreal.get_default_object(unreal.load_asset(DIRECTOR).generated_class())
    require(not director.get_editor_property('DirectSelection'),'Shared Commander default remains reveal')
    return {'native_classes':classes,'blueprint_compiles':compiled,'world':world.get_path_name(),'game_mode':mode.get_path_name(),'hud_class':hud.get_path_name(),'controller_class':controller.get_path_name(),'ground_blueprint':formal,'model_id':cdo.get_editor_property('model_id'),'mesh':mesh.get_path_name(),'grid':{'cells':[cells.x,cells.y],'height_samples':samples,'source_hash':grid.get_editor_property('source_hash')},'hud_rows':len(rows),'refresh_seconds':refresh,'direct_selection_default':False,'pie':unreal.WidgetService.is_pie_running(),'dirty_maps':[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],'dirty_content':[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]}

require(not unreal.WidgetService.is_pie_running(),'Editor must be idle; no PIE mutation')
stage=globals().get('GROUND_HUD_STAGE','art')
result={'stage':stage,'result':{'art':art,'cards':cards,'tables':tables,'scene':scene,'loaded':loaded}[stage]()}
(OUT/f'{stage}-readback.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result))
