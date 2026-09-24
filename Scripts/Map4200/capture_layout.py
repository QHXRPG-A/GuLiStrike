"""Capture terrain/layout review views in the editor with temporary render targets."""
import json
import math
from pathlib import Path
import unreal


def main():
    out=Path(unreal.Paths.project_dir()).resolve()/'Artifacts/Map4200/20260922/views'
    out.mkdir(parents=True,exist_ok=True)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    land=next(a for a in actors if isinstance(a,unreal.Landscape))
    def ground(x,y):
        p=unreal.LandscapeService.get_height_at_location(land.get_name(),x,y)
        assert p.valid
        return unreal.Vector(x,y,p.height)
    assembly=ground(0,110000)
    central=ground(0,0)
    camera=unreal.EditorLevelLibrary.spawn_actor_from_class(unreal.SceneCapture2D,unreal.Vector(0,0,500000),transient=True)
    assert camera
    camera.set_editor_property('is_editor_only_actor',True)
    frames=[]
    try:
        capture=camera.capture_component2d
        capture.set_editor_property('capture_every_frame',False)
        capture.set_editor_property('capture_on_movement',False)
        capture.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
        target=unreal.RenderingLibrary.create_render_target2d(world,1400,1000,unreal.TextureRenderTargetFormat.RTF_RGBA8,
            unreal.LinearColor(0,0,0,1),False,False)
        capture.set_editor_property('texture_target',target)
        cases=[('overview',unreal.Vector(0,0,650000),unreal.Vector(),60),
               ('central_ramps',ground(60000,65000)+unreal.Vector(0,0,18000),central,60),
               ('ground_north_ramp',ground(0,65000)+unreal.Vector(0,0,450),central+unreal.Vector(0,0,250),60),
               ('commander_red_base',assembly+unreal.Vector(0,math.cos(math.radians(55))*16000,math.sin(math.radians(55))*16000),assembly,45)]
        for sx,sy,name in [(-1,1,'northwest'),(1,1,'northeast'),(-1,-1,'southwest'),(1,-1,'southeast')]:
            p=ground(sx*120000,sy*120000)
            cases.append(('corner_'+name,p+unreal.Vector(sx*18000,sy*18000,22000),p,55))
        for name,location,look,fov in cases:
            camera.set_actor_location_and_rotation(location,unreal.MathLibrary.find_look_at_rotation(location,look),False,True)
            capture.set_editor_property('fov_angle',fov)
            capture.capture_scene()
            unreal.RenderingLibrary.export_render_target(world,target,str(out),name+'.png')
            frames.append({'name':name,'file':str(out/(name+'.png')),'location':list(location.to_tuple()),
                           'look_at':list(look.to_tuple()),'fov':fov})
    finally:
        unreal.EditorLevelLibrary.destroy_actor(camera)
    result={'success':True,'frames':frames,'temporary_capture_removed':True,'pie_run':False,
            'commander_pose_source':'GuLiCommanderCameraPawn: 16000 cm arm, pitch -55 degrees, FOV 45 degrees'}
    (out/'capture-manifest.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    return {'success':True,'frames':len(frames),'directory':str(out)}


unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))
