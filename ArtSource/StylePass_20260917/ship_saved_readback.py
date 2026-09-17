"""Fresh-process readback of the saved player appearance; no package writes."""
import unreal,json,traceback
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/StylePass_20260917')
assert '-ShipSavedReadbackWorker' in unreal.SystemLibrary.get_command_line()
r={'success':False,'saved_packages':[]}
actor=None
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
try:
    bp=unreal.load_asset('/Game/GuLiStrike/Ship/BP_CombatAvatarFly01')
    old=unreal.load_asset('/Game/GuLiStrike/Ship/Stylized/Rollback/BP_CombatAvatarFly01_BeforeCel')
    cdo=unreal.get_default_object(bp.generated_class());prior=unreal.get_default_object(old.generated_class())
    hull=cdo.get_editor_property('HullMesh');old_hull=prior.get_editor_property('HullMesh')
    r['blueprint']=bp.get_path_name()
    r['original_hull']={'mesh':hull.static_mesh.get_path_name(),'visible':hull.is_visible(),'transform':str(hull.get_relative_transform()),'collision':str(hull.get_collision_enabled()),'socket_names':[str(x) for x in hull.get_all_socket_names()]}
    r['original_hull_mesh_preserved']=hull.static_mesh==old_hull.static_mesh
    r['original_hull_transform_preserved']=str(hull.get_relative_transform()).split('{',1)[1]==str(old_hull.get_relative_transform()).split('{',1)[1]
    r['collision_preserved']=hull.get_collision_enabled()==old_hull.get_collision_enabled()
    r['sockets_preserved']=list(hull.get_all_socket_names())==list(old_hull.get_all_socket_names())
    actor=actors.spawn_actor_from_class(bp.generated_class(),unreal.Vector(0,0,50000),unreal.Rotator(),transient=True)
    assert actor,'Cannot construct saved player Blueprint'
    cs={c.get_name():c for c in actor.get_components_by_class(unreal.StaticMeshComponent)}
    r['components']=[{'name':name,'mesh':c.static_mesh.get_path_name() if c.static_mesh else None,'visible':c.is_visible(),'collision':str(c.get_collision_enabled()),'parent':c.get_attach_parent().get_name() if c.get_attach_parent() else None,'relative_transform':str(c.get_relative_transform()),'world_scale':list(c.get_world_scale().to_tuple())} for name,c in cs.items()]
    new_hull=cs['AnimeHull'];contour=cs['AnimeContour'];native=actor.get_editor_property('HullMesh')
    r['checks']={'native_hull_hidden':not native.is_visible(),'anime_hull_visible':new_hull.is_visible(),'contour_visible':contour.is_visible(),
        'hull_parent_preserved':new_hull.get_attach_parent()==native,'contour_parent_preserved':contour.get_attach_parent()==native,
        'body_mesh_correct':new_hull.static_mesh.get_name()=='SM_Ship_AnimeHull','contour_mesh_correct':contour.static_mesh.get_name()=='SM_Ship_AnimeContour',
        'body_no_collision':new_hull.get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION,
        'contour_no_collision':contour.get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION,
        'contour_no_shadow':not contour.get_editor_property('cast_shadow'),
        'body_identity_scale':list(new_hull.get_editor_property('relative_scale3d').to_tuple())==[1.,1.,1.],
        'contour_identity_scale':list(contour.get_editor_property('relative_scale3d').to_tuple())==[1.,1.,1.]}
    r['body_uv_channels']=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_num_uv_channels(new_hull.static_mesh,0)
    r['success']=all(r['checks'].values()) and all(r[k] for k in ['original_hull_mesh_preserved','original_hull_transform_preserved','collision_preserved','sockets_preserved']) and r['body_uv_channels']==4
except Exception:r['error']=traceback.format_exc()
finally:
    if actor:actors.destroy_actor(actor)
    (OUT/'ship_saved_readback.json').write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf8')
    unreal.SystemLibrary.quit_editor()
