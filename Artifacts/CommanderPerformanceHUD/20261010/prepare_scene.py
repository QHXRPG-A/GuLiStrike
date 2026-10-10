"""Apply only the new public text rows and prepare the existing HUD map entry; never start PIE."""
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/CommanderPerformanceHUD/20261010'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TABLE = '/Game/GuLiStrike/Data/DT_GuLiStrikeGameTexts_Texts'
LABEL = 'NetworkHUD_Entry'

report = {'success': False, 'compile': 'not_run', 'runtime': 'not_run'}
try:
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert not level.is_in_play_in_editor(), 'Preserve the current PIE session'
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world and world.get_path_name().startswith(MAP + '.'), 'Use the existing Commander map'
    mode = world.get_world_settings().get_editor_property('default_game_mode')
    assert mode is not None
    default = unreal.get_default_object(mode)
    hud = default.get_editor_property('hud_class')
    controller = default.get_editor_property('player_controller_class')
    assert hud.get_path_name() == '/Script/GuLiStrike.GuLiCommanderHUD'
    assert controller.get_path_name() == '/Script/GuLiStrike.GuLiCommanderPlayerController'
    assert TABLE not in {p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    source = {r['Name']: r for r in json.loads((ROOT / 'data/Json/DT_GuLiStrikeGameTexts_Texts.json').read_text(encoding='utf8'))}
    new_ids = set(json.loads((OUT / 'text-source.json').read_text(encoding='utf8'))['text_ids'])
    table = unreal.load_asset(TABLE)
    before = {r['Name']: r for r in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))}
    assert set(before) <= set(source), 'Keep unrelated rows'
    assert all(before[key] == source[key] for key in before), 'Preserve existing text values'
    assert set(source) - set(before) <= new_ids, 'Only add this HUD extension'
    for key in source.keys() - before.keys():
        row = {k: v for k, v in source[key].items() if k != 'Name'}
        # AddRow retains the UObject held by GuLiGameText after an ended PIE session.
        assert unreal.DataTableService.add_row(TABLE, key, json.dumps(row, ensure_ascii=False)), key
    actual = {r['Name']: r for r in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))}
    assert actual == source, 'Full text table readback'
    assert unreal.EditorAssetLibrary.save_loaded_asset(table, False), 'Save text asset'
    report['text_table'] = {'path': TABLE, 'rows': len(actual), 'added': len(actual) - len(before),
                            'full_readback_matches_source': True, 'saved': True, 'method': 'DataTableService.AddRow in place'}
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    report['preexisting_dirty_maps'] = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
    all_actors = actors.get_all_level_actors()
    anchor = next(a for a in all_actors if a.get_actor_label() == 'StateTreeReview_Entry')
    marker = next((a for a in all_actors if a.get_actor_label() == LABEL), None)
    position = anchor.get_actor_location() + unreal.Vector(1500.0, 0.0, 0.0)
    if marker is None:
        marker = actors.spawn_actor_from_class(unreal.Note, position)
        assert marker is not None
        marker.set_actor_label(LABEL)
    instructions = ('网络HUD验证（2026-10-10）：先编译并加载本次原生代码，再按原流程进入指挥官客户端。\n'
                    '左上角每秒显示本客户端连接的收/发/合计KB/s、包/丢包速率、本端发送额度、可靠待确认块数和发送欠账。1KB=1000B。\n'
                    '飞行事件：解码条数/s、应用载荷KB/s、平均估计年龄；它不是实际播放数量，估龄可能含服务器时钟误差。\n'
                    '同进程PIE额外显示当前客户端对应服务端的下行额度、可靠待确认数、欠账、飞行应用待发条数和队首事件年龄。跨进程为不可用。\n'
                    '对比安静场景与大量单位移动/交火：关注收发速率是否接近发送额度，以及飞行待发条数/队首年龄是否持续增长。\n'
                    '单机/本地服务端视口不冒充客户端流量；未完成采样或没有新年龄样本显示--。框选、移动、S停止与切角色应保持正常。\n'
                    '本次仅静态检查和场景准备；编译、运行显示与玩家验收单独记录。')
    marker.set_editor_property('text', instructions)
    assert level.save_current_level(), 'Save existing map including its current edits'
    marker = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == LABEL)
    assert marker.get_editor_property('text') == instructions
    assert marker.get_class().get_path_name() == '/Script/Engine.Note'
    assert MAP not in {p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()}
    location = marker.get_actor_location()
    report['scene'] = {'map': world.get_path_name(), 'entry_actor': marker.get_path_name(), 'label': LABEL,
                       'location': [location.x, location.y, location.z], 'instructions': instructions,
                       'game_mode': mode.get_path_name(), 'controller': controller.get_path_name(),
                       'hud': hud.get_path_name(), 'saved': True, 'entity_readback': True}
    report['success'] = True
except Exception:
    report['error'] = traceback.format_exc()
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'scene-readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
print(json.dumps({'success': report['success'], 'text_table': report.get('text_table'),
                  'scene': report.get('scene'), 'error': report.get('error')}, ensure_ascii=False))
