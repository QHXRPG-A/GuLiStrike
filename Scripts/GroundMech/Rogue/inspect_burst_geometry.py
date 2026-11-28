"""Numerical Niagara mesh measurements for the radial burning pulse."""
import json
from functools import lru_cache
from pathlib import Path
import unreal


def shell_renderer(asset_path):
    system = unreal.load_asset(asset_path)
    renderer_class = unreal.load_class(None,'/Script/Niagara.NiagaraMeshRendererProperties')
    return next(r for r in unreal.ObjectIterator(renderer_class)
                if r.get_outermost() == system.get_outermost() and r.get_outer().get_name().startswith('BurningShell'))


@lru_cache(maxsize=2)
def shell_extent(asset_path):
    mesh = shell_renderer(asset_path).get_editor_property('meshes')[0].get_editor_property('mesh')
    box = mesh.get_bounding_box()
    return ((box.max - box.min) * .5).to_tuple()


def read_shell(component):
    blank = {'emitter':'BurningShell','particles':0,'outer_radius':0.,'min_age':0.,'max_age':0.,'lifetime':0.,'max_alpha':0.}
    result = {'emitters':[blank], 'complete':not component.is_active()}
    if result['complete']:
        return result
    world = component.get_world()
    cache = unreal.NiagaraSimCacheFunctionLibrary.create_niagara_sim_cache(world)
    params = unreal.NiagaraSimCacheCreateParameters()
    params.set_editor_property('attribute_capture_mode', unreal.NiagaraSimCacheAttributeCaptureMode.ALL)
    if not unreal.NiagaraSimCacheFunctionLibrary.capture_niagara_sim_cache_immediate(cache,params,component,False):
        return result
    name = 'BurningShell'
    scales = cache.read_vector_attribute('Scale',name)
    if not scales:
        return result
    extent = shell_extent(component.get_asset().get_path_name())
    ages = cache.read_float_attribute('Age',name)
    lives = cache.read_float_attribute('Lifetime',name)
    colors = cache.read_color_attribute('Color',name)
    blank.update(particles=len(scales),
                 outer_radius=max(abs(s*e) for scale in scales for s,e in zip(scale.to_tuple(),extent)),
                 min_age=min(ages),max_age=max(ages),lifetime=max(lives),max_alpha=max(c.a for c in colors))
    return result


if __name__ == '__main__':
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    asset=unreal.load_asset('/Game/GuLiStrike/FX/GroundMech/Burning/NS_GroundBurningBurst')
    component=unreal.NiagaraFunctionLibrary.spawn_system_at_location(world,asset,unreal.Vector(0,0,1000),auto_destroy=False,auto_activate=False,pre_cull_check=False)
    assert component
    rows=[]
    try:
        for key,value in [('Radius',500),('StartRadius',175),('InitialAge',0)]:
            component.set_variable_float('User.'+key,value)
        lifetime=float(unreal.NiagaraService.get_parameter(asset.get_path_name(),'User.Lifetime').current_value)
        step_seconds=lifetime/20
        component.set_variable_int('User.Seed',12345)
        component.activate(True)
        for step in range(1,23):
            component.advance_simulation(1,step_seconds)
            rows.append({'requested_age':step*step_seconds,'data':read_shell(component)})
    finally:
        component.deactivate()
        component.destroy_component(component.get_owner() or component)
    p=Path(globals().get('GULI_PROBE_OUT',Path(unreal.Paths.project_dir())/'Artifacts/GroundRevision_20261103'))/'geometry_probe.json'
    p.write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps({'rows':len(rows),'report':str(p),'samples':[
        {'time':row['requested_age'],'emitters':[{k:e[k] for k in ('emitter','particles','outer_radius','max_alpha')} for e in row['data']['emitters']]}
        for row in (rows[9],rows[10],rows[-1])]}))
