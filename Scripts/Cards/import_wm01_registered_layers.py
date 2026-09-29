"""Import full-canvas registered layers without touching the shared atlas parent.

Editor asset/shader authoring only; no native build, PIE or map save.
Source PNGs are imported unchanged. Layer transforms are identical by contract.
"""
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'ArtSource/UI/WM01MissileCards/Production_v4'
DEST = '/Game/GuLiStrike/Cards/Commander/WM01'
SOURCE_PARENT = '/Game/GuLiStrike/CardSystem/WarMachineTarot/Materials/M_CelCardParallax_ModelComic_v9'
PARENT = '/Game/GuLiStrike/CardSystem/WarMachineTarot/Materials/M_WM01RegisteredCardLayers_v1'
SOURCE_MI = DEST + '/FireRate/Materials/MI_FireRate_ModelComic_v9'
OWNER = 'GuLi.WM01MissileCards.20260929'
LIB = unreal.EditorAssetLibrary
EDIT = unreal.MaterialEditingLibrary
SCALE = (.92, .745)
OFFSET = (0., -.0875)
DEPTHS = (.10, .04, 0., -.40, -.75, -1.10)


def owned_duplicate(source, path):
    if LIB.does_asset_exist(path):
        asset = unreal.load_asset(path)
        assert LIB.get_metadata_tag(asset, 'GuLi.Owner') == OWNER, path
    else:
        asset = LIB.duplicate_asset(source, path)
        assert asset, path
        LIB.set_metadata_tag(asset, 'GuLi.Owner', OWNER)
    return asset


def import_texture(card, layer):
    path = f'{DEST}/{card}/Textures/T_{card}_Layer{layer}_Registered_v1'
    if LIB.does_asset_exist(path):
        assert LIB.get_metadata_tag(unreal.load_asset(path), 'GuLi.Owner') == OWNER
    task = unreal.AssetImportTask()
    for key, value in {'filename': str(OUT / f'{card}_{layer}.png'),
                       'destination_path': path.rsplit('/', 1)[0],
                       'destination_name': path.rsplit('/', 1)[1],
                       'automated': True, 'replace_existing': True, 'save': False}.items():
        task.set_editor_property(key, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    texture = unreal.load_asset(path)
    assert texture and (texture.blueprint_get_size_x(), texture.blueprint_get_size_y()) == (1024, 1536)
    for key, value in {'srgb': True, 'compression_settings': unreal.TextureCompressionSettings.TC_BC7,
                       'address_x': unreal.TextureAddress.TA_CLAMP, 'address_y': unreal.TextureAddress.TA_CLAMP,
                       'never_stream': False}.items():
        texture.set_editor_property(key, value)
    LIB.set_metadata_tag(texture, 'GuLi.Owner', OWNER)
    LIB.set_metadata_tag(texture, 'GuLi.SourceCanvas', '1024x1536; full source canvas; no per-layer transforms')
    assert LIB.save_loaded_asset(texture, False)
    return texture


def make_parent(default_texture):
    material = owned_duplicate(SOURCE_PARENT, PARENT)
    nodes = [n for n in unreal.ObjectIterator(unreal.MaterialExpression) if n.get_outer() == material]
    positions = [(-1936, 48), (-1920, 352), (-1936, 576), (-1936, 800), (-1936, 1024), (-1936, 1232)]
    for i, (x, y) in enumerate(positions, 1):
        sample, = [n for n in nodes if isinstance(n, unreal.MaterialExpressionTextureSample)
                   and n.material_expression_editor_x == x and n.material_expression_editor_y == y]
        safe, = [n for n in nodes if isinstance(n, unreal.MaterialExpressionCustom)
                 and str(n.get_editor_property('desc')) == f'WM_LAYER_{i}_BoundedUV']
        safe.set_editor_property('code', '''float2 p=(UV-0.5-Offset.rg)/max(Scale.rg,float2(0.01,0.01))+0.5;
float valid=step(0.0,p.x)*step(p.x,1.0)*step(0.0,p.y)*step(p.y,1.0);
''' + ('return float3(p,valid);' if i == 6 else 'return float3(clamp(p,float2(0.0005,0.00033),float2(0.9995,0.99967)),valid);'))
        tag = f'WM_REGISTERED_LAYER_{i}_Texture'
        objects = [n for n in nodes if isinstance(n, unreal.MaterialExpressionTextureObjectParameter)
                   and str(n.get_editor_property('desc')) == tag]
        tex = objects[0] if objects else EDIT.create_material_expression(material, unreal.MaterialExpressionTextureObjectParameter, x-400, y)
        tex.set_editor_property('desc', tag)
        tex.set_editor_property('parameter_name', f'Layer {i} Map')
        tex.set_editor_property('texture', default_texture)
        assert EDIT.connect_material_expressions(tex, '', sample, 'Tex')
        if i == 6:
            far_uv = safe
    def node_by_tag(cls, tag, x, y):
        existing = [n for n in nodes if isinstance(n, cls) and str(n.get_editor_property('desc')) == tag]
        node = existing[0] if existing else EDIT.create_material_expression(material, cls, x, y)
        node.set_editor_property('desc', tag)
        return node
    outer_uv = node_by_tag(unreal.MaterialExpressionCustom, 'WM_REGISTERED_BackgroundMarginUV', -6200, 1900)
    outer_uv.set_editor_property('code', 'return (UV.xy+0.25)/1.5;')
    outer_uv.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT2)
    if 'UV' not in [str(i.get_editor_property('input_name')) for i in outer_uv.get_editor_property('inputs')]:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', 'UV')
        outer_uv.set_editor_property('inputs', [item])
    assert EDIT.connect_material_expressions(far_uv, '', outer_uv, 'UV')
    outer_tex = node_by_tag(unreal.MaterialExpressionTextureSampleParameter2D, 'WM_REGISTERED_BackgroundMarginTexture', -5900, 1900)
    outer_tex.set_editor_property('parameter_name', 'Background Margin Map')
    outer_tex.set_editor_property('texture', default_texture)
    assert EDIT.connect_material_expressions(outer_uv, '', outer_tex, 'UVs')
    composite, = [n for n in nodes if isinstance(n, unreal.MaterialExpressionCustom)
                  and str(n.get_editor_property('desc')) == 'WM_LAYER_Composite']
    # Free-flight trails sit behind the pod and the +1 symbol. This is draw order,
    # not a compensating UV translation; every source pixel keeps its coordinate.
    inputs = list(composite.get_editor_property('inputs'))
    for name in ('BackgroundOuter', 'BackgroundUV'):
        if name not in [str(item.get_editor_property('input_name')) for item in inputs]:
            item = unreal.CustomInput()
            item.set_editor_property('input_name', name)
            inputs.append(item)
    composite.set_editor_property('inputs', inputs)
    assert EDIT.connect_material_expressions(outer_tex, 'RGB', composite, 'BackgroundOuter')
    assert EDIT.connect_material_expressions(far_uv, '', composite, 'BackgroundUV')
    # The approved source occupies p in [0,1]. Its pixels always win there;
    # generated margin pixels are sampled only outside that original rectangle.
    composite.set_editor_property('code', '''float2 p=BackgroundUV.xy;
float outside=max(max(-p.x,p.x-1.0),max(-p.y,p.y-1.0));
float3 c=lerp(Color5,BackgroundOuter,saturate(outside/0.015));
c=lerp(c,Color4,Alpha4);
c=lerp(c,Color3,Alpha3);
c=lerp(c,Color0,Alpha0);
c=lerp(c,Color2,Alpha2);
return lerp(c,Color1,Alpha1);''')
    LIB.set_metadata_tag(material, 'GuLi.CanvasContract', 'Six independent textures; common full-canvas UV; no atlas cells')
    EDIT.recompile_material(material)
    assert LIB.save_loaded_asset(material, False)
    return material


report = {'success': False, 'cards': [], 'original_approval': {'MissilePod': 'Review_v2', 'RainSalvo': 'Review_v3'},
          'scan_and_bonus_flame': 'Current v4 layer 2 appearance explicitly accepted; unchanged',
          'visual_review': 'Pending editor composition and tilt checks; not user approved'}
try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    textures = {card: {i: import_texture(card, i) for i in range(1, 7) if card != 'MissilePod' or i != 1}
                for card in ('MissilePod', 'RainSalvo')}
    margins = {card: import_texture(card, '6_Margin') for card in textures}
    parent = make_parent(textures['MissilePod'][6])
    for card in ('MissilePod', 'RainSalvo'):
        path = f'{DEST}/{card}/Materials/MI_{card}_ModelComic_v1'
        mi = owned_duplicate(SOURCE_MI, path)
        mi.set_editor_property('parent', parent)
        assert unreal.MaterialService.set_instance_texture_parameter(path, 'Background Margin Map', margins[card].get_path_name())
        for index, depth in enumerate(DEPTHS, 1):
            texture = textures[card].get(index, textures[card][6])
            assert unreal.MaterialService.set_instance_texture_parameter(path, f'Layer {index} Map', texture.get_path_name())
            assert unreal.MaterialService.set_instance_scalar_parameter(path, f'Layer {index} Depth', depth)
            assert unreal.MaterialService.set_instance_scalar_parameter(path, f'Layer {index} Opacity', 0. if card == 'MissilePod' and index == 1 else 1.)
            assert unreal.MaterialService.set_instance_vector_parameter(path, f'Layer {index} Scale', *SCALE, 1, 1)
            assert unreal.MaterialService.set_instance_vector_parameter(path, f'Layer {index} Offset', *OFFSET, 0, 0)
        for key in ('BaseColor Map', 'Ability Layer Map'):
            assert unreal.MaterialService.set_instance_texture_parameter(path, key, textures[card][6].get_path_name())
        assert unreal.MaterialService.set_instance_scalar_parameter(path, 'Layers global depth', 4.)
        for key in ('Ability behind machinery', 'Foreground behind machinery'):
            assert unreal.MaterialService.set_instance_scalar_parameter(path, key, 0.)
        LIB.set_metadata_tag(mi, 'GuLi.Layer2', 'Three assembly arms' if card == 'MissilePod' else 'Blue bonus missile, opaque green +1 and accepted long flame')
        LIB.set_metadata_tag(mi, 'GuLi.VisualReview', 'Registered v4; editor review pending')
        EDIT.update_material_instance(mi)
        assert LIB.save_loaded_asset(mi, False)
        loaded = unreal.load_asset(path)
        evidence = []
        for index in range(1, 7):
            tex = EDIT.get_material_instance_texture_parameter_value(loaded, f'Layer {index} Map')
            scale = EDIT.get_material_instance_vector_parameter_value(loaded, f'Layer {index} Scale')
            offset = EDIT.get_material_instance_vector_parameter_value(loaded, f'Layer {index} Offset')
            assert abs(scale.r-SCALE[0]) < 1e-5 and abs(scale.g-SCALE[1]) < 1e-5
            assert abs(offset.r-OFFSET[0]) < 1e-5 and abs(offset.g-OFFSET[1]) < 1e-5
            evidence.append({'layer': index, 'texture': tex.get_path_name(), 'scale': [scale.r, scale.g],
                             'offset': [offset.r, offset.g], 'opacity': EDIT.get_material_instance_scalar_parameter_value(loaded, f'Layer {index} Opacity')})
        report['cards'].append({'card': card, 'material': path, 'parent': loaded.get_editor_property('parent').get_path_name(),
                                'saved_readback': True, 'layers': evidence,
                                'background_margin': EDIT.get_material_instance_texture_parameter_value(loaded, 'Background Margin Map').get_path_name(),
                                'background_margin_domain': 'Outside original UV [0,1] only; 25 percent border per side'})
    report['success'] = True
except Exception:
    report['error'] = traceback.format_exc()
(OUT / 'ue-import.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report, ensure_ascii=False))
