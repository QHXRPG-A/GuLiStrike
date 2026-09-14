"""Apply the shared destruction-wave cap; run via Scripts/ue_exec.py."""
import json
import sys
from pathlib import Path
import unreal

SCRIPTS = str(Path(unreal.Paths.project_dir()).resolve() / 'Scripts')
if SCRIPTS not in sys.path:
    sys.path.insert(0, SCRIPTS)
from unit_destruction_shockwave import ROOT, calibrate, require

report = {'systems': [], 'saved': []}
try:
    paths = [ROOT + 'NS_UnitDestruction_' + letter for letter in 'ABCDEFGHIJKLMNOPQRST']
    paths += [ROOT + 'NS_WingmanDestruction_Aerial_' + str(n) for n in (3, 4)]
    for path in paths:
        if '/NS_WingmanDestruction_Aerial_' in path:
            require(unreal.GuLiCombatEffectAuthoringLibrary.normalize_explosion_refraction_space(
                unreal.EditorAssetLibrary.load_asset(path)) == '', 'Normalize refraction space: ' + path)
        result = calibrate(path)
        compile_result = unreal.NiagaraService.compile_with_results(path)
        require(compile_result.success and compile_result.error_count == 0, str(compile_result))
        result['compile'] = {'errors': compile_result.error_count, 'warnings': compile_result.warning_count}
        obj = unreal.EditorAssetLibrary.load_asset(path)
        if result['changes'] or obj.get_outer() in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages():
            require(unreal.get_editor_subsystem(unreal.EditorAssetSubsystem).save_asset(path), 'Save ' + path)
            report['saved'].append(path)
        # Saved input readback also proves idempotence, without any second mutation.
        readback = calibrate(path)
        require(not readback['changes'], 'Calibration compounded on readback: ' + path)
        report['systems'].append(result)
    report['success'] = True
except Exception as exc:
    report['error'] = str(exc)
    raise
finally:
    output = Path(unreal.Paths.project_dir()) / 'Artifacts/UnitFeedback/destruction-shockwave-2_5.json'
    output.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(report, ensure_ascii=False))
