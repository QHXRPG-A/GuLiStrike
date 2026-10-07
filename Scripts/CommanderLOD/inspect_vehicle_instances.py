"""Inspect assembled editor-only presentation actors, then remove the temporary actors."""
import json
import sys
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART,units
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not editor.get_game_world()
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
result=[]
skeletal=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
for row in units():
    if not row['PresentationClass']: continue
    actor=actors.spawn_actor_from_class(unreal.load_class(None,row['PresentationClass']),unreal.Vector(-45000,-60000,800))
    assert actor
    try:
        components=[]
        for component in actor.get_components_by_class(unreal.MeshComponent):
            asset=component.get_editor_property('static_mesh') if isinstance(component,unreal.StaticMeshComponent) else component.get_editor_property('skeletal_mesh_asset') if isinstance(component,unreal.SkeletalMeshComponent) else None
            if not asset: continue
            transform=unreal.MathLibrary.make_relative_transform(component.get_world_transform(),actor.get_actor_transform())
            entry=dict(name=component.get_name(),type=component.get_class().get_name(),mesh=asset.get_path_name(),
                mesh_type=asset.get_class().get_name(),visible=component.is_visible(),hidden_in_game=component.get_editor_property('hidden_in_game'),
                location=list(transform.translation.to_tuple()),rotation=list(transform.rotation.rotator().to_tuple()),scale=list(transform.scale3d.to_tuple()),
                materials=[material.get_path_name() if material else None for material in component.get_materials()])
            if isinstance(asset,unreal.SkeletalMesh):
                entry['skeletal_lod_count']=skeletal.get_lod_count(asset)
                entry['skeleton']=asset.skeleton.get_path_name() if asset.skeleton else None
            components.append(entry)
        result.append(dict(id=row['Id'],name=row['Name'],components=components))
    finally:
        assert actors.destroy_actor(actor)
(ART/'Reports/vehicle_instances.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,units=result)))
