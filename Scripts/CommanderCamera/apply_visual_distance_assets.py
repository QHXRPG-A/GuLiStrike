"""Apply distance-culling removal to the exact project assets found by the audit."""
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'ArtSource/WarMachineHover_20260930/LOD2/distance-assets.json'
LIB, NS, EM = unreal.EditorAssetLibrary, unreal.NiagaraService, unreal.NiagaraEmitterService
SOURCE = '/Game/Assets/VFX/WeaponBulletVFX/NS/VFX_FireGun_Loop'
DEST = '/Game/GuLiStrike/FX/GroundMech/NS_GroundMech_RocketJet'
report = {'success': False, 'saved': [], 'effect_types': {}, 'rocket_jet': {'source': SOURCE, 'target': DEST}}

try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(), 'PIE active'
    for path in ['/Game/GuLiStrike/FX/UnitFeedback/FXT_UnitDestruction',
                 '/Game/GuLiStrike/FX/WingmanFlight/FXT_WingmanFlight']:
        effect = unreal.load_asset(path)
        assert effect, path
        settings_array = effect.get_editor_property('system_scalability_settings')
        settings = list(settings_array.get_editor_property('settings'))
        before = [bool(s.get_editor_property('cull_by_distance')) for s in settings]
        for setting in settings:
            setting.set_editor_property('cull_by_distance', False)
        settings_array.set_editor_property('settings', settings)
        effect.set_editor_property('system_scalability_settings', settings_array)
        assert LIB.save_loaded_asset(effect, False), path
        after = [bool(s.get_editor_property('cull_by_distance')) for s in
                 effect.get_editor_property('system_scalability_settings').get_editor_property('settings')]
        assert not any(after), path
        report['effect_types'][path] = {'before': before, 'after': after}
        report['saved'].append(path)
    system = unreal.load_asset(DEST) if LIB.does_asset_exist(DEST) else LIB.duplicate_asset(SOURCE, DEST)
    assert system, DEST
    changed = []
    for emitter in NS.list_emitters(DEST):
        name = str(emitter.emitter_name)
        for module in EM.list_modules(DEST, name):
            module_name = str(module.module_name)
            if not module_name.startswith('EmitterState'):
                continue
            value = EM.get_module_input(DEST, name, module_name, 'Enable Distance Culling')
            if value.lower() == 'true':
                assert EM.set_module_input(DEST, name, module_name, 'Enable Distance Culling', 'false')
                assert EM.get_module_input(DEST, name, module_name, 'Enable Distance Culling').lower() == 'false'
                changed.append(name)
    result = NS.compile_with_results(DEST)
    assert result.success and not result.errors, str(result)
    assert LIB.save_loaded_asset(system, False), DEST
    report['saved'].append(DEST)
    report['rocket_jet']['emitters_changed'] = changed
    report['rocket_jet']['compile'] = {'success': bool(result.success), 'errors': list(result.errors), 'warnings': list(result.warnings)}
    report['rocket_jet']['source_distance_culling'] = {
        e: EM.get_module_input(SOURCE, e, 'EmitterState', 'Enable Distance Culling') for e in changed}
    assert all(v.lower() == 'true' for v in report['rocket_jet']['source_distance_culling'].values()), 'Vendor source changed'
    report['success'] = True
except Exception:
    report['error'] = traceback.format_exc()
OUT.write_text(json.dumps(report, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report))
