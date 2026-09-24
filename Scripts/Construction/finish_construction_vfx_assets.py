"""Import the VFX table, wire only the builder, and save/read back the existing sample map."""
import contextlib
import gc
import io
import json
import sys
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'outputs/construction-vfx'
OUT.mkdir(parents=True, exist_ok=True)


def run():
    assert not unreal.WidgetService.is_pie_running()
    report = json.loads((OUT / 'asset-build.json').read_text(encoding='utf-8'))
    assert report.get('success'), 'Finish the three Niagara systems before binding them'
    ids = json.loads((OUT / 'registered-ids.json').read_text(encoding='utf-8'))
    scope = {'__name__': '__main__', 'GULI_TABLE_FILTER': {'DT_GuLiStrikeVfx_Effects'}}
    with contextlib.redirect_stdout(io.StringIO()):
        exec(compile((ROOT/'Scripts/import_data_to_engine.py').read_text(encoding='utf-8'), 'import_data_to_engine.py', 'exec'), scope)
    imported = json.loads((ROOT/'Data/tmp_import_report.json').read_text(encoding='utf-8'))
    assert all(row.get('imported') for row in imported['tables']), imported
    sys.path.insert(0, str(ROOT/'Scripts/Vfx'))
    from configure_registry_bindings import configure_blueprints
    builder = '/Game/GuLiStrike/Vehicles/ConstructionVehicle/BP_ConstructionVehicle'
    bindings = configure_blueprints([builder])
    for side in ['L', 'R']:
        assert unreal.BlueprintService.set_component_property(builder, 'MiningLaser_'+side, 'bAutoActivate', 'false')
        assert unreal.BlueprintService.set_component_property(builder, 'MiningLaser_'+side, 'bVisible', 'false')
        assert unreal.BlueprintService.set_component_property(builder, 'MiningLaser_'+side, 'bVisibleInRayTracing', 'false')
        assert unreal.BlueprintService.set_component_property(builder, 'MiningLaser_'+side, 'CastShadow', 'false')
    compiled = unreal.BlueprintService.compile_blueprint(builder)
    assert compiled.success, str(compiled)
    assert unreal.EditorAssetLibrary.save_asset(builder, only_if_is_dirty=False)
    # Update the already delivered review anchor; the normal economy, vehicles and B placement remain the entry.
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0] == '/Game/Maps/LVL_CommanderMassPrototype'
    api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    note = next(a for a in api.get_all_level_actors() if a.get_actor_label() == 'ConstructionReview_Progress')
    note.modify()
    note.set_editor_property('text',
        '矿厂特效样板：B选择矿厂放置，蓝色虚影等待施工；真实建造车到位后开始升起。\n'
        '屋檐蓝白电火花；淡青绿色光柱与三角粒子每1秒呼吸。每辆车左右两束紫色激光沿近侧基底轮廓2秒往返。\n'
        '单车、四车分别观察；选中一车按S，其他车保持施工；全部S时火花与激光停止，光柱保留。\n'
        '重新下达施工后恢复；完工底部UpgradeGlow只播放一次，并叠加0.65秒蓝白边缘发光。\n'
        'R旋转四向、换目标、移动、运输、死亡和建筑销毁检查无残留；矿车仍使用绿色激光。')
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects')
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    new_rows = [r for r in rows if int(r['Id']) in ids.values()]
    assert len(new_rows) == len(ids)
    for row in new_rows:
        assert unreal.load_asset(row['ResourcePath']) is not None, row
    miner = '/Game/GuLiStrike/Vehicles/ElectromagneticMiner/BP_MiningVehicle_TransporterLvl2'
    settings = unreal.get_default_object(unreal.load_class(None, '/Script/GuLiStrike.GuLiBuildingConstructionSettings'))
    assert settings.get_editor_property('LaserVfxId') == ids['BuildingConstructionLaser']
    return {'success': True, 'map': world.get_path_name(), 'map_saved': True,
            'review_actor': note.get_actor_label(), 'review_location': list(note.get_actor_location().to_tuple()),
            'review_text': note.get_editor_property('text'), 'vfx_rows': new_rows, 'builder_bindings': bindings,
            'miner_binding': unreal.BlueprintService.get_component_property(miner, 'VfxRegistryBindings', 'Bindings'),
            'game_mode': str(world.get_world_settings().get_editor_property('default_game_mode')),
            'automatic_construction': settings.get_editor_property('bEnabled')}


try:
    result = run()
except Exception:
    result = {'success': False, 'error': traceback.format_exc()}
(OUT/'integration-assets.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({k:v for k,v in result.items() if k in ['success','map','error','map_saved']}))
gc.collect()
