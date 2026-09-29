"""Add static disc-bottom metadata sockets, preserving accepted mesh geometry/materials/LODs."""
import unreal,json,traceback
from pathlib import Path
ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/WarMachineHover_20260929'
PATH='/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Rigid'
r={'success':False,'path':PATH}
try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    mesh=unreal.load_asset(PATH);assert mesh
    spec=json.loads((OUT/'nozzles.json').read_text())
    for item in spec['nozzles']:
        s=mesh.find_socket(item['name'])
        if not s:
            s=unreal.new_object(unreal.StaticMeshSocket,outer=mesh);s.set_editor_property('socket_name',item['name']);mesh.add_socket(s)
        s.set_editor_property('relative_location',unreal.Vector(*item['location_cm']))
        s.set_editor_property('relative_rotation',unreal.Rotator(-90,0,0))
        s.set_editor_property('relative_scale',unreal.Vector(item['diameter_cm'],1,1))
    # Conservative envelope includes full upper yaw, pitch/recoil, 120+12 game-cm hover and body sway.
    mesh.set_editor_property('positive_bounds_extension',unreal.Vector(950,600,1900))
    mesh.set_editor_property('negative_bounds_extension',unreal.Vector(950,600,1300))
    lib=unreal.EditorAssetLibrary
    lib.set_metadata_tag(mesh,'GuLi.Animation','RigidWPO.v2; 29 floats; four disc-bottom nozzles; no skeleton')
    lib.set_metadata_tag(mesh,'GuLi.HoverSockets','FX_Hover_FL/FR/RL/RR; scale.X=authored disc diameter; position scaled once')
    assert lib.save_loaded_asset(mesh,False)
    r['sockets']=[{'name':n['name'],'location':list(mesh.find_socket(n['name']).relative_location.to_tuple()),'diameter_cm':mesh.find_socket(n['name']).relative_scale.x} for n in spec['nozzles']]
    r['success']=True
except:r['error']=traceback.format_exc()
(OUT/'ue-nozzles.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(r))
