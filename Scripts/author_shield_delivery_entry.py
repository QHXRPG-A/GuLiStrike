"""Save the shield delivery and Pioneer barracks player entry on the prototype map."""
import json
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir()).resolve()
assert not unreal.EditorLevelLibrary.get_pie_worlds(True)
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name().split('.')[0] == '/Game/Maps/LVL_CommanderMassPrototype'
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
tag = unreal.Name('ShieldDelivery20261112')
instructions = '''护盾外围交矿与先驱号兵营
Play选择指挥官。双方开局都有护盾发生器和矿车；占领据点后按正式配置提供护盾发生器与先驱号兵营。
蓝方开局护盾在(-300,-70200)，据点兵营约在(-7950,-69750)。坐标单位厘米。
框选有货矿车，点击交矿或右键己方护盾，检查矿车在护盾外围停靠、卸货，随后原矿按FIFO每秒2单位加工入队伍资源。
选择先驱号兵营，检查生产的先驱号在内部出生并走出建筑。门、天花板和坡道不提供模型碰撞。其他单位向兵营另一侧移动时仍按建筑圆形边界寻路和侧绕。
只对自产先驱号的出营阶段忽略出生兵营边界，出营后恢复正常避障，其他建筑和单位仍参与避障。
正式源表：GuLiStrikeBuildings.xlsx（定义5护盾、6兵营），GuLiStrikeAvoidance.xlsx（BuildingCircles），GuLiStrikeModels.xlsx（模型2006）。
使用正式对局、生产和资源账本，不需要启用临时玩法开关。出生受现有生产周期、人口和资源条件约束。
手感与视觉效果由玩家检查。'''

def authored(label, cls, position):
    found = [a for a in actors.get_all_level_actors() if a.get_actor_label() == label]
    if found:
        actor = found[0]
        assert isinstance(actor, cls) and actor.actor_has_tag(tag)
    else:
        actor = actors.spawn_actor_from_class(cls, unreal.Vector(*position))
        actor.set_actor_label(label)
        actor.set_editor_property('tags', [tag])
    actor.modify()
    actor.set_editor_property('is_editor_only_actor', True)
    actor.set_folder_path('Review/ShieldDelivery20261112')
    actor.set_actor_location(unreal.Vector(*position), False, True)
    return actor

with unreal.ScopedEditorTransaction('Shield delivery and Pioneer barracks entry'):
    note = authored('ShieldDelivery_Entry', unreal.Note, (-300, -70200, 1200))
    note.set_editor_property('text', instructions)
    barracks = authored('PioneerBarracks_Entry', unreal.Note, (-7950, -69750, 1200))
    barracks.set_editor_property('text', '先驱号兵营：正常建筑圆阻挡寻路和避障；自产先驱号在内部出生，沿朝向出营，出营时忽略出生兵营边界。主体、门、天花板与坡道没有模型碰撞。正常Play选择指挥官，在此据点检查出兵并尝试命令其他单位绕过兵营。')
    camera = authored('ShieldDelivery_View', unreal.CameraActor, (-17200, -69750, 10000))
    camera.set_actor_rotation(unreal.Rotator(-40, 0, 0), False)
    for old in actors.get_all_level_actors():
        if isinstance(old, unreal.Note) and '矿厂' in old.get_editor_property('text'):
            text = old.get_editor_property('text')
            if old.get_actor_label() == 'TeleportGround_TransitExit':
                text = text.replace('矿厂坡道与卸货地板仍供正常地面行驶使用；落地后继续观察普通移动及矿车入厂卸货。',
                                    '先驱号兵营保留圆形通行边界，模型部件不提供移动碰撞；矿车在护盾发生器外围交矿。')
            elif old.get_actor_label() == 'ConstructionReview_Type6':
                text = text.replace('矿厂', '先驱号兵营')
            elif old.get_actor_label() == 'Grid300_Review':
                text = '\n'.join(line for line in text.split('\n') if '矿厂' not in line)
            old.modify()
            old.set_editor_property('text', text)
        if isinstance(old, unreal.Note) and old.get_actor_label() == 'CircleAvoidanceReview_Entry':
            text = old.get_editor_property('text')
            text = text.replace('工厂恢复原卸货点，没有新增单车入口预约。', '矿车在护盾发生器外围交矿并加工；先驱号兵营使用正常圆形通行边界，自产先驱号在内部出生后出营。')
            text = text.replace('采矿回厂施工', '采矿交矿施工')
            old.modify()
            old.set_editor_property('text', text)

assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
entities = []
for actor in (note, barracks, camera):
    v = actor.get_actor_location()
    entities.append({'label': actor.get_actor_label(), 'path': actor.get_path_name(),
                     'class': actor.get_class().get_name(), 'position': [v.x, v.y, v.z],
                     'editor_only': actor.get_editor_property('is_editor_only_actor')})
assert note.get_editor_property('text') == instructions
assert camera.get_auto_activate_player_index() == -1
review = unreal.GameplayStatics.get_actor_of_class(world, unreal.GuLiHybridNetworkReviewActor)
assert review
result = {'success': True, 'saved': True, 'map': world.get_path_name(), 'entities': entities,
          'runtime_entry': review.get_path_name(), 'instructions': instructions,
          'visual_verification': 'reserved for player'}
(root / 'Artifacts/ShieldDelivery20261112/player-entry.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=False))
