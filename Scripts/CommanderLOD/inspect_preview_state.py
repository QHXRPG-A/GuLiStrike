import unreal,json
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
rows=[]
for actor in actors:
    if actor.get_actor_label() in ['CommanderLOD_DefaultSoldier_LOD0','CommanderLOD_WM01_LOD0','CommanderLOD_BiZhiMao_LOD0']:
        c=actor.get_component_by_class(unreal.InstancedStaticMeshComponent)
        m=c.get_editor_property('static_mesh')
        rows.append(dict(name=actor.get_actor_label(),mesh=m.get_path_name(),bounds=list(m.get_bounds().box_extent.to_tuple()),
            actor_location=list(actor.get_actor_location().to_tuple()),actor_scale=list(actor.get_actor_scale3d().to_tuple()),
            relative_scale=list(c.get_editor_property('relative_scale3d').to_tuple()),instances=c.get_instance_count(),
            transform=str(c.get_instance_transform(0,True)) if c.get_instance_count() else None,
            visible=c.get_editor_property('visible'),hidden_in_game=c.get_editor_property('hidden_in_game'),
            custom=list(c.get_editor_property('per_instance_sm_custom_data'))[:64]))
unreal.MCPythonHelper.submit_result(json.dumps(rows))
