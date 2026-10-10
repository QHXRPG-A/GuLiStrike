"""Read source graphs/scales in the live editor; never mutate or save packages."""
import json
from pathlib import Path
import unreal

OUT=Path(unreal.Paths.project_dir())/'outputs/performance/20261009-client-three-optimizations'
report={'success':True,'pie_worlds':[w.get_path_name() for w in unreal.EditorLevelLibrary.get_pie_worlds(True)],'effects':[]}
for effect_id in [2,5,52,36,45]:
    row=unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(effect_id)
    asset=row.resource_path
    path=asset.get_path_name().split('.')[0]
    item={'id':effect_id,'path':path,'scale':[row.scale.x,row.scale.y,row.scale.z],
          'settings':[], 'emitters':[]}
    settings=unreal.NiagaraService.get_all_editable_settings(path)
    for p in settings.rapid_iteration_parameters:
        item['settings'].append({'path':str(p.setting_path),'value':str(p.current_value)})
    for e in unreal.NiagaraService.list_emitters(path):
        name=str(e.emitter_name)
        item['emitters'].append({'name':name,'modules':[str(m) for m in unreal.NiagaraEmitterService.list_modules(path,name)],
          'renderers':[str(unreal.NiagaraEmitterService.get_renderer_details(path,name,r.renderer_index)) for r in unreal.NiagaraEmitterService.list_renderers(path,name)]})
    if effect_id in (5,52):item['graph']=json.loads(unreal.GuLiCombatEffectAuthoringLibrary.get_machine_gun_bounds_inputs(asset))
    report['effects'].append(item)
(OUT/'source-vfx-readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'pie_worlds':report['pie_worlds'],'saved':str(OUT/'source-vfx-readback.json')}))
