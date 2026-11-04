"""Run after the approved native Editor build is loaded. No gameplay or pose evaluation."""
import hashlib
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
OUT=ROOT/'Artifacts/GroundUpperAim_20261104'
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
assert hasattr(unreal.GuLiGroundMechAnimationEditorLibrary,'bind_upper_body_aim'), 'Load the new native Editor build first'
baseline=json.loads((OUT/'baseline.json').read_text(encoding='utf-8'))
for path in ('Data/Json/DT_GuLiStrikeMech_Camera.json','Data/Json/DT_GuLiStrikeMech_Gait.json','Data/Json/DT_GuLiStrikeMech_GroundIK.json'):
    assert hashlib.sha256((ROOT/path).read_bytes()).hexdigest()==baseline['sha256'][path], path
source=ROOT/'Scripts/import_data_to_engine.py'
scope={'__name__':'upper_aim_import','GULI_TABLE_FILTER':{'DT_GuLiStrikeMech_Mobility'},
       'GULI_IMPORT_REPORT':str(OUT/'asset-import.json'),
       'GULI_IMPORT_PROGRESS':str(OUT/'asset-import-progress.log'),
       'GULI_SYNC_MECH_CAMERA_DEFAULTS':False}
exec(compile(source.read_text(encoding='utf-8'),str(source),'exec'),scope)
report=json.loads((OUT/'asset-import.json').read_text(encoding='utf-8'))
assert not report['errors'], report['errors']
assert len(report['tables'])==1 and report['tables'][0]['imported']
mobility=report['ground_mobility']
assert mobility['success'] and mobility['upper_aim_binding']['success']
assert not mobility['camera_defaults_updated'] and not mobility['contact_curves_rebaked']
assert not mobility['heavy_gait']['changed_gaits']
for population in (200,600):
    manifest=json.loads((ROOT/f'Config/Hybrid/RuntimeManifest-{population}.json').read_text(encoding='utf-8-sig'))
    assert int(manifest['numbers']['adapter/family/GroundPlayer/state_version'])==13
    assert manifest['strings']['ground/mobility_configuration']==mobility['configuration_fingerprint']
source=ROOT/'Scripts/GroundMech/MechaDestroyer/read_ground_mobility_assets.py'
exec(compile(source.read_text(encoding='utf-8'),str(source),'exec'),
     {'__name__':'upper_aim_asset_readback','GULI_MOBILITY_AUDIT_REPORT':str(OUT/'asset-structure.json')})
tuning=json.loads((OUT/'source-tuning.json').read_text(encoding='utf-8'))
tuning['ue_imported']=True
(OUT/'source-tuning.json').write_text(json.dumps(tuning,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'fingerprint':mobility['configuration_fingerprint'],
    'binding':mobility['upper_aim_binding'],'report':str(OUT/'asset-import.json')}))
