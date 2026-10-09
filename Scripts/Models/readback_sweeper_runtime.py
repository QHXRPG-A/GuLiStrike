"""Read actual Q-summoned Mass batches and the values used by both clients."""
import json
import re
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/ArtSource/SweeperTeamColor_v1_20261008/UE')
definition=next(r for r in json.loads((OUT.parents[2]/'Data/Json/DT_GuLiStrikeModels_Models.json').read_text(encoding='utf8')) if r['Id']==1005)
def linear(hex_color):
    def f(x):
        x=int(x,16)/255
        return x/12.92 if x<=.04045 else ((x+.055)/1.055)**2.4
    return [f(hex_color[i:i+2]) for i in [1,3,5]]+[1]
EXPECTED = {'own':linear(definition['BluePrimaryHex']), 'enemy':linear(definition['EnemyPrimaryHex'])}


def values(registry, component):
    value = registry.get_vector_parameter(component,1005,'Root','*','TeamPrimary')
    return {'primary':list(value.to_tuple()) if value else None,
            'enabled':registry.get_scalar_parameter(component,1005,'Root','*','TeamEnabled')}


def run():
    result = {'clients': [], 'errors': [], 'source': 'actual native Q activation, server summons, client Mass meshes and registry actual CPD reads'}
    worlds = [w for w in unreal.ObjectIterator(unreal.World) if 'UEDPIE' in w.get_path_name()]
    server = next(w for w in worlds if '/UEDPIE_0_' in w.get_path_name())
    result['server_summoned_units'] = [u for u in json.loads(unreal.GuLiTeleportQALibrary.snapshot(server))['units'] if u['type']==5]
    for world in worlds:
        pc = unreal.GameplayStatics.get_player_controller(world,0)
        if not pc or not pc.is_local_controller():
            continue
        team = 1 if pc.player_state.get_team()==unreal.GuLiTeam.RED else 2
        registry = next(s for s in unreal.ObjectIterator(unreal.GuLiModelRegistrySubsystem) if s.get_world()==world)
        local = next(s for s in unreal.ObjectIterator(unreal.GuLiLocalTeamColorSubsystem) if s.get_world()==world)
        entry = {'world':world.get_path_name(),'actual_player_team':team,'batches':[],'refresh_checks':[]}
        reply = pc.player_state.get_commander_skills().get_last_reply()
        entry['q_reply'] = {'code':str(reply.code),'error':reply.error,
                            'unit_results':[{'succeeded':r.execution.succeeded,'error':r.execution.error} for r in reply.units]}
        for actor in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.GuLiCommanderPresentationActor):
            if actor.get_editor_property('is_editor_only_actor'):
                continue
            for comp in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
                if not comp.static_mesh or 'SM_Sweeper_Rigid' not in comp.static_mesh.get_path_name() or not re.search('_Team_[12]',comp.get_name()) or not comp.get_instance_count():
                    continue
                actual_team = int(re.search('_Team_([12])',comp.get_name()).group(1))
                relation = 'own' if actual_team==team else 'enemy'
                expected = EXPECTED[relation]
                value = values(registry,comp)
                passed = value['primary'] is not None and max(abs(a-b) for a,b in zip(value['primary'],expected))<1e-5 and value['enabled']==1
                poses = []
                for i in range(comp.get_instance_count()):
                    transform = comp.get_instance_transform(i,True)
                    if min(transform.scale3d.to_tuple())>0:
                        poses.append({'index':i,'position':list(transform.translation.to_tuple()),'scale':list(transform.scale3d.to_tuple())})
                entry['batches'].append({'component':comp.get_path_name(),'actual_unit_team':actual_team,'relation':relation,
                    'instances':comp.get_instance_count(),'visible_poses':poses,'mesh':comp.static_mesh.get_path_name(),
                    'material':comp.get_material(0).get_path_name(),'animation_stride':comp.get_editor_property('num_custom_data_floats'),
                    'actual':value,'expected':expected,'passed':passed})
                if not passed:
                    result['errors'].append(world.get_name()+'/'+comp.get_name()+' local relation CPD mismatch')
                original_team = unreal.GuLiTeam.RED if actual_team==1 else unreal.GuLiTeam.BLUE
                opposite_team = unreal.GuLiTeam.BLUE if actual_team==1 else unreal.GuLiTeam.RED
                try:
                    local.register_model_component(comp,1005,'Root',opposite_team,False)
                    local.apply_local_team_colors()
                    changed = values(registry,comp)
                    new_expected = EXPECTED['enemy' if relation=='own' else 'own']
                    ok = changed['primary'] is not None and max(abs(a-b) for a,b in zip(changed['primary'],new_expected))<1e-5
                    entry['refresh_checks'].append({'component':comp.get_name(),'ownership_registration_refresh':ok,'actual':changed})
                    if not ok:
                        result['errors'].append('Explicit team refresh failed: '+comp.get_name())
                finally:
                    local.register_model_component(comp,1005,'Root',original_team,False)
                    local.apply_local_team_colors()
                if values(registry,comp)!=value:
                    result['errors'].append('Original batch team failed to restore: '+comp.get_name())
        if {b['relation'] for b in entry['batches']}!={'own','enemy'}:
            result['errors'].append('Both Sweeper teams not present on '+world.get_name())
        result['clients'].append(entry)
    if len(result['clients'])!=2:
        result['errors'].append('Expected two local clients')
    result['success'] = not result['errors']
    (OUT/'runtime-local-team-readback.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    return {'success':result['success'],'errors':result['errors'],
            'summoned_sweepers':len(result['server_summoned_units']),
            'clients':[{'actual_player_team':c['actual_player_team'],'batches':[{k:b[k] for k in ['actual_unit_team','relation','instances','actual','passed']} for b in c['batches']]} for c in result['clients']]}


unreal.MCPythonHelper.submit_result(json.dumps(run()))
