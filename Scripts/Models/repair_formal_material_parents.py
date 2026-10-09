"""Restore explicit formal parent references after moving the paint library."""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'Artifacts/ModelRegistryRuntime20261008'
baseline = json.loads((ROOT / 'Data/Models/migration-baseline-20261008.json').read_text(encoding='utf8'))
lib = unreal.MaterialEditingLibrary
source_by_name = {}
for model in baseline['models']:
    for slot in model['slots']:
        path = slot.get('material')
        if path:
            source_by_name.setdefault(path.rsplit('.', 1)[-1], set()).add(path)

report = {'instances': [], 'fallback_linework': [], 'errors': []}
for path in unreal.EditorAssetLibrary.list_assets('/Game/GuLiStrike/Models/TeamColor_v1', True, False):
    instance = unreal.load_asset(path)
    if not isinstance(instance, unreal.MaterialInstanceConstant):
        continue
    candidates = set()
    for original_path in source_by_name.get(instance.get_name(), []):
        source = unreal.load_object(None, original_path)
        if not isinstance(source, unreal.MaterialInstanceConstant):
            continue
        original = source.get_editor_property('parent')
        while isinstance(original, unreal.MaterialInstanceConstant):
            original = original.get_editor_property('parent')
        if original:
            candidates.add(instance.get_path_name().split('/Instances/')[0] + '/Parents/' + original.get_name())
    if len(candidates) != 1:
        raise RuntimeError('Ambiguous original parent for ' + path + ': ' + str(candidates))
    parent = unreal.load_asset(next(iter(candidates)))
    if not isinstance(parent, unreal.Material):
        raise RuntimeError('Missing formal parent for ' + path)
    lib.set_material_instance_parent(instance, parent)
    lib.update_material_instance(instance)
    if instance.get_editor_property('parent') != parent:
        raise RuntimeError('Parent readback mismatch: ' + path)
    if not unreal.EditorAssetLibrary.save_loaded_asset(instance, False):
        raise RuntimeError('Instance save failed: ' + path)
    report['instances'].append({'instance': path, 'parent': parent.get_path_name()})

# Original ink/VAT graphs stay untouched. The three legacy opaque PBR families
# receive restrained normal-discontinuity ink in the existing three-tone stage.
for path in unreal.EditorAssetLibrary.list_assets('/Game/GuLiStrike/Models/TeamColor_v1', True, False):
    parent = unreal.load_asset(path)
    if not isinstance(parent, unreal.Material):
        continue
    changed = False
    for expression in unreal.ObjectIterator(unreal.MaterialExpressionCustom):
        if expression.get_outer() != parent:
            continue
        code = expression.get_editor_property('code')
        if code.endswith('return Palette*band;') and 'float band=d<' in code:
            expression.set_editor_property('code', code.replace(
                'return Palette*band;',
                'float edge=saturate((max(length(ddx(normalize(N))),length(ddy(normalize(N))))-.25)*4);'
                'return lerp(Palette*band,float3(.025,.030,.028),edge);'))
            changed = True
    if changed:
        lib.recompile_material(parent)
        if not unreal.EditorAssetLibrary.save_loaded_asset(parent, False):
            raise RuntimeError('Parent save failed: ' + path)
        report['fallback_linework'].append(path)
report['success'] = bool(report['instances']) and not report['errors']
(OUT / 'formal-parent-repair.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': report['success'], 'instances': len(report['instances']),
                                              'fallback_linework': len(report['fallback_linework'])}))
