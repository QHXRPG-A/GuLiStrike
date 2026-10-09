"""User-requested shading revision: readable dark regions, unchanged palette and mesh."""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'Artifacts/ModelRegistryRuntime20261008'
lib = unreal.MaterialEditingLibrary
import sys
sys.path.insert(0,str(ROOT / "Scripts/Models"))
from tone_style import GRAY, revise

report = {'authorization': '这些黑块太深了，再浅一些', 'palette_unchanged': True,
          'geometry_unchanged': True, 'parents': [], 'errors': [],
          'shading': {'shadow_band': .62, 'middle_band': .82, 'highlight_band': 1.,
                      'dark_surface_fill_linear': .03, 'internal_ink_hex': '#2C3735',
                      'mask_strength_factor': .78, 'normal_edge_strength_factor': .65}}
for path in unreal.EditorAssetLibrary.list_assets('/Game/GuLiStrike/Models/TeamColor_v1', True, False):
    parent = unreal.load_asset(path)
    if not isinstance(parent, unreal.Material): continue
    stages = [e for e in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if e.get_outer() == parent]
    if not stages: continue
    for e in stages:
        e.set_editor_property('code', revise(e.get_editor_property('code')))
    for e in unreal.ObjectIterator(unreal.MaterialExpressionVectorParameter):
        if e.get_outer() == parent and str(e.get_editor_property('parameter_name')) in ('InkColor', 'Ink Color'):
            e.set_editor_property('default_value', unreal.LinearColor(*GRAY))
    lib.recompile_material(parent)
    if not unreal.EditorAssetLibrary.save_loaded_asset(parent, False): raise RuntimeError('Save failed: ' + path)
    unreal.EditorAssetLibrary.set_metadata_tag(parent, 'GuLi.ShadowRevision', 'DarkSurfaceFill_v1')
    report['parents'].append({'path': parent.get_path_name(), 'tone_stages': len(stages)})
report['success'] = len(report['parents']) == 22 and not report['errors']
(OUT / 'dark-surface-adjustment.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': report['success'], 'parents': len(report['parents']), 'errors': report['errors']}))
