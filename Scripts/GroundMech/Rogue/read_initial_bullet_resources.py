"""Read the exact requested vendor resources through the editor, without mutation."""
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/GroundAmmo_20261108'
SOURCES = ['/Game/_VFXResources/Niagara_System/NS_Explosion_19',
           '/Game/Assets/VFX/WeaponBulletVFX/NS/VFX_Missile']

def main():
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    ns, em = unreal.NiagaraService, unreal.NiagaraEmitterService
    report = {'success': True, 'systems': {}, 'logic_assets': [], 'mutations': False, 'PIE': False}
    for path in SOURCES:
        asset = unreal.load_asset(path)
        assert isinstance(asset, unreal.NiagaraSystem), path
        report['systems'][path] = {
            'class': asset.get_class().get_path_name(),
            'parameters': [str(p) for p in ns.list_parameters(path)],
            'emitters': [{'name': str(e.emitter_name),
                          'properties': str(em.get_emitter_properties(path, str(e.emitter_name))),
                          'modules': [str(m) for m in em.list_modules(path, str(e.emitter_name))],
                          'renderers': [str(em.get_renderer_details(path, str(e.emitter_name), r.renderer_index))
                                        for r in em.list_renderers(path, str(e.emitter_name))]}
                         for e in ns.list_emitters(path)]}
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    for row in registry.get_assets_by_path('/Game/Assets/Missile_Cluster', recursive=True):
        report['logic_assets'].append({'path': str(row.package_name), 'class': str(row.asset_class_path)})
    assert report['logic_assets'], 'Specified Missile_Cluster reference is missing'
    report['editor_world'] = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name()
    return report

try:
    report = main()
except Exception:
    report = {'success': False, 'error': traceback.format_exc(), 'mutations': False}
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'source_resources.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': report['success'], 'error': report.get('error'),
    'report': str(OUT / 'source_resources.json')}, ensure_ascii=False))
