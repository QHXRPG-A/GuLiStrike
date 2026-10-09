"""Temporarily render the recorded old shading at the same gamma, then restore exactly."""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'Artifacts/ModelRegistryRuntime20261008'
old = json.loads((OUT / 'formal-shadow-before.json').read_text(encoding='utf8'))
restores = []
selected_models = globals().get('GULI_CAPTURE_IDS')
selected_names = None
if selected_models:
    selected_names = {r['Name'] for r in json.loads((ROOT / 'Data/Json/DT_GuLiStrikeModels_Models.json').read_text(encoding='utf8')) if r['Id'] in selected_models}
try:
    for row in old:
        if row['class'] != 'Material': continue
        if selected_names and not any('/' + name + '/' in row['path'] for name in selected_names): continue
        material = unreal.load_asset(row['path'])
        codes = {r['name']: r['code'] for r in row['custom']}
        vectors = {r['name']: r['value'] for r in row['vectors']}
        edits = []
        for expression in unreal.ObjectIterator(unreal.MaterialExpression):
            if expression.get_outer() != material: continue
            if isinstance(expression, unreal.MaterialExpressionCustom) and expression.get_name() in codes:
                edits.append((expression, 'code', expression.get_editor_property('code')))
                expression.set_editor_property('code', codes[expression.get_name()])
            elif isinstance(expression, unreal.MaterialExpressionVectorParameter):
                name = str(expression.get_editor_property('parameter_name'))
                if name in ('InkColor', 'Ink Color'):
                    edits.append((expression, 'default_value', expression.get_editor_property('default_value')))
                    expression.set_editor_property('default_value', unreal.LinearColor(*vectors[name]))
        restores.append((material, edits))
        unreal.MaterialEditingLibrary.recompile_material(material)
    GULI_CAPTURE_IMAGE_DIR = str(OUT / 'Images/BeforeShadowAdjustment')
    GULI_CAPTURE_REPORT_PATH = str(OUT / 'before-shadow-captures.json')
    exec(compile((ROOT / 'Scripts/Models/capture_runtime_paint_gallery.py').read_text(encoding='utf8'), 'capture_old_shading', 'exec'))
finally:
    for material, edits in restores:
        for expression, prop, value in edits: expression.set_editor_property(prop, value)
        unreal.MaterialEditingLibrary.recompile_material(material)
    restored_ok = bool(restores) if selected_models else len(restores) == 22
    (OUT / ('temporary-shading-restore-subset.json' if selected_models else 'temporary-shading-restore.json')).write_text(json.dumps({'parents_restored': len(restores), 'success': restored_ok, 'selected_models': selected_models}), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': restored_ok, 'parents_restored': len(restores), 'same_gamma_before': 2.2}))
