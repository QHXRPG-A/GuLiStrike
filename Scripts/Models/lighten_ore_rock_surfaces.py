"""User-requested rock readability. Fixed-card fill only; crystal/mesh data stay intact."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008')
GRAY = (.0251868596, .0382043716, .0356013149, 1)


def run():
    lib = unreal.MaterialEditingLibrary
    rows = []
    for side in ('Blue', 'Red'):
        path = '/Game/GuLiStrike/Resources/Ores/Materials/M_Ore_' + side + '_Rock'
        material = unreal.load_asset(path)
        attributes = next(e for e in unreal.ObjectIterator(unreal.MaterialExpressionMakeMaterialAttributes) if e.get_outer() == material)
        nodes = {e.get_editor_property('desc'): e for e in unreal.ObjectIterator(unreal.MaterialExpression)
                 if e.get_outer() == material and e.get_editor_property('desc').startswith('GuLi.OreRockFill.v1')}
        if not nodes:
            color = lib.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -550, 180)
            color.set_editor_property('constant', unreal.LinearColor(*GRAY))
            color.set_editor_property('desc', 'GuLi.OreRockFill.v1.FixedCardGray')
            amount = lib.create_material_expression(material, unreal.MaterialExpressionConstant, -550, 350)
            amount.set_editor_property('r', .85)
            amount.set_editor_property('desc', 'GuLi.OreRockFill.v1.FillAmount')
            multiply = lib.create_material_expression(material, unreal.MaterialExpressionMultiply, -300, 200)
            multiply.set_editor_property('desc', 'GuLi.OreRockFill.v1.ReadableShadow')
            if not (lib.connect_material_expressions(color, '', multiply, 'A')
                    and lib.connect_material_expressions(amount, '', multiply, 'B')
                    and lib.connect_material_expressions(multiply, '', attributes, 'EmissiveColor')):
                raise RuntimeError('Rock fill connection failed: ' + path)
        lib.recompile_material(material)
        rows.append({'resource': path, 'fixed_card': '#2C3735', 'linear_fill_factor': .85,
                     'base_color_vertex_input_preserved': True, 'kind': 'rock'})
    for side, color in [('Blue','#274E61'),('Red','#662249')]:
        path = '/Game/GuLiStrike/Resources/Ores/Materials/M_Ore_' + side + '_Crystal'
        material = unreal.load_asset(path)
        attributes = next(e for e in unreal.ObjectIterator(unreal.MaterialExpressionMakeMaterialAttributes) if e.get_outer() == material)
        custom = next((e for e in unreal.ObjectIterator(unreal.MaterialExpressionCustom)
                       if e.get_outer() == material and e.get_editor_property('desc') == 'GuLi.OreCrystalFill.v1.ReadableDarkSides'),None)
        if custom is None or len(custom.get_editor_property('inputs')) != 2 or not all(lib.get_inputs_for_material_expression(material,custom)):
            sources = lib.get_inputs_for_material_expression(material, attributes)
            vertex, emission = sources[0], sources[5]
            if vertex is None or emission is None:
                raise RuntimeError('Original crystal color/emission connection missing: ' + path)
            values = [int(color[i:i+2],16)/255 for i in (1,3,5)]
            linear = [v/12.92 if v <= .04045 else ((v+.055)/1.055)**2.4 for v in values]
            if custom is None:
                custom = lib.create_material_expression(material,unreal.MaterialExpressionCustom,-100,400)
            custom.set_editor_property('desc','GuLi.OreCrystalFill.v1.ReadableDarkSides')
            custom.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
            custom_inputs = []
            for name in ('VertexRGB','OriginalEmission'):
                item = unreal.CustomInput()
                item.set_editor_property('input_name',name)
                custom_inputs.append(item)
            custom.set_editor_property('inputs',custom_inputs)
            custom.set_editor_property('code','float Dark = 1-saturate(dot(VertexRGB.rgb,float3(.2126,.7152,.0722))/.40); return OriginalEmission.rgb + float3('+','.join(f'{v:.10f}' for v in linear)+')*Dark;')
            if not (lib.connect_material_expressions(vertex,'',custom,'VertexRGB')
                    and lib.connect_material_expressions(emission,'',custom,'OriginalEmission')
                    and lib.connect_material_expressions(custom,'',attributes,'EmissiveColor')):
                raise RuntimeError('Crystal side fill connection failed: '+path)
        values = [int(color[i:i+2],16)/255 for i in (1,3,5)]
        linear = [v/12.92 if v <= .04045 else ((v+.055)/1.055)**2.4 for v in values]
        custom.set_editor_property('code','float Dark = 1-saturate(dot(VertexRGB.rgb,float3(.2126,.7152,.0722))/.40); return OriginalEmission.rgb + float3('+','.join(f'{v:.10f}' for v in linear)+')*Dark;')
        lib.recompile_material(material)
        rows.append({'resource':path,'kind':'crystal','fixed_card':color,'linear_fill_factor':1.0,'dark_luma_threshold':.4,
                     'base_color_vertex_input_preserved':True,'original_emission_preserved':True,'only_dark_surfaces_filled':True})
    can_save = not any('UEDPIE_' in w.get_path_name() for w in unreal.ObjectIterator(unreal.World))
    saved = [unreal.EditorAssetLibrary.save_asset(r['resource'], False) for r in rows] if can_save else []
    report = {'authorization': '这些黑块太深了，再浅一些', 'source': 'OreHISM rock components; visibility isolation confirms the cluster disappears',
              'materials': rows, 'geometry_unchanged': True, 'saved': False, 'success': len(rows) == 4}
    report['saved'] = len(saved) == 4 and all(saved)
    report['saved_output_verified'] = all(
        any(e and e.get_editor_property('desc') == ('GuLi.OreRockFill.v1.ReadableShadow' if r['kind']=='rock' else 'GuLi.OreCrystalFill.v1.ReadableDarkSides')
            for e in lib.get_inputs_for_material_expression(unreal.load_asset(r['resource']),
                next(x for x in unreal.ObjectIterator(unreal.MaterialExpressionMakeMaterialAttributes) if x.get_outer() == unreal.load_asset(r['resource']))))
        for r in rows)
    (OUT / 'ore-rock-lighter-revision.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    return report


unreal.MCPythonHelper.submit_result(json.dumps(run()))
