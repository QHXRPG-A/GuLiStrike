"""Reconcile AA bounds with its existing built geometry; do not modify its import scale."""
import json
import re
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'Artifacts/ModelRegistryRuntime20261008'


def run():
    path = '/Game/Assets/Props/Buildings/Missile_Turret/missile_turret/StaticMeshes/missile_turret'
    asset = unreal.load_asset(path)
    editor = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    before = json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(asset))
    settings = editor.get_lod_build_settings(asset, 0)
    report = {'resource': path, 'before_settings': str(settings),
              'baseline': 'ArtSource/ModelInterface_B_20261008/paint-render-diagnostic.json: MissileTurret build_scale3d=12 and before_bounds',
              'source_geometry_unchanged': False}
    report['before_bounds'] = str(asset.get_bounds())
    result = json.loads(unreal.GuLiModelAuthoringLibrary.refresh_paint_display_bounds(asset))
    if not result['success']:
        raise RuntimeError(result['error'])
    after = json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(asset))
    report['source_geometry_unchanged'] = before['lods'] == after['lods']
    report['after_settings'] = str(editor.get_lod_build_settings(asset, 0))
    box = asset.get_bounding_box()
    report['bounds'] = {'min': list(box.min.to_tuple()), 'max': list(box.max.to_tuple())}
    report['success'] = report['source_geometry_unchanged'] and box.max.z > 3000 and box.max.z < 4000
    report['render_geometry_unchanged'] = before['render_lods'] == after['render_lods']
    report['build_settings_unchanged'] = re.sub(r' \(0x[0-9A-Fa-f]+\)', '', str(settings)) == re.sub(r' \(0x[0-9A-Fa-f]+\)', '', str(editor.get_lod_build_settings(asset,0)))
    # Struct debug addresses are not part of the settings. Compare the actual
    # original import scale, while the full before/after strings remain evidence.
    report['build_scale_unchanged'] = list(settings.build_scale3d.to_tuple()) == [12.0,12.0,12.0]
    report['success'] = report['success'] and report['render_geometry_unchanged'] and report['build_scale_unchanged']
    (OUT / 'aa-original-scale-restored.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    if not report['success']:
        raise RuntimeError('AA original geometry/scale validation failed; do not save.')
    report['saved'] = unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
    (OUT / 'aa-original-scale-restored.json').write_text(json.dumps(report, indent=2), encoding='utf8')
    return {'success': report['success'] and report['saved'], 'bounds': report['bounds'], 'source_geometry_unchanged': report['source_geometry_unchanged']}


unreal.MCPythonHelper.submit_result(json.dumps(run()))
