"""Reload the saved Mass map and read only this Sweeper acceptance fixture set."""
import gc
import json
from pathlib import Path
import unreal

OUT=Path('D:/UE5.7/test1/ArtSource/SweeperTeamColor_v1_20261008/UE')
saved=json.loads((OUT/'acceptance-scene-save.json').read_text(encoding='utf8'))
for n in ['world','w','actor','a','pc','p','comp','c','reference','base','camera','note','editor','levels','registry','local','sub','r','s','ws','worlds','server','cs']:
    globals().pop(n,None)
gc.collect()
if not unreal.EditorLoadingAndSavingUtils.load_map(saved['map']):
    raise RuntimeError('Saved acceptance map could not reload.')


def run():
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    owned=[a for a in actors if a.actor_has_tag('GuLi.SweeperOrangeTeam.Acceptance.20261009')]
    errors=[];entries=[]
    if sorted(a.get_actor_label() for a in owned)!=sorted(saved['owned_actors']):
        errors.append('Saved fixture set differs')
    registry=next(s for s in unreal.ObjectIterator(unreal.GuLiModelRegistrySubsystem)
                  if s.get_world()==unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world())
    for a in owned:
        entry={'label':a.get_actor_label(),'editor_only':bool(a.get_editor_property('is_editor_only_actor')),
               'position_cm':list(a.get_actor_location().to_tuple()),'scale':list(a.get_actor_scale3d().to_tuple())}
        if not entry['editor_only']:errors.append(entry['label']+' is not editor-only')
        if isinstance(a,unreal.StaticMeshActor):
            c=a.static_mesh_component
            color=registry.get_vector_parameter(c,1005,'Root','*','TeamPrimary')
            entry.update(mesh=c.static_mesh.get_path_name(),material=c.get_material(0).get_path_name(),
                         primary=list(color.to_tuple()) if color else None,
                         enabled=registry.get_scalar_parameter(c,1005,'Root','*','TeamEnabled'))
            expected=next(d['expected_primary'] for d in saved['definitions'] if d['label']==a.get_actor_label())
            if not color or max(abs(x-y) for x,y in zip(color.to_tuple(),expected))>1e-5 or entry['enabled']!=1:
                errors.append('Saved default CPD differs: '+entry['label'])
        entries.append(entry)
    formal=json.loads((OUT.parent/'formal-ue-import.json').read_text(encoding='utf8'))
    mesh=unreal.load_object(None,formal['mesh'])
    snapshot=json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(mesh))
    if snapshot!=formal['mesh_snapshot_before']:errors.append('Native mesh invariant changed after map reload')
    return {'success':not errors,'map':saved['map'],'saved_and_reloaded':True,'actors':entries,'errors':errors,
            'native_mesh_attributes_exact':snapshot==formal['mesh_snapshot_before'],
            'runtime_checks':'separate real Q summons and both-client readbacks'}


result=run()
(OUT/'acceptance-scene-readback.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(result))
