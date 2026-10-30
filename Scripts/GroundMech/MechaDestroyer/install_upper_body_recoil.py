"""Import only Gait, bind the new input, and refresh manifests after native reload.

Set UPPER_BODY_RECOIL_STAGE='preflight' for a read-only native schema check.
Never rebuilds the animation graph, evaluates poses, or starts gameplay.
"""
import json
import math
import runpy
from pathlib import Path

import unreal


ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Artifacts/GroundUpperBodyRecoil20261028'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
BASE = '/Game/GuLiStrike/GroundMech/MechaDestroyer_A4B2'
FIELDS = ('UpperBodyRecoilLeftAsset', 'UpperBodyRecoilRightAsset',
          'UpperBodyRecoilScale', 'UpperBodyRecoilMaxDegrees')


def rows(table):
    return json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))


def preflight():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if level.is_in_play_in_editor():
        raise RuntimeError('Stop gameplay before asset authoring; this script never stops PIE')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world.get_path_name().split('.')[0] != MAP:
        raise RuntimeError(f'Open {MAP} before importing this tuning')
    row = unreal.GuLiStrikeMechGaitRow()
    for field in ('upper_body_recoil_left_asset', 'upper_body_recoil_right_asset',
                  'upper_body_recoil_scale', 'upper_body_recoil_max_degrees'):
        row.get_editor_property(field)
    if not hasattr(unreal.GuLiGroundMechAnimationEditorLibrary, 'bind_upper_body_recoil'):
        raise RuntimeError('New native code is not loaded; compile/reload only with user authorization')
    expected = next(r for r in json.loads((ROOT / 'Data/Json/DT_GuLiStrikeMech_Gait.json').read_text(encoding='utf8')) if r['Name'] == 'Shoot')
    bp = unreal.load_asset(BASE + '/Animations/ABP_GroundMech_A4B2')
    if not bp or not hasattr(unreal.get_default_object(bp.generated_class()), 'upper_body_recoil_rotation'):
        raise RuntimeError('Animation Blueprint does not inherit the new native input')
    for field in FIELDS[:2]:
        sequence = unreal.load_asset(expected[field])
        if not isinstance(sequence, unreal.AnimSequence) or sequence.get_skeleton() != bp.get_editor_property('target_skeleton'):
            raise RuntimeError(f'Incompatible reference: {field}')
    return bp, expected


def install():
    bp, expected = preflight()
    OUT.mkdir(parents=True, exist_ok=True)
    lib = unreal.EditorAssetLibrary
    table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Gait')
    before = {r['Name']: {k: v for k, v in r.items() if k not in FIELDS} for r in rows(table)}
    import_report = OUT / 'gait_import.json'
    runpy.run_path(str(ROOT / 'Scripts/import_data_to_engine.py'), init_globals={
        'GULI_TABLE_FILTER': {'DT_GuLiStrikeMech_Gait'},
        'GULI_SKIP_MECH_POST_IMPORT': True,
        'GULI_IMPORT_REPORT': str(import_report),
        'GULI_IMPORT_PROGRESS': str(OUT / 'gait_import.log'),
    })
    imported = json.loads(import_report.read_text(encoding='utf8'))
    if imported['errors'] or len(imported['tables']) != 1 or not imported['tables'][0].get('imported'):
        raise RuntimeError(imported)
    after = rows(table)
    preserved = {r['Name']: {k: v for k, v in r.items() if k not in FIELDS} for r in after}
    if preserved != before:
        raise RuntimeError('Gait import changed fields outside upper-body recoil')
    shoot = next(r for r in after if r['Name'] == 'Shoot')
    for field in FIELDS:
        actual, wanted = shoot[field], expected[field]
        if not (math.isclose(actual, wanted, abs_tol=1e-6) if isinstance(wanted, (float, int)) else actual == wanted):
            raise RuntimeError(f'Saved Gait/Shoot mismatch: {field}')
    graph = json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.bind_upper_body_recoil(bp))
    if not graph.get('success') or not graph.get('existing_links_preserved'):
        raise RuntimeError(graph)
    if graph['nodes_after'] - graph['nodes_before'] not in (0, 1):
        raise RuntimeError('Binding added more than its one variable getter')
    if not lib.save_loaded_asset(bp, False) or not lib.save_loaded_asset(table, False):
        raise RuntimeError('Could not save the animation Blueprint or Gait table')
    configuration = json.loads(unreal.GuLiGroundMechAnimationEditorLibrary.configure_mobility_assets(False))
    if not configuration.get('success'):
        raise RuntimeError(configuration)
    manifests = []
    for population in (200, 600):
        path = ROOT / f'Config/Hybrid/RuntimeManifest-{population}.json'
        result = json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population, str(path)))
        if not result.get('success'):
            raise RuntimeError(result)
        manifest = json.loads(path.read_text(encoding='utf-8-sig'))
        if manifest['strings']['ground/mobility_configuration'] != configuration['configuration_fingerprint']:
            raise RuntimeError(f'Manifest fingerprint mismatch: {population}')
        manifests.append({'population': population, 'config_hash': manifest['config_hash'],
                          'configuration_fingerprint': configuration['configuration_fingerprint']})
    report = {'success': True, 'graph': graph, 'shoot': {k: shoot[k] for k in FIELDS},
              'other_gait_fields_preserved': True, 'configuration': configuration['configuration_fingerprint'],
              'manifests': manifests, 'play_started': False, 'pose_evaluation': 'not_run', 'visual_acceptance': 'pending_user'}
    (OUT / 'asset_install.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    return report


if globals().get('UPPER_BODY_RECOIL_STAGE') == 'preflight':
    preflight()
    unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'native_schema': 'loaded', 'asset_mutations': False}))
else:
    unreal.MCPythonHelper.submit_result(json.dumps(install(), ensure_ascii=False))
