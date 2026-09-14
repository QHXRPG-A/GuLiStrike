"""Author the project-owned, per-camera enemy silhouette post process."""
import json
import os
import unreal

PATH = '/Game/GuLiStrike/FX/TeamOutline/M_TeamOutline'
REPORT = 'D:/UE5.7/test1/TestResults/WORK-20260912-003/outline-material.json'
MAT = unreal.MaterialEditingLibrary

def main():
    existing = unreal.load_asset(PATH) if unreal.EditorAssetLibrary.does_asset_exist(PATH) else None
    if existing:
        # Read back the previous graph before the authored graph is regenerated.
        previous_graph = unreal.MaterialNodeService.export_material_graph(PATH)
    else:
        unreal.MaterialService.create_material('M_TeamOutline', '/Game/GuLiStrike/FX/TeamOutline')
        previous_graph = ''
    material = unreal.load_asset(PATH)
    material.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property('blendable_location', unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    MAT.delete_all_material_expressions(material)
    scene = MAT.create_material_expression(material, unreal.MaterialExpressionSceneTexture, -700, -200)
    scene.set_editor_property('scene_texture_id', unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    stencil = MAT.create_material_expression(material, unreal.MaterialExpressionSceneTexture, -700, 0)
    stencil.set_editor_property('scene_texture_id', unreal.SceneTextureId.PPI_CUSTOM_STENCIL)
    depth = MAT.create_material_expression(material, unreal.MaterialExpressionSceneTexture, -700, 200)
    depth.set_editor_property('scene_texture_id', unreal.SceneTextureId.PPI_CUSTOM_DEPTH)
    team = MAT.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 400)
    team.set_editor_property('parameter_name', 'LocalTeam')
    team.set_editor_property('default_value', 0.0)
    width = MAT.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -700, 550)
    width.set_editor_property('parameter_name', 'OutlineWidthPixels')
    width.set_editor_property('default_value', 2.0)
    color = MAT.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, 700)
    color.set_editor_property('parameter_name', 'OutlineColor')
    color.set_editor_property('default_value', unreal.LinearColor(1.0, 0.015, 0.025, 1.0))
    custom = MAT.create_material_expression(material, unreal.MaterialExpressionCustom, -200, 0)
    names = ['SceneColor', 'Stencil', 'Depth', 'LocalTeam', 'Width', 'Tint']
    inputs = []
    for name in names:
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name)
        inputs.append(item)
    custom.set_editor_property('inputs', inputs)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property('description', 'Visible enemy outer silhouette; teams 1 and 2')
    custom.set_editor_property('code', '''
float enemyTeam = LocalTeam == 1 ? 2 : (LocalTeam == 2 ? 1 : 0);
float2 uv = GetDefaultSceneTextureUV(Parameters, 25);
float2 pixel = View.BufferSizeAndInvSize.zw * Width;
float centerSceneDepth = SceneTextureLookup(uv, 1, false).r;
float centerEnemy = enemyTeam > 0 && abs(Stencil.r - enemyTeam) < 0.5
    && Depth.r <= centerSceneDepth + 1.0;
float edge = 0;
const float2 offsets[8] = {float2(1,0),float2(-1,0),float2(0,1),float2(0,-1),
    float2(.707,.707),float2(-.707,.707),float2(.707,-.707),float2(-.707,-.707)};
[unroll] for (int i = 0; i < 8; ++i)
{
    float2 p = ClampSceneTextureUV(uv + offsets[i] * pixel, 25);
    float team = SceneTextureLookup(p, 25, false).r;
    float targetDepth = SceneTextureLookup(p, 13, false).r;
    float surfaceDepth = SceneTextureLookup(p, 1, false).r;
    float visibleEnemy = enemyTeam > 0 && abs(team - enemyTeam) < 0.5
        && targetDepth <= surfaceDepth + 1.0 && targetDepth <= centerSceneDepth + 1.0;
    edge = max(edge, visibleEnemy);
}
return lerp(SceneColor.rgb, Tint.rgb, edge * (1-centerEnemy));
''')
    for node, name in zip([scene, stencil, depth, team, width, color], names):
        assert MAT.connect_material_expressions(node, '', custom, name), name
    assert MAT.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MAT.recompile_material(material)
    diagnostics = unreal.MaterialNodeService.get_material_diagnostics(PATH)
    assert diagnostics.success and diagnostics.is_compiled_ok and not diagnostics.compile_errors, str(diagnostics)
    assert unreal.EditorAssetLibrary.save_loaded_asset(material, only_if_is_dirty=False)
    graph = unreal.MaterialNodeService.export_material_graph(PATH)
    os.makedirs(os.path.dirname(REPORT), exist_ok=True)
    with open(REPORT, 'w', encoding='utf-8') as f:
        json.dump({'material':PATH, 'diagnostics':str(diagnostics), 'graph':json.loads(graph),
                   'previous_graph':previous_graph}, f, ensure_ascii=False, indent=2)
    print(json.dumps({'success':True, 'material':PATH, 'report':REPORT}))

main()
