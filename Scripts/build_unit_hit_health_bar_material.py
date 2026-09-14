"""Author the shared, continuously fading hit-bar material without editing the existing selection-bar source."""
import json
import unreal

SOURCE = '/Game/Commander/UI/Materials/M_UI_Cmd_SoldierHealthBarWorld'
TARGET = '/Game/GuLiStrike/FX/UnitFeedback/M_UnitHitHealthBarWorld'


def main():
    if unreal.WidgetService.is_pie_running():
        raise RuntimeError('Stop PIE before material authoring')
    dirty = {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    if TARGET in dirty:
        raise RuntimeError('Unsaved edits in ' + TARGET)
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    material = assets.load_asset(TARGET) if assets.does_asset_exist(TARGET) else assets.duplicate_asset(SOURCE, TARGET)
    if not material:
        raise RuntimeError('Could not load/duplicate health-bar material')
    expressions = [x for x in unreal.ObjectIterator(unreal.MaterialExpression) if x.get_outer() == material]
    visible = [x for x in expressions if isinstance(x, unreal.MaterialExpressionPerInstanceCustomData)
               and x.get_editor_property('data_index') == 2]
    colors = [x for x in expressions if isinstance(x, unreal.MaterialExpressionCustom)]
    if len(visible) != 1 or len(colors) != 1:
        raise RuntimeError('Unknown source health-bar graph')
    code = colors[0].get_editor_property('code')
    old_fill = 'float Fill = Inner * step(P.x, saturate(Health));'
    new_fill = 'float Fill = Inner * step(P.x, Health < 0.0 ? 1.0 : saturate(Health));'
    if old_fill not in code and new_fill not in code:
        raise RuntimeError('Unknown health fill expression')
    # Placeholder units without gameplay health show a neutral hit indicator, not fabricated HP.
    code = code.replace(old_fill, new_fill).replace(
        'float3 HealthColor = float3(2.0, 0.02, 0.03);',
        'float3 HealthColor = Health < 0.0 ? float3(0.65, 0.65, 0.65) : float3(2.0, 0.02, 0.03);')
    colors[0].set_editor_property('code', code)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    edit = unreal.MaterialEditingLibrary
    if not edit.connect_material_property(visible[0], '', unreal.MaterialProperty.MP_OPACITY):
        raise RuntimeError('Could not connect continuous hit opacity')
    edit.recompile_material(material)
    result = unreal.MaterialNodeService.get_material_diagnostics(TARGET)
    if not result.success or not result.is_compiled_ok or result.compile_errors:
        raise RuntimeError(str(result))
    if not assets.save_loaded_asset(material):
        raise RuntimeError('Save failed')
    graph = json.loads(unreal.MaterialNodeService.export_material_graph(TARGET))
    if graph['material']['blend_mode'] != 'BLEND_Translucent' or not any(
            x['property'] == 'Opacity' for x in graph['output_connections']):
        raise RuntimeError('Saved opacity contract did not match')
    return {'success': True, 'saved': TARGET, 'diagnostics': str(result), 'graph': graph}


unreal.MCPythonHelper.submit_result(json.dumps(main()))
