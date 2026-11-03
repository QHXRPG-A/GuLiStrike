"""Update the existing player's note; preserve certified placements and map geometry."""
import json
from pathlib import Path
import unreal
assert not unreal.WidgetService.is_pie_running()
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith('/Game/Maps/LVL_CommanderMassPrototype.')
sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors={a.get_actor_label():a for a in sub.get_all_level_actors()}
names=['GroundHUD_v1_PlayerEntry','GroundHUD_ShieldTarget','GroundHUD_KillTarget','GroundRogue_Entry','GroundMobility_Entry']
assert all(n in actors for n in names)
note=actors[names[0]]
note.set_editor_property('text','Ground HUD 动画：选择 Ground。LMB 有效射击亮边/准星回弹，RMB 瞄准渐变，Space 喷气亮边与耗尽落地恢复就绪，Shift 闪现亮边/CD圆环/就绪。M 地图、I 详情：开0.18秒/关0.12秒；F4 正面直选/取消重开。首次入场与重生0.45秒，返回战斗不重播。护盾靶400生命/500护盾，击杀靶20生命；仅有效伤害/吸收/击杀触发X，死亡靶请重开本局。普通刷新5Hz，事件帧末同步。代码已加载；助手不读图，观感与完整战斗/跨局由玩家验收。资源/证据：ArtSource/UI/GroundMech/HUD_Motion_20261102。')
assert unreal.EditorLoadingAndSavingUtils.save_map(world,'/Game/Maps/LVL_CommanderMassPrototype')
rows=[]
for n in names:
    a=actors[n];p=a.get_actor_location();row={'label':n,'path':a.get_path_name(),'class':a.get_class().get_name(),'location':[p.x,p.y,p.z]}
    if isinstance(a,unreal.GuLiCommanderDeploymentPoint):
        for key in ['team','unit_type_id','rows','columns','initial_max_health_override','initial_shield','allow_automatic_fire','use_in_population_manifest']:
            row[key]=str(a.get_editor_property(key))
    rows.append(row)
bp=unreal.load_asset('/Game/GuLiStrike/GroundMech/BP_GroundMech_Light');cdo=unreal.get_default_object(bp.generated_class())
assert cdo.get_editor_property('model_id')==4001
mesh=cdo.get_component_by_class(unreal.SkeletalMeshComponent).get_editor_property('skeletal_mesh_asset')
assert mesh.get_path_name().startswith('/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2/')
table=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeGroundHUD_Elements')
data=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
motion=[r for r in data if r['Name'].startswith('Motion')]
assert len(motion)==36
for r in motion:
    if r['Texture'] and r['Texture']!='None':assert unreal.load_asset(r['Texture']),r['Texture']
gate=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
assert gate.get_editor_property('success'),gate.get_editor_property('message')
settings=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrikeEditor.GuLiLocalPIESettings'))
settings.set_editor_property('bPromptForRole',True)
play=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.LevelEditorPlaySettings'))
play.set_editor_property('NewWindowWidth',3840);play.set_editor_property('NewWindowHeight',2037)
dirty=list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())+list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())
report={'saved':True,'entities':rows,'note':note.get_editor_property('text'),'model':4001,'mesh':mesh.get_path_name(),'motion_rows':len(motion),'table_rows':len(data),'resource_gate':str(gate.get_editor_property('message')),'dirty':[p.get_name() for p in dirty],'pie':False,'visual_review':'not_performed'}
assert not dirty
path=Path('D:/UE5.7/test1/data/GroundHUDValidation/Motion20261102/scene-readback.json')
path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report))
