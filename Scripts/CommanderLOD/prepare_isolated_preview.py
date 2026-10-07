"""Recreate only one saved unit group in an empty editor level for bounded art rendering."""
import unreal,json,sys
from pathlib import Path
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART,selected_indices
name=commander_lod_arguments['unit']
source_compare=commander_lod_arguments.get('source_compare',False)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem);assert not editor.get_game_world()
world=editor.get_editor_world()
assert world.get_path_name().split('.')[0]=='/Engine/Maps/Entry',world.get_path_name()
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
for actor in unreal.ObjectIterator(unreal.Actor):
 if actor.get_path_name().startswith(world.get_path_name()) and actor.get_actor_label().startswith('CommanderLOD_'):actors.destroy_actor(actor)
unreal._commander_lod_preview_actors=[]
light=actors.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,5000),unreal.Rotator(-45,-35,0),transient=True)
light.set_actor_label('CommanderLOD_ArtKeyLight')
light.light_component.set_intensity(4)
light.light_component.set_editor_property('cast_shadows',False)
unreal._commander_lod_preview_actors.append(light)
scene=json.loads((ART/'Reports/review_scene.json').read_text(encoding='utf8'))
for group in scene['groups']:
 if group['name']!=name:continue
 for i,label in enumerate(group['actors']):
  mesh=unreal.load_asset(group['references'][i]['mesh'])
  if source_compare:
   assert len(group['actors'])==1 and name!='BiZhiMao'
   source=unreal.EditorAssetLibrary.get_metadata_tag(mesh,'GuLi.SourceAsset');assert source
   mesh=unreal.load_asset(source)
  if len(group['actors'])==1:
   actor=actors.spawn_actor_from_class(unreal.GuLiCommanderPresentationActor,unreal.Vector(*group['origin']),transient=True)
   component=next(c for c in actor.get_components_by_class(unreal.InstancedStaticMeshComponent) if c.get_name()=='UnitInstances')
   component.set_static_mesh(mesh)
   tier=selected_indices(unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_lod_count(mesh))[group['lod']] if source_compare else group['lod']
   component.set_forced_lod_model(tier+1)
   component.set_num_custom_data_floats(63);component.clear_instances()
   component.add_instance(unreal.Transform(scale=unreal.Vector(*([group['scale']]*3))),False)
   for j,v in [(0,-1000),(29,-1000),(30,1)]:component.set_custom_data_value(0,j,v,True)
  else:
   source=json.loads((ART/'Reports/vehicle_instances.json').read_text(encoding='utf8'))
   part=next(row for row in source if row['name']==name)['components'][i]
   actor=actors.spawn_actor_from_class(unreal.SkeletalMeshActor if isinstance(mesh,unreal.SkeletalMesh) else unreal.StaticMeshActor,
    unreal.Vector(*group['origin'])+unreal.Vector(*part['location'])*group['scale'],transient=True)
   actor.set_actor_rotation(unreal.Rotator(*part['rotation']),True)
   actor.set_actor_scale3d(unreal.Vector(*part['scale'])*group['scale'])
   component=actor.get_component_by_class(unreal.MeshComponent)
   if isinstance(mesh,unreal.SkeletalMesh):component.set_skeletal_mesh_asset(mesh);component.set_forced_lod(group['lod']+1)
   else:component.set_static_mesh(mesh);component.set_forced_lod_model(group['lod']+1)
   for j,path in enumerate(part['materials']):
    if path:component.set_material(j,unreal.load_asset(path))
  actor.set_actor_label(label);actor.set_editor_property('is_editor_only_actor',False)
  unreal._commander_lod_preview_actors.append(actor)
  component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
  component.set_editor_property('can_ever_affect_navigation',False)
settings_file=ART/'Reports/capture_editor_settings.json'
if not settings_file.exists():settings_file.write_text(json.dumps(dict(idle_when_not_foreground=unreal.SystemLibrary.get_console_variable_int_value('t.IdleWhenNotForeground'))),encoding='utf8')
unreal.SystemLibrary.execute_console_command(world,'t.IdleWhenNotForeground 0')
unreal.SystemLibrary.execute_console_command(world,'Editor.AsyncTextureCompilationFinishAll')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,unit=name,world=world.get_path_name())))
