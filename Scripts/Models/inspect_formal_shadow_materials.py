"""Read actual formal body shaders and defaults before a user-requested tone change."""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'Artifacts/ModelRegistryRuntime20261008'
promotion = json.loads((ROOT / 'ArtSource/ModelInterface_B_20261008/formal-promotion.json').read_text(encoding='utf8'))
records = []
for entry in promotion['moved_materials']:
    path = entry if isinstance(entry, str) else entry.get('target', entry.get('formal', entry.get('to', '')))
    asset = unreal.load_asset(path)
    if not isinstance(asset, (unreal.Material, unreal.MaterialInstanceConstant)):
        continue
    record = {'path': asset.get_path_name(), 'class': asset.get_class().get_name()}
    if isinstance(asset, unreal.Material):
        record['custom'] = [{'name': x.get_name(), 'code': x.get_editor_property('code')}
                            for x in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if x.get_outer() == asset]
        record['scalars'] = [{'name': str(x.get_editor_property('parameter_name')), 'value': x.get_editor_property('default_value')}
                             for x in unreal.ObjectIterator(unreal.MaterialExpressionScalarParameter) if x.get_outer() == asset]
        record['vectors'] = [{'name': str(x.get_editor_property('parameter_name')), 'value': list(x.get_editor_property('default_value').to_tuple())}
                             for x in unreal.ObjectIterator(unreal.MaterialExpressionVectorParameter) if x.get_outer() == asset]
    else:
        record['parent'] = asset.get_editor_property('parent').get_path_name() if asset.get_editor_property('parent') else None
        record['scalars'] = [{'name': str(x.parameter_info.name), 'value': x.parameter_value} for x in asset.scalar_parameter_values]
        record['vectors'] = [{'name': str(x.parameter_info.name), 'value': list(x.parameter_value.to_tuple())} for x in asset.vector_parameter_values]
    records.append(record)
(OUT / 'formal-shadow-before.json').write_text(json.dumps(records, ensure_ascii=False, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'materials': len(records), 'report': str(OUT / 'formal-shadow-before.json')}))
