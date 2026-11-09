"""Read saved bindings and material graph structure without starting a game or reading images."""
import json
from pathlib import Path
import unreal

out = Path(r'D:/UE5.7/PerformanceCaptures/GuLiStrike/20261106-ground-ui-feedback')
manifest = json.loads((out/'ground-mask-materials.json').read_text(encoding='utf-8'))
settings = unreal.get_default_object(unreal.load_class(None, '/Script/GuLiStrike.GuLiGroundSceneUISettings'))
rows = []
special = []
for row in manifest['bindings']:
    source = unreal.load_asset(row['source'])
    mask = unreal.load_asset(row['mask'])
    assert source and mask
    assert unreal.EditorAssetLibrary.get_metadata_tag(mask, 'GuLi.MaskSource') == source.get_path_name()
    if isinstance(source, unreal.Material):
        inputs = {}
        for prop in (unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET, unreal.MaterialProperty.MP_OPACITY_MASK,
                     unreal.MaterialProperty.MP_OPACITY):
            a = unreal.MaterialEditingLibrary.get_material_property_input_node(source, prop)
            b = unreal.MaterialEditingLibrary.get_material_property_input_node(mask, prop)
            assert (a is None) == (b is None)
            if a:
                assert a.get_name() == b.get_name()
            inputs[str(prop)] = a.get_name() if a else None
        assert source.get_editor_property('used_with_skeletal_mesh') == mask.get_editor_property('used_with_skeletal_mesh')
        rows.append({'source': row['source'], 'mask': row['mask'], 'preserved_inputs': inputs,
                     'skeletal_usage': mask.get_editor_property('used_with_skeletal_mesh')})
        for node in unreal.ObjectIterator(unreal.MaterialExpression):
            if node.get_outer() == source and node.get_class().get_name() in (
                    'MaterialExpressionPerInstanceRandom', 'MaterialExpressionActorPositionWS'):
                special.append({'source': row['source'], 'expression': node.get_class().get_name()})
    else:
        source_parent = source.get_editor_property('parent').get_path_name()
        expected = next(x['mask'] for x in manifest['bindings'] if x['source'] == source_parent)
        assert mask.get_editor_property('parent').get_path_name() == expected

bindings = settings.get_editor_property('MaskMaterials')
assert len(bindings) == len(manifest['bindings'])
result = {'success': True, 'gameplay_started': False, 'pie': len(unreal.EditorLevelLibrary.get_pie_worlds(True)),
          'fade_start_cm': settings.get_editor_property('FadeStartCm'),
          'fade_end_cm': settings.get_editor_property('FadeEndCm'),
          'config_binding_count': len(bindings), 'outline': str(settings.get_editor_property('OutlineMaterial')),
          'source_inputs': rows, 'special_position_or_random_inputs': special,
          'scene_ui_class_loaded': unreal.load_class(None, '/Script/GuLiStrike.GuLiSceneUIWidget') is not None}
assert result['fade_start_cm'] == 35000 and result['fade_end_cm'] == 50000
(out/'asset-readback-final.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({k: v for k, v in result.items() if k != 'source_inputs'}, ensure_ascii=False))
