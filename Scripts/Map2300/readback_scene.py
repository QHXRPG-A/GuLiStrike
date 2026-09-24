"""Read only the delivered map entities and baked source data; never start gameplay."""
import json
import math
from pathlib import Path
import unreal


def main():
    root=Path(unreal.Paths.project_dir()).resolve()
    out=root/'Artifacts/Map2300/20260923'
    helpers={}
    exec((root/'Scripts/Map2300/capture_baseline.py').read_text(encoding='utf-8').rsplit('unreal.MCPythonHelper.submit_result',1)[0],helpers)
    value=helpers['value']
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    lands=[a for a in actors if isinstance(a,unreal.LandscapeProxy)]
    assert len(lands)==1
    info=unreal.LandscapeService.get_landscape_info(lands[0].get_name())
    assert info.num_components==256 and info.resolution_x==4081 and info.resolution_y==4081
    assert len(lands[0].get_editor_property('target_layers'))==3
    assert {layer.layer_name for layer in info.layers}=={'Base_Layer','Layer_02','Layer_03'}
    origin,extent=lands[0].get_actor_bounds(False)
    assert abs(origin.x)<1 and abs(origin.y)<1 and abs(extent.x-115000)<1 and abs(extent.y-115000)<1
    service=unreal.get_editor_subsystem(unreal.GuLiMapAuthoringSubsystem)
    snapshot=service.get_snapshot()
    density=service.get_density_snapshot()
    assert snapshot.success and density.success
    layout=json.loads(snapshot.json)
    density=json.loads(density.json)
    (out/'layout-after.json').write_text(json.dumps(layout,ensure_ascii=False,indent=2),encoding='utf-8')
    (out/'density-after.json').write_text(json.dumps(density,ensure_ascii=False,indent=2),encoding='utf-8')
    markers=layout['markers']
    assert len(markers)==81 and len({m['marker_id'] for m in markers})==81
    by_id={m['marker_id']:m for m in markers}
    for record in json.loads((out/'marker-migration-identities.json').read_text()):
        current=by_id[record['marker_id']]
        assert current['marker_key']==record['new_key']
        assert sorted(r['region_id'] for r in current['regions'])==sorted(record['region_ids'])
    assert density['schema_version']==1 and density['cell_size_cm']==2500 and not density['anomalies']
    definition=unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap')
    resource_check=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    nav_check=unreal.GuLiNavigationBakeLibrary.validate_world_navigation(world)
    assert resource_check.success,str(resource_check.issues)
    assert resource_check.initial_soldier_count==500 and resource_check.validated_initial_soldier_count==500
    assert nav_check.success,nav_check.message
    army=json.loads(unreal.GuLiResourceAuthoringLibrary.get_initial_army_spawn_layout_json())
    assert len(army['slots'])==500
    assert sorted(sum(s['team']==team for s in army['slots']) for team in {s['team'] for s in army['slots']})==[250,250]
    (out/'initial-army-layout-after.json').write_text(json.dumps(army,ensure_ascii=False,indent=2),encoding='utf-8')
    mode=world.get_world_settings().get_editor_property('default_game_mode')
    assert mode and mode.get_name()=='GuLiCommanderGameMode',str(mode)
    controller=unreal.get_default_object(mode).get_editor_property('player_controller_class')
    assert controller and controller.get_name()=='GuLiCommanderPlayerController',str(controller)
    assert definition.layout_version==5 and abs(definition.baked_object_scale-.2)<1e-5
    assert len(definition.territories)==81 and len(definition.clusters)==240 and len(definition.nodes)==6240
    assert sum(c.resource_type==unreal.GuLiResourceType.BLUE for c in definition.clusters)==200
    assert sum(c.resource_type==unreal.GuLiResourceType.RED for c in definition.clusters)==40
    center_clusters=[c for c in definition.clusters if c.territory_index==40]
    assert sum(c.resource_type==unreal.GuLiResourceType.BLUE for c in center_clusters)==6
    assert sum(c.resource_type==unreal.GuLiResourceType.RED for c in center_clusters)==4
    assert len({n.get_editor_property('node_id') for n in definition.nodes})==6240
    assert definition.calculate_layout_hash()==definition.layout_hash
    clusters=list(definition.clusters)
    minimum_spacing=min(math.hypot(a.center.x-b.center.x,a.center.y-b.center.y)
                        for i,a in enumerate(clusters) for b in clusters[i+1:])
    assert minimum_spacing>=5500
    for a in clusters:
        assert sum(b.resource_type==a.resource_type and abs(a.center.x+b.center.x)<1 and abs(a.center.y+b.center.y)<1 for b in clusters)==1
        t=definition.territories[a.territory_index]
        dx,dy=a.center.x-t.center.x,a.center.y-t.center.y
        assert abs(dx)<=8750 and abs(dy)<=8750 and math.hypot(dx,dy)>=6500
        assert abs(dx)-a.obstacle_radius_centimeters>=2500 and abs(dy)-a.obstacle_radius_centimeters>=2500
    # Baked territory centers intentionally carry logical board XY and Z=0;
    # the corresponding marker actors, spawn anchors and resource nodes sit on terrain.
    entity_positions=[unreal.Vector(*m['world_transform']['position']) for m in markers]+[c.center for c in clusters]+[n.world_transform.translation for n in definition.nodes]
    anchors=definition.spawn_anchors
    for name,y in [('red_factory',87500),('red_assembly',72500),('blue_factory',-87500),('blue_assembly',-72500)]:
        point=getattr(anchors,name)
        assert abs(point.x)<1 and abs(point.y-y)<1
    entity_positions += [anchors.red_factory,anchors.red_assembly,anchors.blue_factory,anchors.blue_assembly]
    hits=unreal.LandscapeService.batch_line_trace([unreal.Vector(v.x,v.y,1000000) for v in entity_positions],
                                                [unreal.Vector(v.x,v.y,-1000000) for v in entity_positions])
    assert all(h.hit and h.actor_name.startswith('Landscape') for h in hits)
    maximum_slope=max(math.degrees(math.acos(max(-1,min(1,h.hit_normal.z)))) for h in hits)
    maximum_ground_error=max(abs(v.z-h.hit_location.z) for v,h in zip(entity_positions,hits))
    assert maximum_slope<=15 and maximum_ground_error<2,(maximum_slope,maximum_ground_error)
    assert all(abs(v.x)<=90000 and abs(v.y)<=90000 for v in entity_positions)
    for marker in markers:
        r,c=marker['parameters']['BoardRow'],marker['parameters']['BoardColumn']
        x,y,_=marker['world_transform']['position']
        assert abs(x-(c-5)*20000)<1 and abs(y-(5-r)*20000)<1
    nav=[]
    for actor in actors:
        if actor.get_class().get_name() in ('NavMeshBoundsVolume','GuLiFlightNavigationVolume','RecastNavMesh'):
            origin,extent=actor.get_actor_bounds(False)
            item={'path':actor.get_path_name(),'class':actor.get_class().get_name(),'origin':value(origin),'extent':value(extent)}
            names=('AgentRadius','AgentHeight','AgentMaxSlope','RuntimeGeneration','TileSizeUU','NavMeshResolutionParams',
                   'bDoFullyAsyncNavDataGathering','AuthoringBakeSettings')
            for name in names:
                try: item[name]=value(actor.get_editor_property(name))
                except Exception: pass
            if isinstance(actor,unreal.GuLiFlightNavigationVolume):
                assert abs(extent.x-167300)<1 and abs(extent.y-167300)<1
                data=actor.get_editor_property('navigation_data')
                item['navigation_data']=data.get_path_name()
                item['graph_counts']={key:len(data.get_editor_property(key)) for key in ('nodes','cells','portals','links')}
            if isinstance(actor,unreal.NavMeshBoundsVolume):
                assert abs(extent.x-90000)<1 and abs(extent.y-90000)<1
            nav.append(item)
    economy=unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy')
    result={'success':True,'map':world.get_path_name(),'actors':len(actors),'landscape':value(info),
            'game_mode':mode.get_path_name(),'player_controller':controller.get_path_name(),
            'unique_outposts':81,'old_marker_and_region_ids_preserved':81,'new_outposts':0,
            'resource':{'version':definition.layout_version,'source_hash':definition.source_hash,'layout_hash':definition.layout_hash,
                        'territories':81,'blue_clusters':200,'red_clusters':40,'nodes':6240,
                        'center_blue_clusters':6,'center_red_clusters':4,'road_clear_width_cm':5000,
                        'minimum_cluster_spacing_cm':minimum_spacing,'maximum_placement_slope_deg':maximum_slope,
                        'maximum_ground_error_cm':maximum_ground_error,'anchors':value(definition.spawn_anchors)},
            'initial_army':{'expected':resource_check.initial_soldier_count,'validated':resource_check.validated_initial_soldier_count},
            'navigation':nav,'navigation_validation':value(nav_check),
            'economy':helpers['properties'](economy),
            'notes':[{'label':a.get_actor_label(),'location':value(a.get_actor_location()),'text':a.get_editor_property('text')}
                     for a in actors if isinstance(a,unreal.Note)],
            'dirty_maps':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
            'dirty_content':[p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
            'pie_run':False,'memory_benefit_measured':False}
    (out/'entities-after-save.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    baked={'territories':value(definition.territories),'clusters':value(definition.clusters),'nodes':value(definition.nodes)}
    (out/'baked-layout-after.json').write_text(json.dumps(baked,ensure_ascii=False,separators=(',',':')),encoding='utf-8')
    return {'success':True,'outposts':81,'components':256,'blue_clusters':200,'red_clusters':40,'nodes':6240,
            'maximum_placement_slope_deg':maximum_slope,'maximum_ground_error_cm':maximum_ground_error,
            'dirty_maps':result['dirty_maps'],'dirty_content':result['dirty_content']}


unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))
