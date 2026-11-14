"""Save review positions around the normal mining flow; never start gameplay."""
import json
import math
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'ArtSource/QuadrupedMiner_10k_v5'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'QuadrupedMinerV5Review'
FOLDER = 'GuLiStrike/Review/QuadrupedMiner_v5'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
world = editor.get_editor_world()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert not editor.get_game_world() and world.get_path_name().split('.')[0] == MAP
assert json.loads((OUT / 'ue-readback.json').read_text(encoding='utf8'))['success']
assert json.loads((OUT / 'render-mesh-readback.json').read_text(encoding='utf8'))['success']
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
definition = unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap')
economy = unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy')
layout_hash = definition.calculate_layout_hash()
assert layout_hash == definition.get_editor_property('layout_hash')
assert str(definition.get_editor_property('map_package')) == MAP
assert economy.get_editor_property('mining_vehicle_unit_type_id') == 3
assert economy.get_editor_property('initial_mining_vehicles_per_team') == 8
anchors = definition.get_editor_property('spawn_anchors')
territories = definition.get_editor_property('territories')
clusters = definition.get_editor_property('clusters')


def vector(value): return list(value.to_tuple())


def record(actor):
    return dict(name=actor.get_name(), label=actor.get_actor_label(), cls=actor.get_class().get_path_name(),
                location=vector(actor.get_actor_location()), rotation=list(actor.get_actor_rotation().to_tuple()),
                scale=vector(actor.get_actor_scale3d()), editor_only=actor.get_editor_property('is_editor_only_actor'))


existing = actors.get_all_level_actors()
owned = {a.get_actor_label(): a for a in existing if TAG in [str(t) for t in a.tags]}
original = {a.get_path_name(): record(a) for a in existing if a not in owned.values()}
made = []; related = []


def place(label, cls, position):
    assert not any(a.get_actor_label() == label and a not in owned.values() for a in existing)
    actor = owned.get(label) or actors.spawn_actor_from_class(cls, position)
    assert isinstance(actor, cls)
    actor.modify(); actor.set_actor_label(label); actor.set_actor_location(position, False, False)
    actor.set_folder_path(FOLDER); actor.set_editor_property('tags', [TAG])
    actor.set_editor_property('is_editor_only_actor', True)
    made.append(actor)
    return actor


with unreal.ScopedEditorTransaction('Deliver quadruped miner through normal mining flow'):
    for side, team, direction in [('Red', unreal.GuLiTeam.RED, 1), ('Blue', unreal.GuLiTeam.BLUE, -1)]:
        assembly = anchors.get_editor_property(side.lower() + '_assembly')
        factory = anchors.get_editor_property(side.lower() + '_factory')
        eligible = [c for c in clusters if territories[c.territory_index].initial_owner in [team, unreal.GuLiTeam.UNASSIGNED]]
        cluster = min(eligible, key=lambda c: math.dist(vector(c.center), vector(assembly)))
        entry = place('QuadrupedV5_' + side + '_Entry', unreal.Note, assembly + unreal.Vector(0,0,350))
        entry.set_editor_property('text',
            '四足采矿车 v5：通过本地图正常指挥官入口审核。\n'
            '双方各 8 辆矿车，UnitTypeId 3；移动、采矿、返厂均使用正式资源。\n'
            '移动后按 S 停车，再向可采矿簇下令；观察抬足、前伸、停车混合及采卸循环。\n'
            '用红/蓝双方视角分别查看：己方灯蓝，敌方灯红，中立熄灯；外壳保持中立色。\n'
            '拉近/拉远观察 LOD，停止、死亡、快速通道时光束应关闭。\n'
            '本轮仅完成编译及数据检查，运行观感由玩家审核。')
        ore = place('QuadrupedV5_' + side + '_Ore', unreal.Note, cluster.center + unreal.Vector(0,0,350))
        ore.set_editor_property('text', f'附近可采矿簇 {cluster.get_editor_property("cluster_id")}，领地 {territories[cluster.territory_index].territory_id}。\n'
            '使用真实矿车采矿指令；中央光束起点跟随机体动作，终点应落在正在开采的真实矿石。\n'
            '满载后自动返厂，卸货后继续采矿；本标记不创建矿石、不改变经济或任务规则。')
        for key, position in [('Assembly', assembly), ('Factory', factory)]:
            place('QuadrupedV5_' + side + '_' + key, unreal.TargetPoint, position)
        focus = assembly + unreal.Vector(0,0,150)
        location = focus + unreal.Vector(-2200, -direction*3600, 2900)
        camera = place('QuadrupedV5_' + side + '_Camera', unreal.CameraActor, location)
        camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location, focus), False)
        camera.get_components_by_class(unreal.CameraComponent)[0].set_field_of_view(55)
        related.append(dict(side=side, assembly=vector(assembly), factory=vector(factory),
                            ore_cluster=cluster.get_editor_property('cluster_id'), ore_center=vector(cluster.center),
                            ore_territory=str(territories[cluster.territory_index].territory_id),
                            initial_owner=str(territories[cluster.territory_index].initial_owner), nodes=cluster.node_count))

current = {a.get_path_name(): record(a) for a in actors.get_all_level_actors() if TAG not in [str(t) for t in a.tags]}
assert current == original and definition.calculate_layout_hash() == layout_hash
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
# Reload the saved map, then read only this delivery's entities and entry settings.
made = []; existing = []; owned = {}; actor = entry = ore = camera = None; world = None
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(MAP)
world = editor.get_editor_world()
saved = [a for a in actors.get_all_level_actors() if TAG in [str(t) for t in a.tags]]
assert len(saved) == 10 and all(a.get_editor_property('is_editor_only_actor') for a in saved)
entities = [dict(record(a), folder=str(a.get_folder_path()),
                 note=a.get_editor_property('text') if isinstance(a, unreal.Note) else '') for a in saved]
assert {a['label'] for a in entities} == {'QuadrupedV5_' + side + '_' + kind for side in ['Red','Blue'] for kind in ['Entry','Ore','Assembly','Factory','Camera']}
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
report = dict(success=True, saved_and_reloaded=True, map=MAP, entities=entities, entry_points=related,
              game_mode=world.get_world_settings().get_editor_property('default_game_mode').get_path_name(),
              resource_map=definition.get_path_name(), layout_hash=layout_hash, economy=economy.get_path_name(),
              miners_per_team=8, unit_type_id=3, existing_actors_unchanged=len(original), runtime_started=False,
              model_id=1003, mesh_model_id=8017, player_visual_acceptance='pending')
(OUT / 'scene-delivery.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
red_camera = next(a for a in saved if a.get_actor_label() == 'QuadrupedV5_Red_Camera')
unreal.EditorLevelLibrary.set_level_viewport_camera_info(red_camera.get_actor_location(),red_camera.get_actor_rotation())
actors.set_selected_level_actors([next(a for a in saved if a.get_actor_label() == 'QuadrupedV5_Red_Entry')])
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True, saved_and_reloaded=True, map=MAP,
    owned_entities=len(saved), existing_actors_unchanged=len(original), entry_points=related, runtime_started=False)))
