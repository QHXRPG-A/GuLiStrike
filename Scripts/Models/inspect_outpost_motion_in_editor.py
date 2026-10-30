"""Read current presentation poses in an explicitly owned preview session."""
import json
from pathlib import Path
import unreal

OUT=Path('D:/UE5.7/test1/ArtSource/Buildings/OutpostSymmetry_20261029/GlowSpin_v1')
session=json.loads((OUT/'pie-session.json').read_text(encoding='utf8'))
status=json.loads(unreal.get_editor_subsystem(unreal.load_class(None,'/Script/GuLiStrikeEditor.GuLiLocalPIESubsystem')).get_session_status())
assert status['active'] and status['room']==session['status']['room']
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
now=unreal.GameplayStatics.get_game_state(world).get_server_world_time_seconds()
registry=next(s for s in unreal.ObjectIterator(unreal.GuLiModelRegistrySubsystem) if s.get_outer()==world)
rows=[]
for actor in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.load_class(None,'/Script/GuLiStrike.GuLiTerritoryOutpostActor')):
    mesh=next(c for c in actor.get_components_by_class(unreal.StaticMeshComponent) if c.get_name()=='OutpostVisual')
    ground=actor.get_actor_location().z-500.
    rotation=mesh.get_editor_property('RelativeRotation')
    primary=registry.get_vector_parameter(mesh,2003,'Root','*','TeamPrimary')
    # Yaw does not change vertical extent; source pivot is centered by UpdatePose.
    box=mesh.static_mesh.get_bounds()
    center=mesh.get_world_transform().transform_location(box.origin)
    half_height=box.box_extent.z*mesh.get_world_scale().z
    rows.append({'territory':str(actor.get_territory_id()),'actor_location':list(actor.get_actor_location().to_tuple()),
        'actor_rotation':list(actor.get_actor_rotation().to_tuple()),'yaw_deg':rotation.yaw,'pitch':rotation.pitch,'roll':rotation.roll,
        'visual_center':list(center.to_tuple()),'bottom_clearance_cm':center.z-half_height-ground,
        'material':mesh.get_material(0).get_path_name(),'mesh':mesh.static_mesh.get_path_name(),
        'core_owned':registry.get_scalar_parameter(mesh,2003,'Root','*','CoreUseTeamColor'),
        'team_primary':list(primary.to_tuple()),'ground_collision':list(actor.get_component_by_class(unreal.BoxComponent).get_unscaled_box_extent().to_tuple())})
assert len(rows)==49
report={'success':True,'server_time':now,'room':status['room'],'period_seconds':60,'outposts':rows}
name=globals().get('GULI_MOTION_SAMPLE','motion-first.json')
assert '/' not in name and '\\' not in name
(OUT/name).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'server_time':now,'count':len(rows),
    'yaw_range':[min(r['yaw_deg'] for r in rows),max(r['yaw_deg'] for r in rows)],
    'clearance_range_cm':[min(r['bottom_clearance_cm'] for r in rows),max(r['bottom_clearance_cm'] for r in rows)],
    'owned_values':sorted(set(r['core_owned'] for r in rows))}))
