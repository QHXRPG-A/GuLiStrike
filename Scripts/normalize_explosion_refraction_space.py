"""Prevent the project aerial/Big17 refraction meshes from scaling twice."""
import json
from pathlib import Path
import unreal

paths = [
    '/Game/GuLiStrike/FX/UnitFeedback/NS_WingmanDestruction_Aerial_3',
    '/Game/GuLiStrike/FX/UnitFeedback/NS_WingmanDestruction_Aerial_4',
    '/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanGroundShockwave_Big_17',
]
report = {'systems': [], 'saved': []}
try:
    if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError('Stop PIE before authoring')
    for path in paths:
        obj = unreal.EditorAssetLibrary.load_asset(path)
        before = unreal.NiagaraEmitterService.get_emitter_properties(path, 'refr_mesh').local_space
        error = unreal.GuLiCombatEffectAuthoringLibrary.normalize_explosion_refraction_space(obj)
        if error != '':
            raise RuntimeError(str(error))
        compiled = unreal.NiagaraService.compile_with_results(path)
        after = unreal.NiagaraEmitterService.get_emitter_properties(path, 'refr_mesh').local_space
        if after or not compiled.success or compiled.error_count:
            raise RuntimeError('Refraction space/compile failed: ' + path)
        if before:
            if not unreal.get_editor_subsystem(unreal.EditorAssetSubsystem).save_loaded_asset(obj):
                raise RuntimeError('Save failed: ' + path)
            report['saved'].append(path)
        report['systems'].append({'path': path, 'local_space_before': before, 'local_space_after': after,
                                  'compile_errors': compiled.error_count, 'compile_warnings': compiled.warning_count})
    report['success'] = True
except Exception as exc:
    report['error'] = str(exc)
    raise
finally:
    output = Path(unreal.Paths.project_dir()) / 'Artifacts/UnitFeedback/explosion-refraction-space.json'
    output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(report))
