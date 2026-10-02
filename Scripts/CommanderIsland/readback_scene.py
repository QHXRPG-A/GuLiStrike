"""Read the saved commander map and actual baked entities without running gameplay."""
import json,math
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
FILES=ROOT/'ArtSource/Environment/GuLiStrike_CommanderIsland_1800m_v1'
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
WORLD=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert WORLD.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
assert not LEVELS.is_in_play_in_editor()

def point(p):return list(p.to_tuple())

def main():
    actors=ACTORS.get_all_level_actors()
    lands=[a for a in actors if isinstance(a,unreal.Landscape)]
    assert len(lands)==1
    land=lands[0];info=unreal.LandscapeService.get_landscape_info(land.get_actor_label())
    assert (info.resolution_x,info.resolution_y,info.num_components)==(4081,4081,256)
    center,extent=land.get_actor_bounds(False)
    assert abs(center.x)<1 and abs(center.y)<1 and abs(extent.x-90000)<1 and abs(extent.y-90000)<1
    layers={str(n) for n in land.get_target_layer_names()}
    assert layers=={'FlatGround','Hills','Plateau','Rock','Beach','Water'},layers
    service=unreal.get_editor_subsystem(unreal.GuLiMapAuthoringSubsystem)
    sr=service.get_snapshot();dr=service.get_density_snapshot()
    assert sr.success and dr.success,(str(sr.issues),str(dr.issues))
    snapshot=json.loads(sr.json);density=json.loads(dr.json)
    before=json.loads((FILES/'Before/editor_snapshot.json').read_text(encoding='utf-8'))
    patches=json.loads((FILES/'marker_migration.json').read_text(encoding='utf-8'))['patches']
    markers=[m for m in snapshot['markers'] if m['type_id']=='Outpost'];by_id={m['marker_id']:m for m in markers}
    assert len(markers)==49 and len(by_id)==49
    roads=[m for m in snapshot['markers'] if m['type_id']=='ResourceClearance']
    assert len(roads)==1 and roads[0]['marker_key']=='CommanderIsland_Roads'
    assert len(roads[0]['regions'])==185
    assert {m['marker_key'] for m in markers}=={p['marker_key'] for p in patches}
    for p in patches:
        m=by_id[p['marker_id']]
        assert sorted(r['region_id'] for r in m['regions'])==sorted(r['region_id'] for r in p['regions'])
    assert density['density_map_id']==before['density']['density_map_id'] and density['cell_size_cm']==2500
    definition=unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap')
    assert definition.layout_version==6 and len(definition.territories)==49
    assert len(definition.clusters)==240 and len(definition.nodes)==6240
    assert len({n.get_editor_property('node_id') for n in definition.nodes})==6240
    assert sum(c.resource_type==unreal.GuLiResourceType.BLUE for c in definition.clusters)==200
    assert sum(c.resource_type==unreal.GuLiResourceType.RED for c in definition.clusters)==40
    assert definition.calculate_layout_hash()==definition.layout_hash
    resource=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    assert resource.success,str(resource.issues)
    assert resource.initial_soldier_count==500 and resource.validated_initial_soldier_count==500
    nav=unreal.GuLiNavigationBakeLibrary.validate_world_navigation(WORLD)
    assert nav.success,nav.message
    path_queries=[]
    query_budgets=[]
    for nav_actor in [a for a in actors if isinstance(a,unreal.RecastNavMesh)]:
        budget=unreal.ActorService.get_property(nav_actor.get_name(),'DefaultMaxSearchNodes')
        hierarchical=unreal.ActorService.get_property(nav_actor.get_name(),'DefaultMaxHierarchicalSearchNodes')
        assert float(budget)==16384 and float(hierarchical)==16384,(budget,hierarchical)
        query_budgets.append({'agent':nav_actor.get_actor_label(),'maximum_search_nodes':float(budget),
            'maximum_hierarchical_search_nodes':float(hierarchical)})
        start=definition.spawn_anchors.red_assembly
        targets=[(str(t.territory_id),t.outpost_ground_location) for t in definition.territories]
        targets += [('RedBase',definition.spawn_anchors.red_factory),('BlueBase',definition.spawn_anchors.blue_factory),('BlueAssembly',definition.spawn_anchors.blue_assembly)]
        for key,end in targets:
            path=unreal.NavigationSystemV1.find_path_to_location_synchronously(WORLD,start,end,nav_actor)
            assert path.is_valid() and not path.is_partial(),(nav_actor.get_actor_label(),key)
            assert math.hypot(path.path_points[-1].x-end.x,path.path_points[-1].y-end.y)<2500,(key,'Projected endpoint mismatch')
            path_queries.append({'agent':nav_actor.get_actor_label(),'destination':key,'complete':True,'path_points':len(path.path_points)})
    assert len(path_queries)==104
    territories=[]
    for t in definition.territories:
        actual=t.outpost_ground_location;logical=t.center
        assert abs(actual.x-logical.x)<90000/7 and abs(actual.y-logical.y)<90000/7
        assert actual.z>0
        territories.append({'key':str(t.territory_id),'logical_center_cm':point(logical),'outpost_ground_cm':point(actual),
            'initial_owner':str(t.initial_owner),'blue_clusters':t.blue_cluster_budget,'red_clusters':t.red_cluster_budget})
    anchors=definition.spawn_anchors
    for name,y in [('red_factory',74000),('blue_factory',-74000),('red_assembly',69000),('blue_assembly',-69000)]:
        p=getattr(anchors,name);assert abs(p.x)<1 and abs(p.y-y)<1 and p.z>0
    positions=[t.outpost_ground_location for t in definition.territories]+[c.center for c in definition.clusters]
    positions += [n.world_transform.translation for n in definition.nodes]
    positions += [getattr(anchors,n) for n in ('red_factory','blue_factory','red_assembly','blue_assembly')]
    hits=unreal.LandscapeService.batch_line_trace([unreal.Vector(p.x,p.y,1000000) for p in positions],
        [unreal.Vector(p.x,p.y,-1000000) for p in positions])
    assert len(hits)==len(positions) and all(h.hit and h.actor_name.startswith('Landscape') for h in hits)
    slope=max(math.degrees(math.acos(max(-1,min(1,h.hit_normal.z)))) for h in hits)
    error=max(abs(p.z-h.hit_location.z) for p,h in zip(positions,hits))
    assert slope<=15 and error<2,(slope,error)
    clusters=list(definition.clusters)
    spacing=min(math.hypot(a.center.x-b.center.x,a.center.y-b.center.y) for i,a in enumerate(clusters) for b in clusters[i+1:])
    assert spacing>=5500,spacing
    for c in clusters:
        assert sum(b.resource_type==c.resource_type and abs(c.center.x+b.center.x)<1 and abs(c.center.y+b.center.y)<1 for b in clusters)==1
        assert all(math.hypot(c.center.x-t.outpost_ground_location.x,c.center.y-t.outpost_ground_location.y)>=6500 for t in definition.territories)
    water=[a for a in actors if isinstance(a,unreal.NavModifierVolume) and 'GuLi.CommanderIsland.Water' in [str(t) for t in a.tags]]
    expected=json.loads((FILES/'UEImport/water_navigation_boxes.json').read_text(encoding='utf-8'))
    assert len(water)==len(expected['boxes']) and all(a.get_editor_property('area_class').get_path_name()=='/Script/NavigationSystem.NavArea_Null' for a in water)
    poses=[a for a in actors if 'GuLi.CommanderIsland.EditorPose' in [str(t) for t in a.tags]]
    assert len(poses)==49
    assert all(a.get_editor_property('is_editor_only_actor') and a.get_editor_property('hidden') and not a.get_actor_enable_collision() for a in poses)
    mode=WORLD.get_world_settings().get_editor_property('default_game_mode')
    assert mode.get_name()=='GuLiCommanderGameMode'
    controller=unreal.get_default_object(mode).get_editor_property('player_controller_class')
    assert controller.get_name()=='GuLiCommanderPlayerController'
    settings=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrike.GuLiOutpostPresentationSettings'))
    config={n:settings.get_editor_property(n) for n in ('Mesh','BodyMaterial','HaloMaterial','ModelHeightCm','FloatAmplitudeCm','FloatPeriodSeconds','LandingSeconds')}
    presentation_error=unreal.GuLiCommanderIslandAuthoringLibrary.validate_outpost_presentation_assets()
    assert not presentation_error,presentation_error
    mesh=config['Mesh'];b=mesh.get_bounds();scale=config['ModelHeightCm']/(2*b.box_extent.z)
    curve=[{'seconds':s,'height_m':config['FloatAmplitudeCm']/100*(1-math.cos(2*math.pi*s/config['FloatPeriodSeconds']))} for s in (0,1.25,2.5,3.75,5)]
    army=json.loads(unreal.GuLiResourceAuthoringLibrary.get_initial_army_spawn_layout_json())
    assert len(army['slots'])==500
    notes=[{'label':a.get_actor_label(),'location_cm':point(a.get_actor_location()),'text':a.get_editor_property('text')} for a in actors if isinstance(a,unreal.Note)]
    native_outpost=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrike.GuLiTerritoryOutpostActor'))
    collider=native_outpost.get_editor_property('GroundCollision')
    presenter=native_outpost.get_editor_property('Presentation')
    assert presenter.get_class().get_name()=='GuLiOutpostPresentationComponent'
    assert tuple(collider.get_unscaled_box_extent().to_tuple())==(300.0,300.0,500.0)
    result={'success':True,'map':WORLD.get_path_name(),'game_mode':mode.get_path_name(),'controller':controller.get_path_name(),
        'physical_size_m':[1800,1800],'resolution':[4081,4081],'terrain_layers':sorted(layers),'territories':territories,
        'stable_marker_ids_kept':49,'marker_ids_removed':32,'density_identity_preserved':True,'layout_version':6,
        'resource_clearance':{'marker_id':roads[0]['marker_id'],'regions':len(roads[0]['regions'])},
        'resource':{'blue_clusters':200,'red_clusters':40,'nodes':6240,'source_hash':definition.source_hash,'layout_hash':definition.layout_hash,
            'minimum_cluster_spacing_cm':spacing,'maximum_placement_slope_degrees':slope,'maximum_ground_error_cm':error},
        'initial_army':{'slots':500,'validated_slots':resource.validated_initial_soldier_count},'water_nav_boxes':len(water),
        'editor_navigation_queries':path_queries,'editor_navigation_query_budgets':query_budgets,
        'saved_navigation_entries':[{k:getattr(e,k) for k in ('object_path','kind','status','source_hash','message')} for e in nav.entries],
        'native_outpost':{'presenter_class':presenter.get_class().get_path_name(),
            'fixed_collider_extent_cm':point(collider.get_unscaled_box_extent()),'collision':str(collider.get_collision_enabled())},
        'presentation':{'mesh':mesh.get_path_name(),'body':config['BodyMaterial'].get_path_name(),'halo':config['HaloMaterial'].get_path_name(),
            'model_height_cm':config['ModelHeightCm'],'scale':scale,'curve':curve,'landing_seconds':config['LandingSeconds']},
        'notes':notes,'editor_pose_count':len(poses),'editor_poses_hidden_in_game':True,
        'dirty_maps':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
        'dirty_content':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
        'pie_run':False,'automated_tests_run':False,'runtime_animation_and_replication':'Awaiting player acceptance'}
    assert not result['dirty_maps'] and not result['dirty_content'],(result['dirty_maps'],result['dirty_content'])
    (FILES/'EditorEvidence/saved_scene_entities.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    for name,data in [('layout_after',snapshot),('density_after',density),('initial_army_after',army)]:
        (FILES/'EditorEvidence'/f'{name}.json').write_text(json.dumps(data,ensure_ascii=False,indent=2),encoding='utf-8')
    return {'success':True,'map':result['map'],'outposts':49,'blue_clusters':200,'red_clusters':40,'nodes':6240,
        'validated_army_slots':500,'water_nav_boxes':len(water),'maximum_ground_error_cm':error,
        'dirty_maps':result['dirty_maps'],'dirty_content':result['dirty_content'],'evidence':'saved_scene_entities.json'}

unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))
