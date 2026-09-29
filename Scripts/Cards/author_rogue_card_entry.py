"""Deploy after the approved native build. No PIE or image inspection.

Updates only the card presentation contract, tables, UI capture material and named maps.
"""
import json
import runpy
import sys
import traceback
from pathlib import Path
import unreal

ROOT_DIR=Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0,str(ROOT_DIR/'Scripts/Cards'))
from card_reveal_graph import B,Graph,Node,variable,function,compile_save,require
from card_artwork_config import ROOT,CARD,DIRECTOR,write_start_function
from card_presentation_size import write_pointer_function

ENTRY_INSTRUCTIONS = ('重防号肉鸽卡：就绪指挥官席位按F4，随机显示最多三张合格卡；底部右侧“重选”免费重抽，'
                      '优先换成未显示的合格牌，无其他牌时提示并保留原牌。Esc关闭后重开保持最新候选。'
                      '初始无导弹技能/导弹仓；重防导弹仓04.01每队一次。解锁后出现导弹伤害03.01与雨点攻势05.01，'
                      '后者每次加一枚同时齐射导弹，可叠加。单击翻牌，再单击确认。现有/后续单位同队生效。运行与美术效果待验证。')


def confirmation_contract():
    variable(DIRECTOR,'ActiveCardCount','int',3,True,'Live Presentation')
    for name in ['ExternalConfirmation','AwaitingConfirmation','UseLiveCardData']:
        variable(DIRECTOR,name,'bool',False,True,'Live Presentation')
    if not B.variable_exists(DIRECTOR,'LiveCardIds'):
        require(B.add_variable(DIRECTOR,'LiveCardIds','FString','()',True,'Array'),'LiveCardIds')
    if not B.variable_exists(DIRECTOR,'OnConfirmationRequested'):
        require(B.add_event_dispatcher(DIRECTOR,'OnConfirmationRequested'),'Confirmation dispatcher')
        require(B.add_event_dispatcher_parameter(DIRECTOR,'OnConfirmationRequested','SelectedIndex','int'),'Confirmation index')
    function(DIRECTOR,'CompleteConfirmation')
    compile_save(DIRECTOR)
    g=Graph(DIRECTOR,'CompleteConfirmation',True)
    g.branch(g.both(g.get('AwaitingConfirmation'),g.compare('Equal',g.get('Phase'),3,'Int')))
    g.set('AwaitingConfirmation',False)
    g.invoke('SetPhase',NewPhase=4);g.layout()
    g=Graph(DIRECTOR,'ActivateHit',True)
    g.branch(g.both(g.native('Not_PreBool',A=g.get('AwaitingConfirmation')),
                    g.compare('GreaterEqual',g.arg('HitIndex'),0,'Int'),g.compare('Less',g.arg('HitIndex'),g.get('ActiveCardCount'),'Int')))
    select=g.branch(g.compare('Equal',g.get('Phase'),1,'Int'))
    g.set('SelectedIndex',g.arg('HitIndex'));g.invoke('SetPhase',NewPhase=2)
    g.tail=select['else']
    g.branch(g.both(g.compare('Equal',g.get('Phase'),3,'Int'),g.compare('Equal',g.arg('HitIndex'),g.get('SelectedIndex'),'Int')))
    external=g.branch(g.get('ExternalConfirmation'))
    g.set('AwaitingConfirmation',True);g.set('HoveredIndex',-1)
    event=Node(g,B.add_call_delegate_node(DIRECTOR,g.name,'OnConfirmationRequested',*g.pos()))
    event.put('execute',g.tail);event.put('SelectedIndex',g.get('SelectedIndex'))
    g.tail=external['else'];g.invoke('SetPhase',NewPhase=4);g.layout()
    write_start_function();write_pointer_function()
    from card_artwork_config import write_update_card_function
    write_update_card_function()
    return compile_save(DIRECTOR)


def card_local_motion():
    g=Graph(CARD,'ApplyFrame',True)
    a=g.arg('FlightAlpha')
    g.call('Actor','K2_SetActorRelativeLocation',NewRelativeLocation=g.vec(g.arg('DisplayX')*a,(a-1)*g.arg('FarDistance'),0),bSweep=False,bTeleport=True)
    size=g.arg('DisplayScale')*g.native('Lerp',A=.015,B=1,Alpha=a)
    g.call('Actor','SetActorScale3D',NewScale3D=g.vec(size,size,size))
    g.call('Actor','SetActorHiddenInGame',bNewHidden=g.native('Not_PreBool',A=g.arg('Visible')))
    for current,target in [('CurrentYaw',g.arg('HoverX')*g.arg('MaxTilt')*-1),('CurrentRoll',g.arg('HoverY')*g.arg('MaxTilt'))]:
        g.set(current,g.native('FInterpTo',Current=g.get(current),Target=target,DeltaTime=g.arg('DeltaSeconds'),InterpSpeed=g.arg('HoverSpeed')))
    g.method('SceneComponent','K2_SetRelativeRotation',g.get('HoverPivot'),NewRotation=g.rot(roll=g.get('CurrentRoll'),yaw=g.get('CurrentYaw')),bSweep=False,bTeleport=True)
    g.method('SceneComponent','K2_SetRelativeRotation',g.get('FlipPivot'),NewRotation=g.rot(yaw=g.arg('FlipAngle')),bSweep=False,bTeleport=True)
    for name,value in [('Progress',g.arg('FlashProgress')),('Intensity',g.arg('FlashIntensity'))]:
        g.method('MaterialInstanceDynamic','SetScalarParameterValue',g.get('FlashMID'),ParameterName=name,Value=value)
    g.layout();return compile_save(CARD)


def capture_material():
    frame='/Game/GuLiStrike/CardSystem/WarMachineTarot/Materials/MI_WarMachineFrame'
    if not unreal.EditorAssetLibrary.does_asset_exist(frame):
        require(unreal.EditorAssetLibrary.duplicate_asset('/Game/GuLiStrike/Cards/Commander/WM01/FireRate/Materials/MI_FireRate_UI_Comic_v6',frame),'Shared blank frame')
        require(unreal.EditorAssetLibrary.save_asset(frame,False),'Save shared frame')
    path=ROOT+'/Materials/M_CardCaptureUI'
    material=unreal.load_asset(path)
    if not material:
        material=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_CardCaptureUI',ROOT+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    material.set_editor_property('material_domain',unreal.MaterialDomain.MD_UI)
    material.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT)
    lib=unreal.MaterialEditingLibrary
    lib.delete_all_material_expressions(material)
    texture=lib.create_material_expression(material,unreal.MaterialExpressionTextureSampleParameter2D,-400,0)
    texture.set_editor_property('parameter_name','CardCapture')
    texture.set_editor_property('texture',unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture'))
    # SceneColor HDR stores inverse opacity; unpremultiply before Slate's translucent blend.
    inverse=lib.create_material_expression(material,unreal.MaterialExpressionOneMinus,-200,180)
    require(lib.connect_material_expressions(texture,'A',inverse,''),'Capture alpha')
    floor=lib.create_material_expression(material,unreal.MaterialExpressionMax,-40,180)
    floor.set_editor_property('const_b',.0001)
    require(lib.connect_material_expressions(inverse,'',floor,'A'),'Safe alpha')
    straight=lib.create_material_expression(material,unreal.MaterialExpressionDivide,110,0)
    require(lib.connect_material_expressions(texture,'RGB',straight,'A'),'Capture color')
    require(lib.connect_material_expressions(floor,'',straight,'B'),'Unpremultiply')
    require(lib.connect_material_property(straight,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR),'UI color')
    require(lib.connect_material_property(inverse,'',unreal.MaterialProperty.MP_OPACITY),'UI opacity')
    lib.recompile_material(material)
    require(unreal.EditorAssetLibrary.save_loaded_asset(material,False),'Capture material save')
    return path


def tables():
    names={'DT_GuLiStrikeRogueCards_Cards','DT_GuLiStrikeGameTexts_Texts'}
    runpy.run_path(str(ROOT_DIR/'Scripts/import_data_to_engine.py'),init_globals={'GULI_TABLE_FILTER':names})
    report=json.loads((ROOT_DIR/'Data/tmp_import_report.json').read_text(encoding='utf-8'))
    require(not report.get('errors') and all(e.get('imported') for e in report.get('tables',[])), 'Feature table import succeeded')
    # Independently check the final table; the importer report layout is not a runtime contract.
    for name in names:
        table=unreal.load_asset('/Game/GuLiStrike/Data/'+name)
        require(table is not None,'Imported '+name)
        rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
        expected=json.loads((ROOT_DIR/f'Data/Json/{name}.json').read_text(encoding='utf-8'))
        require(len(rows)==len(expected),'Row count '+name)
    textpath='/Game/GuLiStrike/Data/ST_GuLiStrikeGameTexts'
    strings=unreal.load_asset(textpath)
    if not strings:
        strings=unreal.AssetToolsHelpers.get_asset_tools().create_asset('ST_GuLiStrikeGameTexts','/Game/GuLiStrike/Data',unreal.StringTable,unreal.StringTableFactory())
    require(unreal.GuLiRogueCardPresentationLibrary.rebuild_game_text_string_table(strings,unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeGameTexts_Texts')),'Localized StringTable export')
    require(unreal.EditorAssetLibrary.save_loaded_asset(strings,False),'Save StringTable')
    return report


def scene_readback():
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    map_path='/Game/Maps/LVL_CommanderMassPrototype'
    current=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not current or not current.get_path_name().startswith(map_path+'.'):
        require(level.load_level(map_path),'Load actual battle map')
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    settings=world.get_world_settings()
    override=settings.get_editor_property('default_game_mode')
    default=unreal.load_class(None,'/Script/GuLiStrike.GuLiCommanderGameMode')
    if override is None:
        settings.set_editor_property('default_game_mode',default)
        override=default
    gm=unreal.get_default_object(override)
    pc_class=gm.get_editor_property('player_controller_class')
    pc=unreal.get_default_object(pc_class)
    component=pc.get_component_by_class(unreal.GuLiRogueCardPresentation)
    require(component is not None,'Commander F4 presentation component')
    actor_api=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    marker=next((a for a in actor_api.get_all_level_actors() if a.get_actor_label()=='RogueCards_F4_Entry'),None)
    if marker is None:
        marker=actor_api.spawn_actor_from_class(unreal.Note,unreal.Vector(-3200,72500,-1200))
        marker.set_actor_label('RogueCards_F4_Entry')
    marker.set_editor_property('text',ENTRY_INSTRUCTIONS)
    require(level.save_current_level(),'Save battle map')
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    relevant=[{'name':a.get_name(),'class':a.get_class().get_path_name(),'location':str(a.get_actor_location())}
              for a in actors if a==marker or any(t in a.get_class().get_name() for t in ['PlayerStart','Commander','Spawn','Stronghold'])]
    director=unreal.get_default_object(unreal.load_asset(DIRECTOR).generated_class())
    materials=[]
    for row in json.loads((ROOT_DIR/'Data/Json/DT_GuLiStrikeRogueCards_Cards.json').read_text(encoding='utf-8')):
        material=unreal.load_asset(row['FrontMaterial'])
        depth=unreal.MaterialEditingLibrary.get_material_instance_scalar_parameter_value(material,'Layers global depth')
        require(abs(depth-4)<.001,'Preserve parallax depth x4')
        require(unreal.load_class(None,row['ImplementationClass']) is not None,'Effect class '+row['Id'])
        materials.append({'id':row['Id'],'material':material.get_path_name(),'depth':depth})
    options=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrike.GuLiRogueCardSettings'))
    references={}
    for key in ['Cards','Texts','DirectorClass','CaptureMaterial','FrameMaterial']:
        value=options.get_editor_property(key)
        require(value is not None,'Setting '+key)
        references[key]=str(value)
    require(unreal.load_class(None,DIRECTOR+'.BP_CardRevealDirector_C') is not None,'Director setting')
    return {'map':map_path,'saved':True,'game_mode':override.get_path_name(),'player_controller':pc_class.get_path_name(),
            'component':component.get_class().get_path_name(),'input':'Enhanced Input F4; UMG Escape/LMB',
            'review_map':'/Game/GuLiStrike/CardSystem/WarMachineTarot/Maps/LVL_WarMachineTarotReview',
            'external_confirmation_default':director.get_editor_property('ExternalConfirmation'),
            'actors':relevant,'materials':materials,'settings_references':references,
            'blur_strength':options.get_editor_property('BlurStrength'),
            'fade_seconds':options.get_editor_property('FadeSeconds'),'runtime':'not_run','visual_acceptance':'user_pending'}


if __name__=='__main__':
    out=ROOT_DIR/'Artifacts/RogueCards';out.mkdir(parents=True,exist_ok=True)
    resume=globals().get('GULI_ROGUE_RESUME_FROM')
    report=json.loads((out/'deployment.json').read_text(encoding='utf-8')) if resume else {}
    report.pop('error',None)
    report['success']=False
    try:
        require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(),'No edits during PIE')
        require(hasattr(unreal,'GuLiRogueCardPresentationLibrary'),'Load the approved native build first')
        from migrate_card_asset_layout import migrate
        stages=[('migration',migrate),('tables',tables),('card',card_local_motion),
                ('director',confirmation_contract),('capture_material',capture_material),('scene',scene_readback)]
        require(not resume or resume in [name for name,_ in stages],'Known resume stage')
        active=not resume
        for name,operation in stages:
            active=active or name==resume
            if not active:
                continue
            report[name]=operation()
            (out/'deployment.json').write_text(json.dumps(report,ensure_ascii=False,indent=2,default=str),encoding='utf-8')
        report['success']=True
    except Exception:
        report['success']=False;report['error']=traceback.format_exc()
    (out/'deployment.json').write_text(json.dumps(report,ensure_ascii=False,indent=2,default=str),encoding='utf-8')
    print(json.dumps(report,ensure_ascii=False,default=str))
