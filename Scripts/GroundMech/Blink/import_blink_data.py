"""Import only Blink's four tables after the authorized native build has been loaded."""
import json
from pathlib import Path
import unreal


def main():
    root = Path(unreal.Paths.project_dir()).resolve()
    out = root / 'ArtSource/FX/GroundMech/Blink_20261027/Shape_E_v1'
    out.mkdir(parents=True, exist_ok=True)
    if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError('Stop gameplay before importing Blink data')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world.get_path_name().split('.')[0] != '/Game/Maps/LVL_CommanderMassPrototype':
        raise RuntimeError('Open LVL_CommanderMassPrototype before importing; no tables changed')
    ability = unreal.load_class(None, '/Script/GuLiStrike.GuLiGA_Blink')
    cue = unreal.load_class(None, '/Script/GuLiStrike.GuLiGCN_GroundMechBlink')
    if not ability or not cue or not hasattr(unreal.GuLiCombatEffectAuthoringLibrary,'configure_ground_blink_ribbons'):
        raise RuntimeError('Teleport Blink native build is not loaded; no tables changed')
    scope = {'__name__': 'ground_blink_import',
             'GULI_TABLE_FILTER': {'DT_GuLiStrikeMech_Skills', 'DT_GuLiStrikeMech_Mobility', 'DT_GuLiStrikeMech_Camera', 'DT_GuLiStrikeVfx_Effects'},
             'GULI_SKIP_MECH_POST_IMPORT': True, 'GULI_SKIP_GROUND_FIRE_POST_IMPORT': True,
             'GULI_IMPORT_REPORT': str(out / 'table_import.json'),
             'GULI_IMPORT_PROGRESS': str(out / 'table_import.log')}
    source = root / 'Scripts/import_data_to_engine.py'
    exec(compile(source.read_text(encoding='utf8'), str(source), 'exec'), scope)
    imported = json.loads((out / 'table_import.json').read_text(encoding='utf8'))
    if imported['errors']:
        raise RuntimeError(imported['errors'])
    config = json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.configure_mobility_assets(False))
    if not config.get('success') or config.get('asset_mutations'):
        raise RuntimeError(config)
    manifests = []
    for population in (200, 600):
        path = root / f'Config/Hybrid/RuntimeManifest-{population}.json'
        result = json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population, str(path)))
        if not result.get('success'):
            raise RuntimeError(result)
        value = json.loads(path.read_text(encoding='utf-8-sig'))
        if value['strings']['ground/mobility_configuration'] != config['configuration_fingerprint']:
            raise RuntimeError('Blink configuration/manifest fingerprint mismatch')
        manifests.append({'population': population, 'configuration_hash': value['config_hash']})
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(
        unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Skills')))
    blink = next(row for row in rows if row['Name'] == 'Blink')
    cameras=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(
        unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Camera')))
    if blink['BlinkDistance']!=3200 or blink['BlinkRibbonCount']!=96 or abs(blink['BlinkConcealSeconds']-.22)>.0001 or abs(blink['BlinkRecoverySeconds']-.3)>.0001 or any(
            r['BlinkFovBoostDegrees']!=15 or r['BlinkFovRiseSeconds']!=.08 or r['BlinkFovReturnSeconds']!=.25 or r['BlinkHoldSeconds']!=0 for r in cameras):
        raise RuntimeError('Teleport data readback does not match the authored values')
    for row in cameras:
        values=(500,160,11) if row['Name']=='Free' else (350,110,4)
        if tuple(row[k] for k in ('BlinkArmLength','BlinkSideOffsetCm','BlinkPitchOffsetDegrees'))!=values:
            raise RuntimeError('Blink flash framing readback mismatch')
    report = {'success': True, 'tables': imported['tables'], 'blink': blink,
              'cameras': cameras,'model_material_overrides':False,
              'configuration_fingerprint': config['configuration_fingerprint'], 'manifests': manifests,
              'ability_class': ability.get_path_name(), 'cue_class': cue.get_path_name(),
              'PIE_started': False, 'visual_approval': 'pending_player'}
    (out / 'data_readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'table_count':len(imported['tables']),
        'configuration_fingerprint':report['configuration_fingerprint'],'recovery_seconds':blink['BlinkRecoverySeconds'],
        'ribbons':blink['BlinkRibbonCount'],'shape_vfx_id':blink['BlinkShapeVfxId'],'report':str(out/'data_readback.json')}))


main()
