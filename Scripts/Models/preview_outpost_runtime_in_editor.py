"""Explicit current-session inspection using the real commander camera and actors."""
import json
import math
from pathlib import Path
import unreal

OUT=Path(globals().get('GULI_OUTPOST_OUTPUT','D:/UE5.7/test1/ArtSource/Buildings/OutpostSymmetry_20261029/UE_Delivery_v1'))
CAPTURES=Path(globals().get('GULI_OUTPOST_CAPTURES','D:/UE5.7/test1/Artifacts/OutpostSymmetric20261029'))
CAPTURES.mkdir(parents=True,exist_ok=True)
session=json.loads((OUT/'pie-session.json').read_text(encoding='utf8'))
status=json.loads(unreal.get_editor_subsystem(unreal.load_class(None,'/Script/GuLiStrikeEditor.GuLiLocalPIESubsystem')).get_session_status())
assert status['active'] and status['room']==session['status']['room'],'Do not change another PIE session'
world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
pc=unreal.GameplayStatics.get_player_controller(world,0)
view=pc.get_view_target();camera=view.get_commander_camera();arm=camera.get_attach_parent()
def xyz(v):return list(v.to_tuple())
def rotation(r):return {'pitch':r.pitch,'yaw':r.yaw,'roll':r.roll}

action=globals().get('GULI_OUTPOST_ACTION','prepare')
territory=globals().get('GULI_OUTPOST_TERRITORY','Outpost_R4C4')
if action=='restore':
    saved=json.loads((OUT/'runtime-camera-before.json').read_text(encoding='utf8'))
    view.set_actor_location_and_rotation(unreal.Vector(*saved['location']),unreal.Rotator(**saved['rotation']),False,True)
    arm.set_editor_property('target_arm_length',saved['arm_length'])
    arm.set_relative_rotation(unreal.Rotator(**saved['arm_rotation']),False,True)
    camera.set_relative_location(unreal.Vector(*saved['camera_location']),False,True)
    camera.set_relative_rotation(unreal.Rotator(**saved['camera_rotation']),False,True)
    camera.set_field_of_view(saved['fov'])
    view.set_actor_tick_enabled(saved['tick'])
    for record in saved['widgets']:
        widget=unreal.find_object(None,record['path'])
        if widget:widget.set_visibility(getattr(unreal.SlateVisibility,record['visibility']))
    unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'camera_and_hud_restored':True}))
else:
    actor=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world,
        unreal.load_class(None,'/Script/GuLiStrike.GuLiTerritoryOutpostActor')) if str(a.get_territory_id())==territory)
    component=next(c for c in actor.get_components_by_class(unreal.StaticMeshComponent) if c.get_name()=='OutpostVisual')
    if action=='prepare':
        if not (OUT/'runtime-camera-before.json').exists():
            widgets=[w for w in unreal.ObjectIterator(unreal.UserWidget) if w.get_owning_player()==pc and w.is_in_viewport()]
            saved={'location':xyz(view.get_actor_location()),'rotation':rotation(view.get_actor_rotation()),
                'arm_length':arm.get_editor_property('target_arm_length'),'arm_rotation':rotation(arm.get_editor_property('relative_rotation')),
                'camera_location':xyz(camera.get_editor_property('relative_location')),'camera_rotation':rotation(camera.get_editor_property('relative_rotation')),
                'fov':camera.field_of_view,'tick':view.is_actor_tick_enabled(),'widgets':[]}
            for widget in widgets:
                visibility=widget.get_visibility()
                name=next(n for n in dir(unreal.SlateVisibility) if n.isupper() and getattr(unreal.SlateVisibility,n)==visibility)
                saved['widgets'].append({'path':widget.get_path_name(),'visibility':name})
                widget.set_visibility(unreal.SlateVisibility.COLLAPSED)
            (OUT/'runtime-camera-before.json').write_text(json.dumps(saved,ensure_ascii=False,indent=2),encoding='utf8')
        center=component.get_world_location()
        offset=unreal.Vector(0,9500 if territory.startswith('Outpost_R1') else -9500,1400)
        if globals().get('GULI_OUTPOST_FACE_OPENING',False):
            yaw=component.get_editor_property('RelativeRotation').yaw
            base=90. if territory.startswith('Outpost_R1') else -90.
            angle=math.radians(yaw+90.*round((base-yaw)/90.))
            offset=unreal.Vector(9500*math.cos(angle),9500*math.sin(angle),1400)
        location=center+offset
        look=unreal.MathLibrary.find_look_at_rotation(location,center)
        view.set_actor_tick_enabled(False)
        arm.set_editor_property('target_arm_length',0.)
        view.set_actor_location_and_rotation(location,unreal.Rotator(),False,True)
        arm.set_relative_rotation(look,False,True)
        camera.set_relative_location(unreal.Vector(),False,True)
        camera.set_relative_rotation(unreal.Rotator(),False,True)
        camera.set_field_of_view(45.)
        unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'territory':territory,'normal_commander_camera':True,'camera':xyz(location)}))
    elif action=='capture':
        bounds=component.static_mesh.get_bounds();transform=component.get_world_transform()
        corners=[transform.transform_location(bounds.origin+unreal.Vector(x*bounds.box_extent.x,y*bounds.box_extent.y,z*bounds.box_extent.z))
            for x in (-1,1) for y in (-1,1) for z in (-1,1)]
        bottom=min(p.z for p in corners);ground=actor.get_actor_location().z-500
        registry=next(s for s in unreal.ObjectIterator(unreal.GuLiModelRegistrySubsystem) if s.get_outer()==world)
        owned=registry.get_scalar_parameter(component,2003,'Root','*','CoreUseTeamColor')
        primary=registry.get_vector_parameter(component,2003,'Root','*','TeamPrimary')
        file=CAPTURES/(territory+'_InGame.png')
        assert unreal.GuLiModelAuthoringLibrary.capture_runtime_viewport(pc,str(file))
        report={'success':True,'territory':territory,'world':world.get_path_name(),'room':status['room'],
            'camera':xyz(camera.get_world_location()),'camera_rotation':rotation(camera.get_world_rotation()),
            'model':component.static_mesh.get_path_name(),'outline':component.get_overlay_material().get_path_name(),
            'model_yaw_deg':component.get_editor_property('RelativeRotation').yaw,
            'bottom_clearance_cm':bottom-ground,'core_use_team_color':owned,'team_primary_linear':xyz(primary),
            'core_display':'white' if owned==0 else 'team color','image':str(file),
            'hud_temporarily_hidden':True,'actual_gameplay_actor':True}
        (OUT/(territory+'_capture.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
        unreal.MCPythonHelper.submit_result(json.dumps(report))
    else:raise ValueError(action)
