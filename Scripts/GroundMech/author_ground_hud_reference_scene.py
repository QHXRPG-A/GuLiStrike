"""Update only the saved Ground HUD entry note, then read back the real scene and PIE gate."""
import json
from pathlib import Path
import unreal

assert not unreal.WidgetService.is_pie_running(),'Stop PIE before saving the delivery map'
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().startswith('/Game/Maps/LVL_CommanderMassPrototype.')
actors={a.get_actor_label():a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()}
names=['GroundHUD_v1_PlayerEntry','GroundHUD_ShieldTarget','GroundHUD_KillTarget','GroundRogue_Entry','GroundMobility_Entry','GroundRogue_Wall','GroundRogue_Friendly']
assert all(n in actors for n in names),'Preserve the existing Ground validation area'
entry=actors[names[0]];entry.modify()
entry.set_editor_property('text','Ground HUD 参考复刻版 2026-11-02：选择红队 Ground（4001/A4B2）。已接入三张参考图对应的驾驶舱框、生命/喷气/技能槽、真实高度场等高线、平面地图和立体选牌。左键射击、右键瞄准、Space喷气、Shift闪现；M地图、I详情、F4正面单击选牌、Esc关闭。普通数据5Hz、关键事件同步。HUDuiPRO静态素材，不播放整套媒体动画。护盾靶400生命/500护盾；击杀靶20生命。构建及结构化检查完成，视觉、完整操作和性能由玩家验收。')
assert unreal.EditorLoadingAndSavingUtils.save_map(world,'/Game/Maps/LVL_CommanderMassPrototype')
gate=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
assert gate.get_editor_property('success'),str(gate.get_editor_property('message'))
report={'map':world.get_path_name(),'saved':True,'note':entry.get_editor_property('text'),'source_hash':gate.get_editor_property('source_hash'),'resource_gate':True,'entities':[]}
for name in names:
    actor=actors[name];row={'label':name,'path':actor.get_path_name(),'class':actor.get_class().get_name(),'position':str(actor.get_actor_location())}
    if isinstance(actor,unreal.GuLiCommanderDeploymentPoint):
        row.update({key:str(actor.get_editor_property(key)) for key in ['unit_type_id','team','initial_max_health_override','initial_shield','start_idle','allow_automatic_fire']})
    report['entities'].append(row)
root=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
(root/'data/GroundHUDValidation/Reference20261102/scene.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report))
