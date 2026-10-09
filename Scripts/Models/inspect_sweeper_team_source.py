"""Read the existing Sweeper display LODs and rigid material before color-only work."""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'ArtSource/SweeperTeamColor_v1_20261008'
OUT.mkdir(parents=True, exist_ok=True)
GEOMETRY = ROOT / 'ArtSource/ModelInterface_B_20261008/Sweeper_v1_20261008'
GEOMETRY.mkdir(parents=True, exist_ok=True)


def run():
    model = next(r for r in json.loads((ROOT / 'Data/Json/DT_GuLiStrikeModels_Models.json').read_text(encoding='utf8')) if r['Id'] == 1005)
    mesh = unreal.load_object(None, model['ResourcePath'])
    snapshot = json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(mesh))
    materials = []
    lib = unreal.MaterialEditingLibrary
    for slot in mesh.get_editor_property('static_materials'):
        material = slot.material_interface
        base = material
        while isinstance(base, unreal.MaterialInstanceConstant):
            base = base.get_editor_property('parent')
        custom = [{'name': e.get_name(), 'inputs': [str(i.get_editor_property('input_name')) for i in e.get_editor_property('inputs')], 'code': e.get_editor_property('code')}
                  for e in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if e.get_outer() == base]
        vectors = [{'name': str(e.get_editor_property('parameter_name')), 'value': list(e.get_editor_property('default_value').to_tuple()),
                    'cpd': bool(e.get_editor_property('use_custom_primitive_data')), 'index': e.get_editor_property('primitive_data_index')}
                   for e in unreal.ObjectIterator(unreal.MaterialExpressionVectorParameter) if e.get_outer() == base]
        scalars = [{'name': str(e.get_editor_property('parameter_name')), 'value': e.get_editor_property('default_value')}
                   for e in unreal.ObjectIterator(unreal.MaterialExpressionScalarParameter) if e.get_outer() == base]
        materials.append({'slot': str(slot.material_slot_name), 'material': material.get_path_name(),
                          'parent': base.get_path_name(), 'custom': custom, 'vectors': vectors, 'scalars': scalars,
                          'shading_model': str(base.get_editor_property('shading_model')), 'blend': str(base.get_editor_property('blend_mode'))})
    exports = []
    for lod in range(len(snapshot['render_lods'])):
        for rendered in (False, True):
            path = GEOMETRY / f'Original_LOD{lod}_{"render" if rendered else "source"}.json'
            result = json.loads(unreal.GuLiModelAuthoringLibrary.write_mesh_paint_geometry(mesh, lod, str(path), rendered))
            if not result['success']:
                raise RuntimeError(result['error'])
            exports.append({'lod': lod, 'rendered': rendered, 'file': str(path), 'result': result})
    report = {'model': model, 'snapshot': snapshot, 'materials': materials, 'exports': exports, 'formal_asset_writes': False}
    (OUT / 'original-source-readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    return {'success': True, 'resource': model['ResourcePath'], 'lods': len(snapshot['render_lods']),
            'materials': [{'slot': m['slot'], 'parent': m['parent'], 'custom_inputs': [e['inputs'] for e in m['custom']]} for m in materials]}


unreal.MCPythonHelper.submit_result(json.dumps(run()))
