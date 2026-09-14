"""Create pooled Aerial 3/4 copies, retaining every layer and capping wave extent."""
import json
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(unreal.Paths.project_dir()).resolve() / 'Scripts'))
from unit_destruction_shockwave import calibrate

DEST = '/Game/GuLiStrike/FX/UnitFeedback'
SOURCE = '/Game/AllExplosions/Niagara/Aerial'
OUT = Path('D:/UE5.7/test1/Artifacts/UnitFeedback/wingman-aerial-assets.json')
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
NS = unreal.NiagaraService
report = {'systems': [], 'saved': []}


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def emitters(path):
    return [{'name': str(x.emitter_name), 'enabled': x.is_enabled}
            for x in NS.list_emitters(path)]


def main():
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(), 'Stop PIE before authoring')
    effect = require(ASSETS.load_asset(DEST + '/FXT_UnitDestruction'), 'Missing shared effect type')
    for number in (3, 4):
        src = SOURCE + '/NS_Explosion_Aerial_' + str(number)
        dst = DEST + '/NS_WingmanDestruction_Aerial_' + str(number)
        require(NS.summarize(src), 'Missing source: ' + src)
        original_emitters = emitters(src)
        require(all(any(e['name'] == name and e['enabled'] for e in original_emitters)
                    for name in ('smoke_shockwave', 'refr_mesh')), 'Missing source shockwave/refraction')
        obj = require(ASSETS.load_asset(dst) if ASSETS.does_asset_exist(dst)
                      else ASSETS.duplicate_asset(src, dst), 'Create ' + dst)
        changed = False
        for prop, value in [('effect_type', effect), ('max_pool_size', 16), ('pool_prime_size', 0)]:
            if obj.get_editor_property(prop) != value:
                obj.set_editor_property(prop, value)
                changed = True
        # Preserve the source's complete graph and enable flags, including its shockwave and refraction.
        require(emitters(dst) == original_emitters, 'Emitter topology changed: ' + dst)
        require(unreal.GuLiCombatEffectAuthoringLibrary.normalize_explosion_refraction_space(obj) == '',
                'Normalize aerial refraction space')
        wave_calibration = calibrate(dst)
        changed = changed or bool(wave_calibration['changes'])
        result = NS.compile_with_results(dst)
        require(result.success and result.error_count == 0, 'Niagara compile failed: ' + str(result))
        if changed or obj.get_outer() in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages():
            require(ASSETS.save_loaded_asset(obj), 'Save failed: ' + dst)
            report['saved'].append(dst)
        report['systems'].append({
            'source': src, 'path': dst, 'compile': str(result),
            'shockwave': wave_calibration,
            'emitters': emitters(dst),
            'renderers': {e['name']: [str(r) for r in unreal.NiagaraEmitterService.list_renderers(dst, e['name'])]
                          for e in original_emitters},
            'parameters': [str(p) for p in NS.list_parameters(dst)],
            'effect_type': obj.get_editor_property('effect_type').get_path_name(),
            'pool_size': obj.get_editor_property('max_pool_size'),
            'pool_prime_size': obj.get_editor_property('pool_prime_size'),
            'source_emitters_unchanged': emitters(src) == original_emitters})
    report['success'] = True


try:
    main()
except Exception as exc:
    report['error'] = str(exc)
    raise
finally:
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(report, ensure_ascii=False))
