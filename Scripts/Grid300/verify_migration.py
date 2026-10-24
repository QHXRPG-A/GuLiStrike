"""Structured editor readback of saved Grid300 entities; no gameplay or visual testing."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
DATA=ROOT/'ArtSource/Navigation/Grid300'

def readback():
    result=unreal.GuLiGridAuthoringLibrary.read_migration_state()
    assert result[0],str(result)
    state=json.loads(result[1])
    terrain=json.loads((DATA/'Terrain/manifest.json').read_text(encoding='utf-8'))
    assert state['terrain_hash']==terrain['source_hash']
    expected={'cell_cm':300,'cells_x':567,'cells_y':567,'height_vertices':568*568,'landscapes':1,
              'landscape_components':81,'recast_actors':0,'territories':49,'clusters':240,'nodes':6240,'total_ore':9600,
              'player_capsule_diameter_cm':300}
    for key,value in expected.items():assert state[key]==value,(key,state[key],value)
    assert state['navigation_config']=='/Script/GuLiStrike.GuLiGridNavigationSystemConfig'
    dimensions=json.loads((DATA/'dimensions.json').read_text(encoding='utf-8'))
    for kind in ('units','buildings'):
        source={d['id']:d for d in dimensions[kind]};current={d['id']:d for d in state[kind]}
        assert set(source)==set(current),(kind,'catalog changed')
        for key,entry in source.items():
            assert current[key]['footprint_cells']==entry['footprint_cells']
            assert abs(current[key]['scale']-entry['scale'])<.0001
    from runpy import run_path
    helpers=run_path(str(ROOT/'Scripts/Grid300/apply_migration.py'),run_name='grid300_read_helpers')
    helpers['verify_source_fields'](final=True)
    same=helpers['same'];values=helpers['values']
    asset=unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap')
    prepared=json.loads((DATA/'layout_resource.json').read_text(encoding='utf-8'))
    for kind,fields in {'territories':('center','outpost_ground_location'),
                        'clusters':('center','obstacle_radius_centimeters','footprint_cells'),
                        'nodes':('world_transform','initial_amount')}.items():
        for actual,target in zip(asset.get_editor_property(kind),prepared[kind]):
            for field in fields:
                v=actual.get_editor_property(field)
                if field=='world_transform':v=values(v)
                elif field in ('center','outpost_ground_location'):v=list(v.to_tuple())
                assert same(v,target[field]),(kind,field)
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    flight=[]
    for actor in actors:
        if not isinstance(actor,unreal.GuLiFlightNavigationVolume):continue
        settings=actor.get_editor_property('authoring_bake_settings')
        assert unreal.GuLiFlightNavigationEditorLibrary.validate_volume(actor,settings) is not None
        asset=actor.get_editor_property('navigation_data');assert asset
        flight.append({'volume':actor.get_path_name(),'asset':asset.get_path_name(),'current_geometry_valid':True})
    state['flight_navigation']=flight
    by_path={a.get_path_name():a for a in actors}
    for row in json.loads((DATA/'layout_actors.json').read_text(encoding='utf-8')):
        assert row['path'] in by_path,row['path']
        assert same(values(by_path[row['path']].get_actor_transform()),row['target']),row['path']
    fixture=[a for a in actors if a.actor_has_tag(unreal.Name('Grid300Review20261022'))]
    spec=json.loads((DATA/'review.json').read_text(encoding='utf-8'))
    expected={('Grid300_'+r['id']):r['location'] for r in spec['targets']}
    expected['Grid300_Review']=expected['Grid300_Assembly']
    expected['Grid300_Launch200']=expected['Grid300_Assembly']
    assert len(fixture)==len(expected),'Missing or duplicate player review entries'
    assert {a.get_actor_label() for a in fixture}==set(expected)
    for actor in fixture:
        assert same(list(actor.get_actor_location().to_tuple()),expected[actor.get_actor_label()])
        assert actor.get_editor_property('is_editor_only_actor')
        label=actor.get_actor_label()
        cls=unreal.Note if label=='Grid300_Review' else unreal.GuLiHybridNetworkReviewActor if label=='Grid300_Launch200' else unreal.TargetPoint
        assert isinstance(actor,cls)
        if label=='Grid300_Launch200':
            assert actor.get_editor_property('units')==spec['launch_population'] and actor.get_editor_property('additional_flights')==0
            assert not actor.get_editor_property('preparation_error')
            path=actor.get_editor_property('manifest').get_editor_property('file_path')
            assert Path(path).resolve()==(DATA/'PlayerReview/manifest.json').resolve()
            validation=json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(path))
            assert validation['success'],validation
            assert '-GuLiManifest=' in actor.get_editor_property('launch_arguments')
    state['review_entities']=[{'path':a.get_path_name(),'label':a.get_actor_label(),
                              'class':a.get_class().get_name(),'location':list(a.get_actor_location().to_tuple())} for a in fixture]
    state['compiled_editor_readback']=True
    state['gameplay_run']=False;state['performance_measured']=False;state['player_visual_acceptance']='pending'
    (DATA/'editor_migration_readback.json').write_text(json.dumps(state,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return state

if __name__=='__main__':
    try:result={'success':True,'state':readback()}
    except Exception as exc:result={'success':False,'error':str(exc)}
    unreal.MCPythonHelper.submit_result(json.dumps(result,ensure_ascii=False))
