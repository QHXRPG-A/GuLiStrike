"""Read only the delivered outpost entities/assets after saving and ending PIE."""
import json
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'ArtSource/Buildings/OutpostSymmetry_20261029/UE_Delivery_v1'
ed=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert ed.get_game_world() is None
layout=json.loads(unreal.GuLiCommanderIslandAuthoringLibrary.get_outpost_presentation_layout_json())
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
poses=[a for a in actors if a.get_actor_label().startswith('EditorPose_Outpost_')]
markers={a.get_actor_label():a for a in actors if a.get_class().get_name()=='GuLiMapMarker' and a.get_actor_label().startswith('Outpost_R')}
assert len(poses)==49
entities=[]
for actor in poses:
    c=actor.static_mesh_component;origin,extent=actor.get_actor_bounds(False)
    clearance=origin.z-extent.z-markers[actor.get_actor_label().removeprefix('EditorPose_')].get_actor_location().z
    assert c.static_mesh.get_path_name()==layout['mesh']
    assert abs(clearance-500)<.01
    assert actor.get_editor_property('hidden') and actor.get_editor_property('is_editor_only_actor')
    assert c.get_overlay_material()
    entities.append({'label':actor.get_actor_label(),'size_cm':list((extent*2).to_tuple()),'bottom_clearance_cm':clearance,
        'mesh':c.static_mesh.get_path_name(),'overlay':c.get_overlay_material().get_path_name()})
mesh=unreal.load_object(None,layout['mesh'])
snapshot=json.loads(unreal.GuLiModelAuthoringLibrary.get_mesh_invariant_snapshot(mesh))
assert snapshot['render_lods'][0]['triangles']==4824
retirement=json.loads((OUT/'ue-model-retirement.json').read_text(encoding='utf8'))
deleted={p:not unreal.EditorAssetLibrary.does_asset_exist(p) for p in retirement['deleted']}
assert all(deleted.values())
settings=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrike.GuLiOutpostPresentationSettings'))
status=json.loads(unreal.get_editor_subsystem(unreal.load_class(None,'/Script/GuLiStrikeEditor.GuLiLocalPIESubsystem')).get_session_status())
assert not status['active'] and not status['logic_running'] and not status['coordinator_running']
cpd_component=poses[0].static_mesh_component
bindings={name:cpd_component.get_custom_primitive_data_index_for_scalar_parameter(name) for name in
    ['GuLi_TeamEnabled','GuLi_TeamLightStrength','GuLi_CoreUseTeamColor']}
assert bindings=={'GuLi_TeamEnabled':16,'GuLi_TeamLightStrength':17,'GuLi_CoreUseTeamColor':18}
result={'success':True,'map':ed.get_editor_world().get_path_name(),'layout':layout,'outpost_count':len(poses),
    'entities':entities,'snapshot':snapshot,'deleted_models':deleted,
    'old_gallery_absent':not any(a.get_actor_label()=='ModelRegistry_Mass_ManualOutpost' for a in actors),
    'outline_default':settings.get_editor_property('OutlineMaterial').get_path_name(),'cpd_bindings':bindings,
    'pie_cleanup':status,'saved_map_clean':not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(),
    'saved_content_clean':not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages(),
    'player_visual_acceptance':'pending','automated_tests_run':False}
assert result['old_gallery_absent'] and result['saved_map_clean'] and result['saved_content_clean']
(OUT/'ue-production-delivery.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'outposts':len(poses),'triangles':4824,'deleted_models':deleted,
    'saved_and_clean':True,'pie_ended_and_helpers_stopped':True}))
