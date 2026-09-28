"""Save editor-only review anchors in the existing Commander prototype map.

Uses the map's normal runtime population and resources. Does not start gameplay.
"""
import json
import runpy
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/CommanderStateTree/HierarchyV2'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'CommanderStateTreeReview'
FOLDER = 'GuLiStrike/Review/CommanderStateTree'
report = {'success': False, 'map': MAP, 'gameplay_started': False}


def vector(value):
    return list(value.to_tuple())


def actor_record(actor):
    return {
        'name': actor.get_name(), 'label': actor.get_actor_label(),
        'class': actor.get_class().get_path_name(),
        'location': vector(actor.get_actor_location()),
        'rotation': list(actor.get_actor_rotation().to_tuple()),
        'scale': vector(actor.get_actor_scale3d()),
    }


def run():
    inspect = runpy.run_path(str(ROOT / 'Scripts/inspect_commander_state_trees.py'))['inspect']
    trees = inspect()
    assert trees['success'] and all(a.get('hierarchy_version') == '2' and not a['dirty'] for a in trees['assets']), \
        'Compile, save, and read back all three hierarchy-v2 assets before scene delivery.'
    report['hierarchy_assets'] = [{k: a[k] for k in ('asset', 'editor_hash', 'hierarchy_version', 'compiled_matches_editor')}
                                  for a in trees['assets']]
    assert hasattr(unreal.GuLiMiningVehicleManager, 'get_cluster_slot_poses'), 'Load the new native build first.'
    assert hasattr(unreal.GuLiResourceFactoryActor, 'get_unload_points'), 'Load the four-point factory build first.'
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assert editor.get_game_world() is None, 'An active gameplay world blocks scene authoring.'
    world = editor.get_editor_world()
    assert world.get_path_name() == MAP + '.LVL_CommanderMassPrototype'
    actors_api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = actors_api.get_all_level_actors()
    owned = [a for a in actors if TAG in [str(t) for t in a.tags]]
    original = {a.get_path_name(): actor_record(a) for a in actors if a not in owned}
    assert not any(a.get_class().get_name() == 'GuLiCommanderDeploymentPoint' for a in actors), \
        'Review notes assume the existing default 500-soldier population.'

    definition = unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap')
    economy = unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy')
    assert definition and economy
    assert str(definition.get_editor_property('map_package')) == MAP
    layout_hash = definition.get_editor_property('layout_hash')
    assert definition.calculate_layout_hash() == layout_hash
    anchors = definition.get_editor_property('spawn_anchors')
    factory = anchors.get_editor_property('red_factory')
    assembly = anchors.get_editor_property('red_assembly')
    next_outpost = next(a for a in actors if a.get_actor_label() == 'Outpost_R2C4')
    table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers')
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    bindings = {r['Id']: r['StateTreeAsset'] for r in rows}
    assert sorted(bindings) == [1, 2, 3, 4]
    assert all(unreal.load_asset(path) is not None for path in bindings.values())
    assert not unreal.EditorAssetLibrary.does_asset_exist('/Game/GuLiStrike/Data/DT_GuLiStrikeSpecialTasks_Tasks')
    assert economy.get_editor_property('mining_vehicle_unit_type_id') == 3
    assert economy.get_editor_property('construction_vehicle_unit_type_id') == 4
    miners_per_team = economy.get_editor_property('initial_mining_vehicles_per_team')
    builders_per_team = economy.get_editor_property('initial_construction_vehicles_per_team')
    factory_defaults = unreal.get_default_object(unreal.GuLiResourceFactoryActor)
    unload_points = sorted(
        [c for c in factory_defaults.get_components_by_class(unreal.SceneComponent)
         if c.get_name().startswith('UnloadPoint')], key=lambda c: c.get_name())
    assert len(unload_points) == 4, 'Every native factory must create four unload point components.'
    point_positions = [vector(c.get_relative_transform().translation) for c in unload_points]
    assert len({tuple(p) for p in point_positions}) == 4
    report['factory_unload_points'] = [
        {'component': c.get_name(), 'local_location': p}
        for c, p in zip(unload_points, point_positions)]

    specs = [
        ('StateTreeReview_Entry', assembly + unreal.Vector(0, 0, 500),
         '三棵分层 StateTree 审核入口\n'
         f'使用本地图正常的指挥官入口。双方各250名士兵、各{miners_per_team}辆矿车和{builders_per_team}辆建造车由权威端创建。\n'
         '3棵共享树资产；每个单位独立运行实例。采矿、建造、推进说明点在同一Outliner文件夹。\n'
         '移动、采集、卸货、施工和占领应持续停留在阶段中；失败检查局部恢复路径。\n'
         '打开对应 StateTree 并选中服务端单位实例观察当前状态；本轮运行效果由玩家验证。'),
        ('StateTreeReview_MiningFactory', factory + unreal.Vector(0, -1400, 500),
         '电磁矿车 / ST_CommanderMiner\n'
         '矿车优先当前据点的我方或中立矿簇；矿簇提供0–8个矿位。预占 → 排队寻路 → 落位采矿 → 回厂卸货。\n'
         '工程车互相忽略碰撞和避让。矿厂内部4个卸货点不占用、不预约；直接寻路到点卸货，卸完直接接下一任务。\n'
         '点不可达则换点，四点都失败则换厂；自动任务按原规则退避，人工任务按原规则报告失败。\n'
         '采矿循环和独立返厂共用返货分支。覆盖目标丢失、有货返厂、卸货点失败、S停止保留未卸货物及Shift交接。\n'
         '矿位被占用时5Hz等待；跨据点按步行距离估算选择快速通道。\n'
         '树资产：/Game/GuLiStrike/Commander/Behavior/ST_CommanderMiner'),
        ('StateTreeReview_Construction', assembly + unreal.Vector(1800, 0, 500),
         '建造车 / ST_CommanderBuilder\n'
         f'红方初始{builders_per_team}辆建造车位于集合区。使用正常B建造入口，在合法区域放置可负担的建筑。\n'
         '待建建筑保持碰撞并提供0–4个建造位；最多四车同时施工。选目的地 → 前往 → 100cm内抵达抢位 → 持续施工。\n'
         '途中不占位；本地优先，途中出现本地工地可退单换单；开始施工后不自动换单。\n'
         '抢位失败排除该位置1秒再取单；路径不可达保留现有位置/导航版本排除规则。停工保留进度。\n'
         '树资产：/Game/GuLiStrike/Commander/Behavior/ST_CommanderBuilder'),
        ('StateTreeReview_StrongholdAdvance', next_outpost.get_actor_location() + unreal.Vector(0, 0, 500),
         '扫荡者、战争机器 / ST_CommanderMass\n'
         '双方初始军队使用烘焙集合点；出生授予一次据点推进。R2C4是红方前方的一个可观察据点，实际目标由原拓扑规则选择。\n'
         '观察选目标 → 持续推进 → 持续占领 → 阶段交接。不可达排除重选，无目标按原节拍等待。\n'
         'Shift追加在据点阶段交接；人工接管后不重新授予InitialOnce。手动移动与S使用原入口。\n'
         '树资产：/Game/GuLiStrike/Commander/Behavior/ST_CommanderMass'),
        ('StateTreeReview_Control', assembly + unreal.Vector(-1800, 0, 500),
         '公共控制与请求回执审核\n'
         '选中单位按S：持续停止；运输途中先安全退出，落地后保持停止。\n'
         '连续替换移动：新路未接受前沿旧路移动；再次改令后旧寻路回执不能抢回控制。\n'
         '通过现有运输/外部控制入口暂停，再释放：回到适当业务阶段，避免重复寻路、重复占位。\n'
         'Shift追加：按采卸循环、施工完成或据点阶段交接；观察任务只收尾一次。\n'
         '父状态优先关系：安全退出 → 持续停止 → 替换移动 → 正常业务。\n'
         '新树资产与Xmind仅证明结构；以上效果需要玩家实际操作确认。'),
    ]
    notes = []
    with unreal.ScopedEditorTransaction('Author Commander StateTree review anchors'):
        for label, location, text in specs:
            matches = [a for a in owned if a.get_actor_label() == label]
            assert len(matches) <= 1, 'Duplicate owned review anchor: ' + label
            note = matches[0] if matches else actors_api.spawn_actor_from_class(unreal.Note, location)
            assert isinstance(note, unreal.Note)
            note.modify()
            note.set_actor_label(label)
            note.set_actor_location(location, False, False)
            note.set_folder_path(FOLDER)
            note.set_editor_property('tags', [TAG])
            note.set_editor_property('is_editor_only_actor', True)
            note.set_editor_property('text', text)
            notes.append(note)

    current = {a.get_path_name(): actor_record(a) for a in actors_api.get_all_level_actors()
               if TAG not in [str(t) for t in a.tags]}
    assert original == current, 'An existing actor changed during review authoring.'
    assert definition.calculate_layout_hash() == layout_hash
    report['saved'] = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    assert report['saved']
    report['anchors'] = [dict(actor_record(a), folder=str(a.get_folder_path()),
                              editor_only=a.get_editor_property('is_editor_only_actor'),
                              text=a.get_editor_property('text')) for a in notes]
    report['existing_actors_unchanged'] = len(original)
    report['game_mode'] = world.get_world_settings().get_editor_property('default_game_mode').get_path_name()
    report['soldiers_table'] = table.get_path_name()
    report['state_tree_bindings'] = bindings
    report['resource_map'] = definition.get_path_name()
    report['resource_layout_hash'] = layout_hash
    report['economy_config'] = economy.get_path_name()
    report['territories'] = len(definition.get_editor_property('territories'))
    report['ore_clusters'] = len(definition.get_editor_property('clusters'))
    report['ore_nodes'] = len(definition.get_editor_property('nodes'))
    report['related_actors'] = [actor_record(a) for a in actors if a.get_class().get_name() in
                               ['PlayerStart', 'RecastNavMesh', 'NavMeshBoundsVolume'] or
                               a.get_actor_label() in ['Outpost_R1C4', 'Outpost_R2C4', 'Outpost_R7C4']]
    engineering_count = 2 * (miners_per_team + builders_per_team)
    report['initial_instances_from_configuration'] = {'mass': 500, 'actor': engineering_count, 'total': 500 + engineering_count}
    report['runtime_verified'] = False
    report['success'] = True
    actors_api.set_selected_level_actors([notes[0]])


try:
    run()
except Exception:
    report['error'] = traceback.format_exc()
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'scene-delivery.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({k: report[k] for k in ['success', 'saved', 'error'] if k in report}))
