"""Four images of actual Q-summoned Mass batches; no replacement preview units."""
import json
import re
from pathlib import Path
import unreal

OUT=Path('D:/UE5.7/test1/ArtSource/SweeperTeamColor_v1_20261008/UE')
IMAGES=OUT/'Images'
report={'captures':[],'errors':[]}
for world in unreal.ObjectIterator(unreal.World):
    if 'UEDPIE' not in world.get_path_name():continue
    pc=unreal.GameplayStatics.get_player_controller(world,0)
    if not pc or not pc.is_local_controller():continue
    view_team=1 if pc.player_state.get_team()==unreal.GuLiTeam.RED else 2
    registry=next(s for s in unreal.ObjectIterator(unreal.GuLiModelRegistrySubsystem) if s.get_world()==world)
    for actor in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.GuLiCommanderPresentationActor):
        if actor.get_editor_property('is_editor_only_actor'):continue
        for comp in actor.get_components_by_class(unreal.InstancedStaticMeshComponent):
            match=re.search('Type_5_Team_([12])',comp.get_name())
            if not match or not comp.get_instance_count():continue
            actual_team=int(match.group(1));relation='own' if actual_team==view_team else 'enemy'
            poses=[(i,comp.get_instance_transform(i,True)) for i in range(comp.get_instance_count())]
            poses=[p for p in poses if min(p[1].scale3d.to_tuple())>0 and max(abs(v) for v in p[1].translation.to_tuple())>1]
            if not poses:
                report['errors'].append('No visible pose: '+comp.get_path_name());continue
            index,pose=poses[0]
            center=pose.translation+unreal.Vector(0,0,60)
            camera=unreal.GuLiTeleportQALibrary.create_capture(world)
            try:
                position=center+unreal.Vector(700,700,650)
                camera.set_actor_location_and_rotation(position,unreal.MathLibrary.find_look_at_rotation(position,center),False,True)
                capture=camera.capture_component2d
                capture.capture_every_frame=False;capture.capture_on_movement=False
                capture.capture_source=unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR
                capture.projection_type=unreal.CameraProjectionMode.ORTHOGRAPHIC
                capture.ortho_width=700
                capture.primitive_render_mode=unreal.SceneCapturePrimitiveRenderMode.PRM_USE_SHOW_ONLY_LIST
                capture.show_only_component(comp)
                target=unreal.RenderingLibrary.create_render_target2d(world,1000,800,unreal.TextureRenderTargetFormat.RTF_RGBA8,unreal.LinearColor(.1,.1,.1,1),False,False)
                target.target_gamma=2.2;capture.texture_target=target
                capture.capture_scene()
                name=f'Live_PlayerTeam{view_team}_UnitTeam{actual_team}_{relation}.png'
                unreal.RenderingLibrary.export_render_target(world,target,str(IMAGES),name)
                color=registry.get_vector_parameter(comp,1005,'Root','*','TeamPrimary')
                report['captures'].append({'world':world.get_path_name(),'player_actual_team':view_team,
                    'unit_actual_team':actual_team,'relation':relation,'instance':index,'position':list(pose.translation.to_tuple()),
                    'mesh':comp.static_mesh.get_path_name(),'material':comp.get_material(0).get_path_name(),
                    'actual_team_primary':list(color.to_tuple()) if color else None,'file':'Images/'+name,
                    'rigid_wpo_and_animation_active':bool(comp.get_editor_property('evaluate_world_position_offset'))})
            finally:camera.destroy_actor()
report['success']=len(report['captures'])==4 and not report['errors']
(OUT/'runtime-actual-batch-captures.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report))
