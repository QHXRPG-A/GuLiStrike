"""Derive the approved vendor trail; never edit/save the marketplace template."""
import json
from pathlib import Path
import unreal

SOURCE = '/Game/Assets/VFX/Movement/AdvancedDashVFX/NiagaraVFX/NS_GreenCrystalTrail'
TARGET = '/Game/GuLiStrike/FX/GroundMech/Blink/NS_GroundMech_Blink'
# Demo emits for 2.5s, while Ground's movement phase lasts 0.8s. Keep about
# 200 live ribbon points per mannequin-height at the end of the Ground dash:
# 200 * (488.07364 / 182.56690979) / 0.8 = 668.35 points/s, rounded to 670.
# World-space skeletal sampling already includes the mech's size. Do not scale
# that sampled geometry a second time through the component/table transform.
TUNING = {
    'Constants.Trails.SpawnRate.SpawnRate': 670.0,
    # Fixed-step skeletal displacement must not advect newly spawned points
    # along with the mech. Leave the ribbon at the path actually traversed.
    'Constants.Trails.SkeletalMeshLocation.Inherit Velocity (Scale)': 0.0,
}


def parameters(path):
    return {f'{p.script_type}:{p.parameter_name}': p.value
            for p in unreal.NiagaraService.list_rapid_iteration_params(path, 'Trails')}


def main():
    if unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor():
        raise RuntimeError('Stop PIE before authoring Blink presentation')
    report = {'source': SOURCE, 'target': TARGET, 'visual_approval': 'pending_player'}
    root=Path(unreal.Paths.project_dir()).resolve()
    rows=json.loads((root/'data/Json/DT_GuLiStrikeMech_Skills.json').read_text(encoding='utf8'))
    tuning=next(r for r in rows if r['Name']=='Blink')
    if 'BlinkConcealSeconds' in tuning:
        raise RuntimeError('Use author_blink_teleport_fx.py; skeletal moving trail authoring is retired')
    count=int(tuning['BlinkRibbonLayers'])
    if count<3:
        raise RuntimeError('Approved 3C requires at least three independent ribbon streams')
    emitters=['Trails']+[f'TrailsDense{i}' for i in range(2,count+1)]
    source_before = parameters(SOURCE)
    if not source_before:
        raise RuntimeError('Cannot inspect the source trail; no assets changed')
    if not unreal.EditorAssetLibrary.does_asset_exist(TARGET):
        if not unreal.EditorAssetLibrary.duplicate_asset(SOURCE, TARGET):
            raise RuntimeError('Failed to derive Ground blink trail')
        report['duplicated'] = True
    else:
        report['duplicated'] = False
    system = unreal.load_asset(TARGET)
    if not isinstance(system, unreal.NiagaraSystem):
        raise RuntimeError('Target is not a NiagaraSystem')
    report['parameters_before'] = parameters(TARGET)
    present=list(unreal.NiagaraService.summarize(TARGET).emitter_names)
    for emitter in emitters[1:]:
        if emitter not in present:
            if not unreal.NiagaraService.duplicate_emitter(TARGET,'Trails',emitter):
                raise RuntimeError('Cannot duplicate independent ribbon stream: '+emitter)
    report['streams']=[]
    for emitter in emitters:
        expected={'.SpawnRate.SpawnRate':float(tuning['BlinkRibbonSpawnRate']),
                  '.SkeletalMeshLocation.Inherit Velocity (Scale)':0.0}
        actual=unreal.NiagaraService.list_rapid_iteration_params(TARGET,emitter)
        for suffix,value in expected.items():
            found=[p for p in actual if p.parameter_name.endswith(suffix)]
            if not found:
                raise RuntimeError('Missing cloned parameter: '+emitter+suffix)
            for p in found:
                if not unreal.NiagaraService.set_rapid_iteration_param(TARGET,emitter,p.parameter_name,str(value)):
                    raise RuntimeError('Cannot tune stream: '+emitter)
        report['streams'].append({'emitter':emitter,'spawn_rate':expected['.SpawnRate.SpawnRate'],
                                 'renderer_count':len(unreal.NiagaraEmitterService.list_renderers(TARGET,emitter)),
                                 'module_count':len(unreal.NiagaraEmitterService.list_modules(TARGET,emitter,''))})
    # Keep the source topology, color curves, materials, lifetime and noise.
    result = unreal.NiagaraService.compile_with_results(TARGET)
    if not result.success or result.error_count:
        raise RuntimeError(f'Blink Niagara compile failed: {result}')
    report['compile'] = {'success': result.success, 'errors': result.error_count,
                         'warnings': result.warning_count}
    report['parameters_after'] = parameters(TARGET)
    for name, value in TUNING.items():
        found = [v for k, v in report['parameters_after'].items() if k.endswith(':' + name)]
        if not found or any(abs(float(v) - value) > 0.001 for v in found):
            raise RuntimeError(f'Blink parameter readback mismatch: {name}: {found}')
    if parameters(SOURCE) != source_before:
        raise RuntimeError('Marketplace source parameters unexpectedly changed')
    report['source_unchanged'] = True
    summary = unreal.NiagaraService.summarize(TARGET)
    report['emitters'] = list(summary.emitter_names)
    report['has_gpu_emitters'] = summary.has_gpu_emitters
    if sorted(report['emitters']) != sorted(emitters):
        raise RuntimeError(f'Unexpected GreenCrystalTrail topology: {report["emitters"]}')
    if not unreal.EditorAssetLibrary.save_asset(TARGET, only_if_is_dirty=True):
        raise RuntimeError('Target save failed')
    report['saved_path'] = unreal.load_asset(TARGET).get_path_name()
    report['independent_stream_multiplier']=count
    report['total_spawn_rate']=count*float(tuning['BlinkRibbonSpawnRate'])
    folder = root / 'ArtSource/FX/GroundMech/Blink_20261027/3C_B_v1'
    folder.mkdir(parents=True, exist_ok=True)
    (folder / 'niagara_readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    unreal.MCPythonHelper.submit_result(json.dumps({k: v for k, v in report.items()
        if not k.startswith('parameters_')}, ensure_ascii=False))


main()
