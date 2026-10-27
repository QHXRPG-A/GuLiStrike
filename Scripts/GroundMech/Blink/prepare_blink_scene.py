"""Last delivery step: save the existing Ground review entry and read related entities."""
import json
import re
from pathlib import Path
import unreal


def main():
    root = Path(unreal.Paths.project_dir()).resolve()
    out = root / 'ArtSource/FX/GroundMech/Blink_20261027/Shape_E_v1'
    data = json.loads((out / 'data_readback.json').read_text(encoding='utf8'))
    if not data['success']:
        raise RuntimeError('Blink table/native readback has not completed')
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if level.is_in_play_in_editor():
        raise RuntimeError('Do not interrupt gameplay for scene preparation')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if world.get_path_name().split('.')[0] != '/Game/Maps/LVL_CommanderMassPrototype':
        raise RuntimeError('Wrong review map')
    effects = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(
        unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects')))
    effect = next(row for row in effects if row['Name'] == 'GroundMechBlink')
    # The exporter groups ScaleX/Y/Z into a native FVector, exported as UE text.
    scale = [float(v) for v in re.findall(r'[XYZ]=([-+0-9.eE]+)', effect['Scale'])]
    if effect['Id'] != data['blink']['BlinkVfxId'] or len(scale) != 3 or any(
            abs(v - 1.0) > 0.0001 for v in scale):
        raise RuntimeError('Blink VFX table identity/scale mismatch')
    target = '/Game/GuLiStrike/FX/GroundMech/Blink/NS_GroundMech_Blink'
    if target + '.NS_GroundMech_Blink' not in effect['ResourcePath']:
        raise RuntimeError('Blink VFX reference mismatch')
    fx=json.loads((out/'fx_readback.json').read_text(encoding='utf8'))
    streams=list(unreal.NiagaraService.summarize(target).emitter_names)
    if sorted(streams)!=['ArmorFlecks','Strands'] or fx['ribbon_count']!=data['blink']['BlinkRibbonCount']:
        raise RuntimeError('Finite strand burst structure mismatch')
    shape_effect=next(row for row in effects if row['Id']==data['blink']['BlinkShapeVfxId'])
    if not isinstance(unreal.load_asset(shape_effect['ResourcePath']),unreal.GuLiGroundMechBlinkVfxDefinition):
        raise RuntimeError('Blink energy-shape definition is not saved')
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    related = {a.get_actor_label(): a for a in actors if a.get_actor_label().startswith('GroundMobility_') or a.get_actor_label()=='GroundBlink_TransitWall'}
    if 'GroundBlink_TransitWall' not in related:
        raise RuntimeError('Missing intermediate-wall teleport review object')
    for name in ('GroundMobility_Entry', 'GroundMobility_Flat', 'GroundMobility_Slope18'):
        if name not in related:
            raise RuntimeError(f'Missing existing review entity: {name}')
    gm = unreal.get_default_object(world.get_world_settings().get_editor_property('default_game_mode'))
    pawn_class = gm.get_editor_property('default_ground_pawn_class')
    if 'BP_GroundMech_Light' not in str(pawn_class):
        raise RuntimeError('Map Ground entry does not use the production mech')
    entry = related['GroundMobility_Entry']
    entry.modify()
    tags = list(entry.get_editor_property('tags'))
    marker = unreal.Name('GuLi.Review.GroundBlinkGAS')
    if marker not in tags:
        tags.append(marker)
    current=unreal.Name('GuLi.Review.GroundBlinkTeleport')
    if current not in tags:tags.append(current)
    entry.set_editor_property('tags',tags)
    if not level.save_current_level():
        raise RuntimeError('Review map save failed')
    entities = []
    for name, actor in sorted(related.items()):
        row = {'label': name, 'location': list(actor.get_actor_location().to_tuple()),
               'scale': list(actor.get_actor_scale3d().to_tuple())}
        if isinstance(actor, unreal.StaticMeshActor):
            row['collision_profile'] = str(actor.static_mesh_component.get_collision_profile_name())
            if row['collision_profile'] != 'BlockAll':
                raise RuntimeError(f'Review collision is not BlockAll: {name}')
        entities.append(row)
    report = {'success': True, 'map': world.get_path_name(), 'saved': True,
              'ground_class': str(pawn_class), 'entry_tags': [str(t) for t in entry.get_editor_property('tags')],
              'entities': entities, 'blink': data['blink'], 'vfx': effect,
              'shape_vfx':shape_effect,'ribbon_count':fx['ribbon_count'],'emitters':streams,
              'configuration_fingerprint': data['configuration_fingerprint'],
              'PIE_started': False, 'visual_approval': 'pending_player'}
    (out / 'scene_readback.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf8')
    unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'map':report['map'],'entities':len(entities),
        'ribbon_count':fx['ribbon_count'],'emitters':streams,'saved':True}))


main()
