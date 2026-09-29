"""Explicit one-shot PIE diagnostic. GPU readback is never used by the runtime or performance capture."""
import json, traceback
from pathlib import Path
import unreal

def snapshot_hover(label):
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    assert world, 'Start the authorized Mass PIE fixture first'
    result={'label':label,'world':world.get_path_name(),'source':'one-shot Niagara SimCache GPU readback; excluded from performance intervals','models':[],'systems':[]}
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor):
        if not (isinstance(actor,unreal.GuLiCommanderPresentationActor) or actor.actor_has_tag('WarMachineHoverPreview20260929')):continue
        for ism in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
            if not ism.static_mesh or '_Rigid' not in ism.static_mesh.get_name():continue
            result['models'].append({'component':ism.get_path_name(),'mesh':ism.static_mesh.get_path_name(),'instances':ism.get_instance_count(),
                'custom_floats':ism.get_editor_property('num_custom_data_floats'),'start_cull':ism.get_editor_property('instance_start_cull_distance'),'end_cull':ism.get_editor_property('instance_end_cull_distance'),
                'preview':actor.actor_has_tag('WarMachineHoverPreview20260929'),'first_pose':list(ism.get_editor_property('per_instance_sm_custom_data'))[:29]})
    arrays=unreal.NiagaraDataInterfaceArrayFunctionLibrary
    for component in unreal.ObjectIterator(unreal.NiagaraComponent):
        if component.get_world()!=world or not component.is_active() or not component.get_asset() or component.get_asset().get_name()!='NS_WarMachineHoverPool':continue
        cache=unreal.NiagaraSimCacheFunctionLibrary.create_niagara_sim_cache(world)
        params=unreal.NiagaraSimCacheCreateParameters();params.set_editor_property('attribute_capture_mode',unreal.NiagaraSimCacheAttributeCaptureMode.ALL)
        assert unreal.NiagaraSimCacheFunctionLibrary.capture_niagara_sim_cache_immediate(cache,params,component,False)
        cpu_positions=arrays.get_niagara_array_position(component,'User.LaserPositions')
        cpu_sizes=arrays.get_niagara_array_vector2d(component,'User.LaserSizes')
        cpu_directions=arrays.get_niagara_array_vector(component,'User.LaserDirections')
        system={'component':component.get_path_name(),'input_nozzles':len(cpu_positions),'emitters':[]}
        for name in cache.get_emitter_names():
            positions=cache.read_position_attribute('Position',name);colors=cache.read_color_attribute('Color',name)
            slots=cache.read_int_attribute('LaserSlot',name);sizes=cache.read_vector2_attribute('SpriteSize',name)
            item={'emitter':str(name),'allocated_gpu_particles':len(positions),'visible_particles':sum(c.a>0 and s.x>0 and s.y>0 for c,s in zip(colors,sizes)),
                  'unique_nozzle_slots':len(set(slots)),'examples':[]}
            if str(name)=='LaserBolts':
                directions=cache.read_vector_attribute('SpriteAlignment',name)
                errors=[(p-cpu_positions[s]).length() for s,p,c in zip(slots,positions,colors) if c.a>0]
                item['gpu_cpu_position_max_error_cm']=max(errors,default=0)
                item['gpu_cpu_direction_max_error']=max(((d-cpu_directions[s]).length() for s,d,c in zip(slots,directions,colors) if c.a>0),default=0)
                item['cpu_widths_cm']=sorted(set(round(s.x,3) for s in cpu_sizes if s.x>0))
                assert len(slots)==1024 and len(set(slots))==1024,'GPU core slot aliasing'
            else:
                lanes=cache.read_int_attribute('HoverLane',name)
                assert len(slots)==10240 and len(set(zip(slots,lanes)))==10240,'GPU history lane aliasing'
            for slot,p,color,size in zip(slots,positions,colors,sizes):
                if color.a<=0 or len(item['examples'])>=32:continue
                item['examples'].append({'slot':slot,'position':list(p.to_tuple()),'size':list(size.to_tuple()),'alpha':color.a})
            system['emitters'].append(item)
        result['systems'].append(system)
    return result

label=globals().pop('HOVER_SNAPSHOT_LABEL','runtime')
try:
    report=snapshot_hover(label);report['success']=True
except:report={'success':False,'error':traceback.format_exc()}
out=Path('D:/UE5.7/test1/ArtSource/WarMachineHover_20260929')
(out/('runtime-'+label+'.json')).write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'error':report.get('error'),'systems':len(report.get('systems',[])),'record':str(out/('runtime-'+label+'.json'))}))
