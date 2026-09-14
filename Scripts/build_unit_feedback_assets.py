"""Author unit hit materials and pooled copies of the requested explosion pack through the live UE editor."""
import json
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(unreal.Paths.project_dir()).resolve() / 'Scripts'))
from unit_destruction_shockwave import calibrate

DEST = '/Game/GuLiStrike/FX/UnitFeedback'
SOURCE = '/Game/RealisticExplosionPackD/Particles'
OUT = Path('D:/UE5.7/test1/Artifacts/UnitFeedback')
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
MAT = unreal.MaterialEditingLibrary
NS = unreal.NiagaraService
report = {'source': SOURCE, 'destination': DEST, 'materials': {}, 'systems': [], 'saved': []}


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def save(obj):
    path = obj.get_path_name()
    require(path.startswith(DEST + '/'), 'Out of scope save: ' + path)
    require(ASSETS.save_loaded_asset(obj), 'Save failed: ' + path)
    report['saved'].append(path)


def hit_material(instanced):
    name = 'M_UnitHitWhite_Instanced' if instanced else 'M_UnitHitWhite'
    path = DEST + '/' + name
    if not ASSETS.does_asset_exist(path):
        require(unreal.MaterialService.create_material(name, DEST).success, 'Create ' + path)
    obj = ASSETS.load_asset(path)
    # UE 5.7's bulk delete mutates the expression array while iterating it; use a stable list.
    for expression in list(unreal.ObjectIterator(unreal.MaterialExpression)):
        if expression.get_outer() == obj:
            MAT.delete_material_expression(obj, expression)
    obj.set_editor_property('blend_mode', unreal.BlendMode.BLEND_ADDITIVE)
    obj.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    obj.set_editor_property('two_sided', False)
    MAT.set_material_usage(obj, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    MAT.set_material_usage(obj, unreal.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    time = MAT.create_material_expression(obj, unreal.MaterialExpressionTime, -750, 0)
    start = MAT.create_material_expression(obj,
        unreal.MaterialExpressionPerInstanceCustomData if instanced else unreal.MaterialExpressionScalarParameter, -750, 180)
    if instanced:
        start.set_editor_property('data_index', 0)
        start.set_editor_property('const_default_value', -1000.0)
    else:
        start.set_editor_property('parameter_name', 'HitStartTime')
        start.set_editor_property('default_value', -1000.0)
        start.set_editor_property('use_custom_primitive_data', True)
        start.set_editor_property('primitive_data_index', 7)
    age = MAT.create_material_expression(obj, unreal.MaterialExpressionSubtract, -500, 0)
    require(MAT.connect_material_expressions(time, '', age, 'A'), 'Time')
    require(MAT.connect_material_expressions(start, '', age, 'B'), 'Start')
    normalize = MAT.create_material_expression(obj, unreal.MaterialExpressionMultiply, -300, 0)
    normalize.set_editor_property('const_b', 2.0)  # Exactly 0.5 seconds in game time.
    require(MAT.connect_material_expressions(age, '', normalize, 'A'), 'Normalize age')
    inverse = MAT.create_material_expression(obj, unreal.MaterialExpressionOneMinus, -100, 0)
    require(MAT.connect_material_expressions(normalize, '', inverse, ''), 'Invert age')
    alpha = MAT.create_material_expression(obj, unreal.MaterialExpressionSaturate, 80, 0)
    require(MAT.connect_material_expressions(inverse, '', alpha, ''), 'Clamp alpha')
    white = MAT.create_material_expression(obj, unreal.MaterialExpressionConstant3Vector, 80, -180)
    white.set_editor_property('constant', unreal.LinearColor(2.0, 2.0, 2.0, 1.0))
    glow = MAT.create_material_expression(obj, unreal.MaterialExpressionMultiply, 280, -100)
    require(MAT.connect_material_expressions(white, '', glow, 'A'), 'White color')
    require(MAT.connect_material_expressions(alpha, '', glow, 'B'), 'Fade brightness')
    # Opacity stays at its default 1. Only the added white light fades; the unit's base material is untouched.
    require(MAT.connect_material_property(glow, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR), 'White glow')
    MAT.layout_material_expressions(obj)
    MAT.recompile_material(obj)
    diagnostics = unreal.MaterialNodeService.get_material_diagnostics(path)
    require(diagnostics.success and diagnostics.is_compiled_ok and not diagnostics.compile_errors, str(diagnostics))
    save(obj)
    report['materials'][path] = {'diagnostics': str(diagnostics),
        'graph': json.loads(unreal.MaterialNodeService.export_material_graph(path))}


def main():
    require(not unreal.WidgetService.is_pie_running(), 'Stop PIE before authoring')
    OUT.mkdir(parents=True, exist_ok=True)
    hit_material(False)
    hit_material(True)
    effect_path = DEST + '/FXT_UnitDestruction'
    effect = ASSETS.load_asset(effect_path) if ASSETS.does_asset_exist(effect_path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        'FXT_UnitDestruction', DEST, unreal.NiagaraEffectType, unreal.NiagaraEffectTypeFactoryNew())
    settings = unreal.NiagaraSystemScalabilitySettings()
    settings.set_editor_property('cull_by_distance', True)
    settings.set_editor_property('max_distance', 180000.0)
    array = effect.get_editor_property('system_scalability_settings')
    array.set_editor_property('settings', [settings])
    effect.set_editor_property('system_scalability_settings', array)
    effect.set_editor_property('cull_reaction', unreal.NiagaraCullReaction.DEACTIVATE_IMMEDIATE)
    effect.set_editor_property('update_frequency', unreal.NiagaraScalabilityUpdateFrequency.MEDIUM)
    save(effect)
    for letter in 'ABCDEFGHIJKLMNOPQRST':
        src = SOURCE + '/NS_Explosion_' + letter
        dst = DEST + '/NS_UnitDestruction_' + letter
        original = require(NS.summarize(src), 'Missing source ' + src)
        require('Shockwave' in [str(x) for x in original.emitter_names], 'Source is missing shockwave')
        obj = ASSETS.load_asset(dst) if ASSETS.does_asset_exist(dst) else ASSETS.duplicate_asset(src, dst)
        require(obj, 'Duplicate ' + src)
        names = [str(x.emitter_name) for x in NS.list_emitters(dst)]
        if 'SingleLoopingParticle' in names:
            # The original is a particle light. The explosion, shockwave, smoke and sparks stay intact.
            require(NS.remove_emitter(dst, 'SingleLoopingParticle'), 'Remove standard-unit dynamic light')
        obj.set_editor_property('effect_type', effect)
        obj.set_editor_property('max_pool_size', 16)
        obj.set_editor_property('pool_prime_size', 0)
        wave_calibration = calibrate(dst)
        result = NS.compile_with_results(dst)
        require(result.success and result.error_count == 0, 'Niagara compile: ' + str(result))
        save(obj)
        summary = require(NS.summarize(dst), 'Read back ' + dst)
        require('Shockwave' in [str(x) for x in summary.emitter_names], 'Persisted shockwave missing')
        report['systems'].append({'source': src, 'path': dst, 'compile': str(result),
            'summary': str(summary), 'pool_size': obj.get_editor_property('max_pool_size'),
            'shockwave': wave_calibration})
        (OUT / 'asset_build.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'success': True, 'systems': len(report['systems']), 'materials': len(report['materials']), 'report': str(OUT / 'asset_build.json')}))


try:
    main()
except Exception as exc:
    OUT.mkdir(parents=True, exist_ok=True)
    report['error'] = str(exc)
    (OUT / 'asset_build.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    raise
