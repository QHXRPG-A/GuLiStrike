"""Read only the delivered map entities and baked source data; never start gameplay."""
import json
import math
from pathlib import Path
import unreal


def main():
    root=Path(unreal.Paths.project_dir()).resolve()
    out=root/'Artifacts/Map4200/20260922'
    helpers={}
    exec((root/'Scripts/Map4200/capture_baseline.py').read_text(encoding='utf-8').rsplit('unreal.MCPythonHelper.submit_result',1)[0],helpers)
    value=helpers['value']
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    lands=[a for a in actors if isinstance(a,unreal.LandscapeProxy)]
    assert len(lands)==1
    info=unreal.LandscapeService.get_landscape_info(lands[0].get_name())
    assert info.num_components==256 and info.resolution_x==4081 and info.resolution_y==4081
    origin,extent=lands[0].get_actor_bounds(False)
    assert abs(origin.x)<1 and abs(origin.y)<1 and abs(extent.x-210000)<1 and abs(extent.y-210000)<1
    service=unreal.get_editor_subsystem(unreal.GuLiMapAuthoringSubsystem)
    snapshot=service.get_snapshot()
    density=service.get_density_snapshot()
    assert snapshot.success and density.success
    layout=json.loads(snapshot.json)
    density=json.loads(density.json)
    (out/'layout-after.json').write_text(json.dumps(layout,ensure_ascii=False,indent=2),encoding='utf-8')
    (out/'density-after.json').write_text(json.dumps(density,ensure_ascii=False,indent=2),encoding='utf-8')
    markers=layout['markers']
    assert len(markers)==49 and len({m['marker_id'] for m in markers})==49
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
    assert nav_check.success,nav_check.message
    assert definition.layout_version==3 and abs(definition.baked_object_scale-.2)<1e-5
    assert len(definition.territories)==49 and len(definition.clusters)==240 and len(definition.nodes)==6240
    assert sum(c.resource_type==unreal.GuLiResourceType.BLUE for c in definition.clusters)==200
    assert sum(c.resource_type==unreal.GuLiResourceType.RED for c in definition.clusters)==40
    assert len({n.get_editor_property('node_id') for n in definition.nodes})==6240
    assert definition.calculate_layout_hash()==definition.layout_hash
    clusters=list(definition.clusters)
    minimum_spacing=min(math.hypot(a.center.x-b.center.x,a.center.y-b.center.y)
                        for i,a in enumerate(clusters) for b in clusters[i+1:])
    assert minimum_spacing>=5500
    for a in clusters:
        assert sum(b.resource_type==a.resource_type and abs(a.center.x+b.center.x)<1 and abs(a.center.y+b.center.y)<1 for b in clusters)==1
    # Baked territory centers intentionally carry logical board XY and Z=0;
    # the corresponding marker actors, spawn anchors and resource nodes sit on terrain.
    entity_positions=[unreal.Vector(*m['world_transform']['position']) for m in markers]+[c.center for c in clusters]+[n.world_transform.translation for n in definition.nodes]
    anchors=definition.spawn_anchors
    entity_positions += [anchors.red_factory,anchors.red_assembly,anchors.blue_factory,anchors.blue_assembly]
    hits=unreal.LandscapeService.batch_line_trace([unreal.Vector(v.x,v.y,1000000) for v in entity_positions],
                                                [unreal.Vector(v.x,v.y,-1000000) for v in entity_positions])
    assert all(h.hit and h.actor_name.startswith('Landscape') for h in hits)
    maximum_slope=max(math.degrees(math.acos(max(-1,min(1,h.hit_normal.z)))) for h in hits)
    maximum_ground_error=max(abs(v.z-h.hit_location.z) for v,h in zip(entity_positions,hits))
    assert maximum_slope<=15 and maximum_ground_error<2,(maximum_slope,maximum_ground_error)
    assert all(abs(v.x)<=140000 and abs(v.y)<=140000 for v in entity_positions)
    for marker in markers:
        r,c=marker['parameters']['BoardRow'],marker['parameters']['BoardColumn']
        x,y,_=marker['world_transform']['position']
        assert abs(x-(c-4)*40000)<1 and abs(y-(4-r)*40000)<1
    nav=[]
    for actor in actors:
        if actor.get_class().get_name() in ('NavMeshBoundsVolume','GuLiFlightNavigationVolume','RecastNavMesh'):
            origin,extent=actor.get_actor_bounds(False)
            item={'path':actor.get_path_name(),'class':actor.get_class().get_name(),'origin':value(origin),'extent':value(extent)}
            names=('AgentRadius','AgentHeight','AgentMaxSlope','RuntimeGeneration','TileSizeUU','NavMeshResolutionParams',
                   'bDoFullyAsyncNavDataGathering','NavigationData','AuthoringBakeSettings')
            for name in names:
                try: item[name]=value(actor.get_editor_property(name))
                except Exception: pass
            nav.append(item)
    economy=unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy')
    result={'success':True,'map':world.get_path_name(),'actors':len(actors),'landscape':value(info),
            'unique_outposts':49,'old_marker_and_region_ids_preserved':25,'new_outposts':24,
            'resource':{'version':definition.layout_version,'source_hash':definition.source_hash,'layout_hash':definition.layout_hash,
                        'territories':49,'blue_clusters':200,'red_clusters':40,'nodes':6240,
                        'minimum_cluster_spacing_cm':minimum_spacing,'maximum_placement_slope_deg':maximum_slope,
                        'maximum_ground_error_cm':maximum_ground_error,'anchors':value(definition.spawn_anchors)},
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
    return {'success':True,'outposts':49,'components':256,'blue_clusters':200,'red_clusters':40,'nodes':6240,
            'maximum_placement_slope_deg':maximum_slope,'maximum_ground_error_cm':maximum_ground_error,
            'dirty_maps':result['dirty_maps'],'dirty_content':result['dirty_content']}


unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))
