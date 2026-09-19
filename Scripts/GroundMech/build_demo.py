"""Create the requested environment copy and Ground-first player preset."""
import unreal,json,traceback,hashlib
from pathlib import Path
ROOT=Path(unreal.Paths.project_dir());OUT=ROOT/'TestResults/GroundMech'
SOURCE='/Game/StylizedPineEnvironment/Maps/Demo_Map'
TARGET='/Game/Maps/LVL_GroundMech_Demo'
BASE='/Game/GuLiStrike/GroundMech'
LIB=unreal.EditorAssetLibrary
LEVEL=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EDITOR=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
REPORT={'success':False,'source':SOURCE,'target':TARGET}

def run():
    source_file=ROOT/'Content/StylizedPineEnvironment/Maps/Demo_Map.umap'
    before=hashlib.sha256(source_file.read_bytes()).hexdigest()
    assert LIB.does_asset_exist(BASE+'/BP_GroundMech_Light')
    if not LIB.does_asset_exist(TARGET):
        assert LEVEL.load_level(SOURCE)
        assert unreal.EditorLoadingAndSavingUtils.save_map(EDITOR.get_editor_world(),TARGET)
    else:
        assert LEVEL.load_level(TARGET)
    for actor in ACTORS.get_all_level_actors():
        if isinstance(actor,unreal.PlayerStart) or 'GuLi.Scale020.Review.v1' in [str(t) for t in actor.tags] or 'GuLi.GroundMech.Demo.v1' in [str(t) for t in actor.tags]:ACTORS.destroy_actor(actor)
    world=EDITOR.get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode',LIB.load_blueprint_class(BASE+'/BP_GroundMech_DemoMode'))
    def ground(x,y):
        hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(x,y,100000),unreal.Vector(x,y,-100000),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,[],unreal.DrawDebugTrace.NONE,True)
        v=hit.to_tuple(); assert v[0] and not v[1],(x,y);return v[5]
    def tag(actor,label):
        actor.set_actor_label(label);actor.set_folder_path('GroundMech_Demo')
        actor.tags=[unreal.Name('GuLi.GroundMech.Demo.v1')];return actor
    starts=[]
    for i,(x,y) in enumerate([(17000,-4300),(17000,-2000)]):
        p=ground(x,y)
        a=tag(ACTORS.spawn_actor_from_class(unreal.PlayerStart,p+unreal.Vector(0,0,475),unreal.Rotator(yaw=0)),'GroundMech_Start_'+str(i+1))
        starts.append(list(a.get_actor_location().to_tuple()))
    wm=tag(ACTORS.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector()),'GroundMech_WarMachine_SizeReference')
    wm.static_mesh_component.set_static_mesh(unreal.load_asset('/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Cel'))
    wm.set_actor_scale3d(unreal.Vector(.2,.2,.2));wm.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    center,extent=wm.get_actor_bounds(False,True);p=ground(14800,-5400)
    wm.set_actor_location(unreal.Vector(p.x-center.x,p.y-center.y,p.z-center.z+extent.z+2),False,False)
    EDITOR.set_level_viewport_camera_info(unreal.Vector(14300,-7000,2600),unreal.Rotator(pitch=-28,yaw=42))
    assert LEVEL.save_current_level()
    REPORT.update(success=True,source_sha256=before,source_unchanged=before==hashlib.sha256(source_file.read_bytes()).hexdigest(),starts=starts,war_machine_height_cm=extent.z*2)
try:run()
except Exception:REPORT['error']=traceback.format_exc()
(OUT/'demo-map.json').write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
unreal.SystemLibrary.quit_editor()
