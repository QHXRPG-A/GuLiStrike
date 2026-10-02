"""Apply the explicit 81-to-49 migration and use the existing resource baker."""
import json
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.project_dir()).resolve()
FILES=ROOT/'ArtSource/Environment/GuLiStrike_CommanderIsland_1800m_v1'
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
SERVICE=unreal.get_editor_subsystem(unreal.GuLiMapAuthoringSubsystem)
WORLD=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert WORLD.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
assert not LEVELS.is_in_play_in_editor()

def snapshot():
    result=SERVICE.get_snapshot()
    assert result.success,str(result.issues)
    return json.loads(result.json)

def normalize_id(value):
    return str(value).replace('-','').lower()

def configure_type_ranges(current):
    schema=next(t for t in current['types'] if t['type_id']=='Outpost')
    fields={normalize_id(f['field_id']):f['field_key'] for f in schema['fields']}
    type_asset=unreal.load_asset(schema['asset_path'])
    rules=list(type_asset.get_editor_property('field_rules'))
    limits={'BoardRow':7,'BoardColumn':7,'BlueClusterBudget':8,'RedClusterBudget':6}
    updated=[]
    for rule in rules:
        key=fields[normalize_id(rule.get_editor_property('field_id').to_string())]
        if key in limits:
            rule.set_editor_property('maximum',limits[key]);updated.append(key)
    assert set(updated)==set(limits)
    type_asset.set_editor_property('field_rules',rules)

def migrate():
    baseline=json.loads((FILES/'Before/editor_snapshot.json').read_text(encoding='utf-8'))
    migration=json.loads((FILES/'marker_migration.json').read_text(encoding='utf-8'))
    before=snapshot()
    expected=baseline['layout']['markers'] if ISLAND_ACTION=='migrate' else migration['patches']
    outposts=[m for m in before['markers'] if m['type_id']=='Outpost']
    assert {m['marker_id'] for m in outposts}=={m['marker_id'] for m in expected}
    actors={normalize_id(a.get_editor_property('record').get_editor_property('marker_id').to_string()):a for a in ACTORS.get_all_level_actors() if isinstance(a,unreal.GuLiMapMarker)}
    outpost_ids={normalize_id(m['marker_id']) for m in outposts}
    actors={key:actor for key,actor in actors.items() if key in outpost_ids}
    assert len(actors)==(81 if ISLAND_ACTION=='migrate' else 49)
    with unreal.ScopedEditorTransaction('Migrate commander island to 49 stable outposts'):
        if ISLAND_ACTION=='migrate':
            for m in migration['removed']:
                assert ACTORS.destroy_actor(actors[normalize_id(m['marker_id'])])
        configure_type_ranges(snapshot())
        for patch in migration['patches']:
            result=SERVICE.update_marker(patch['marker_id'],json.dumps(patch,ensure_ascii=False))
            assert result.success,(patch['marker_key'],str(result.issues))
        density=json.loads((FILES/'density_migration.json').read_text(encoding='utf-8'))
        current=SERVICE.get_density_snapshot();assert current.success,str(current.issues)
        current=json.loads(current.json)
        assert current['density_map_id']==density['density_map_id'] and current['cell_size_cm']==density['cell_size_cm']
        for patch in density['patches']:
            result=SERVICE.update_density_cells(json.dumps(patch))
            assert result.success,str(result.issues)
    after=snapshot()
    assert len([m for m in after['markers'] if m['type_id']=='Outpost'])==49
    by_id={m['marker_id']:m for m in after['markers']}
    identities=[]
    for patch in migration['patches']:
        m=by_id[patch['marker_id']]
        assert m['marker_key']==patch['marker_key']
        assert sorted(r['region_id'] for r in m['regions'])==sorted(r['region_id'] for r in patch['regions'])
        identities.append({'marker_id':m['marker_id'],'marker_key':m['marker_key'],'region_ids':[r['region_id'] for r in m['regions']]})
    (FILES/'EditorEvidence/migration_identities.json').write_text(json.dumps(identities,indent=2),encoding='utf-8')
    return {'success':True,'markers_kept':49,'markers_removed':32,'marker_and_region_ids_preserved':True,
            'density_map_id':density['density_map_id'],'cell_size_cm':density['cell_size_cm']}

def density():
    data=json.loads((FILES/'density_migration.json').read_text(encoding='utf-8'))
    current=SERVICE.get_density_snapshot();assert current.success,str(current.issues)
    current=json.loads(current.json)
    assert current['density_map_id']==data['density_map_id'] and current['cell_size_cm']==data['cell_size_cm']
    for patch in data['patches']:
        result=SERVICE.update_density_cells(json.dumps(patch));assert result.success,str(result.issues)
    return {'success':True,'layers':[p['layer_key'] for p in data['patches']]}

def bake():
    result=unreal.GuLiResourceAuthoringLibrary.prepare_canonical_authoring_and_bake(False,True)
    return {**{n:getattr(result,n) for n in ('success','message','source_hash','layout_hash','territory_count','cluster_count','node_count','initial_soldier_count','validated_initial_soldier_count')},'issues':list(result.issues)}

def validate():
    result=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    return {**{n:getattr(result,n) for n in ('success','message','source_hash','layout_hash','territory_count','cluster_count','node_count','initial_soldier_count','validated_initial_soldier_count')},'issues':list(result.issues)}

unreal.MCPythonHelper.submit_result(json.dumps({'migrate':migrate,'resume_markers':migrate,'density':density,'bake':bake,'validate':validate}[ISLAND_ACTION](),ensure_ascii=False))
