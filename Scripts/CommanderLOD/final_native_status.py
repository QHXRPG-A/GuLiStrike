"""Confirm loaded formal table references, vehicle components and saved map status."""
import json
from pathlib import Path
import unreal

ART = Path('D:/UE5.7/test1/ArtSource/CommanderLOD_20261005')
delivery = json.loads((ART / 'formal_delivery.json').read_text(encoding='utf8'))
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not editor.get_game_world()
assert editor.get_editor_world().get_path_name().split('.')[0] == delivery['review_map']
dt = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeCommander_Soldiers')
rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt))
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
vehicles = []
for group in delivery['groups']:
    row = next(r for r in rows if r['Id'] == group['id'])
    for key,expected in [('ModelAsset',group['model_asset']),('PresentationClass',group['presentation_class']),('VATDefinition',group['vat_definition'])]:
        assert row[key] == expected or (not expected and row[key] in ('None',''))
    if group['name'] not in ('ElectromagneticMiner','ConstructionVehicle'):
        continue
    ready = json.loads((ART / ('Reports/formal_prepared_' + group['name'] + '.json')).read_text(encoding='utf8'))
    expected = {c['component']:c for c in ready['component_bindings']}
    source_group = next(g for g in json.loads((ART / 'Reports/resource_groups.json').read_text(encoding='utf8'))['groups'] if g['name'] == group['name'])
    transforms = {c['component']:c for c in source_group['component_bindings']}
    actor = actors.spawn_actor_from_class(unreal.load_class(None,group['presentation_class']),unreal.Vector(0,0,-60000),transient=True)
    try:
        components = []
        for comp in actor.get_components_by_class(unreal.MeshComponent):
            if not comp.is_visible():
                continue
            field = 'skeletal_mesh_asset' if isinstance(comp,unreal.SkeletalMeshComponent) else 'static_mesh'
            mesh = comp.get_editor_property(field)
            if not mesh:
                continue
            wanted = expected[comp.get_name()]
            assert mesh.get_path_name() == wanted['mesh']
            assert [m.get_path_name() if m else None for m in comp.get_materials()] == wanted['materials']
            relative = unreal.MathLibrary.make_relative_transform(comp.get_world_transform(),actor.get_actor_transform())
            original = transforms[comp.get_name()]
            assert max(abs(a-b) for a,b in zip(relative.translation.to_tuple(),original['location'])) < .01
            assert max(abs(a-b) for a,b in zip(relative.scale3d.to_tuple(),original['scale'])) < .001
            components.append(dict(component=comp.get_name(),mesh=mesh.get_path_name(),scale=list(relative.scale3d.to_tuple())))
        assert len(components) == 8
        vehicles.append(dict(name=group['name'],components=components,loaded_current_native_module=True))
    finally:
        actors.destroy_actor(actor)
assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
report = dict(success=True,editor_world=editor.get_editor_world().get_path_name(),engine=unreal.SystemLibrary.get_engine_version(),
    game_world=None,formal_switched=True,loaded_soldier_rows=rows,vehicles=vehicles,map_saved=True,
    dirty_content=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],
    dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
    pie='not_run',network='not_run',fps='not_run')
assert not report['dirty_maps']
(ART / 'Reports/final_native_status.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,map=report['editor_world'],map_saved=True,
    formal_groups=6,vehicles=[dict(name=v['name'],visible_components=len(v['components'])) for v in vehicles],
    game_world=None,dirty_maps=report['dirty_maps'],dirty_content=report['dirty_content'])))
