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


def prepare_directional_scene():
    """Extend the existing mobility floor; save and read only related entities."""
    import math
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert not level.is_in_play_in_editor()
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name().split('.')[0]=='/Game/Maps/LVL_CommanderMassPrototype'
    subsystem=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors={a.get_actor_label():a for a in subsystem.get_all_level_actors()}
    entry=actors['GroundMobility_Entry']; flat=actors['GroundMobility_Flat']
    position=entry.get_actor_location()
    assert (position-unreal.Vector(-2800,50000,1149.04)).length()<.01
    preserved={n:actors[n].get_actor_transform().export_text() for n in ('GroundMobility_Slope18','Ground3C_LookLeft','Ground3C_LookRight','GroundBlink_TransitWall')}
    flat.modify(); flat.set_actor_location(unreal.Vector(position.x,position.y,880),False,False)
    flat.set_actor_scale3d(unreal.Vector(80,80,.5))
    assert str(flat.static_mesh_component.get_collision_profile_name())=='BlockAll'
    cylinder=unreal.load_asset('/Engine/BasicShapes/Cylinder.Cylinder')
    material=actors['Ground3C_LookLeft'].static_mesh_component.get_material(0)
    assert cylinder and material
    marker_names=[]
    for degrees in range(0,360,45):
        radians=math.radians(degrees)
        point=unreal.Vector(position.x+3200*math.cos(radians),position.y+3200*math.sin(radians),907)
        name=f'GroundBlink_Landing_{degrees:03d}'
        marker=actors.get(name) or subsystem.spawn_actor_from_class(unreal.StaticMeshActor,point)
        assert isinstance(marker,unreal.StaticMeshActor)
        marker.modify(); marker.set_actor_label(name); marker.set_folder_path('Review/GroundMobility/BlinkDirections')
        marker.set_actor_location(point,False,False); marker.set_actor_rotation(unreal.Rotator(),False)
        marker.set_actor_scale3d(unreal.Vector(2.8,2.8,.02))
        marker.static_mesh_component.set_static_mesh(cylinder); marker.static_mesh_component.set_material(0,material)
        marker.static_mesh_component.set_collision_profile_name('NoCollision')
        marker.static_mesh_component.set_editor_property('cast_shadow',False)
        marker.set_editor_property('tags',['GuLi.Review.GroundBlinkDirectional','Distance3200'])
        label_name=name+'_Label'; label=actors.get(label_name) or subsystem.spawn_actor_from_class(unreal.TextRenderActor,point+unreal.Vector(0,0,100))
        label.modify(); label.set_actor_label(label_name); label.set_folder_path('Review/GroundMobility/BlinkDirections')
        label.set_actor_location(point+unreal.Vector(0,0,100),False,False)
        label.set_actor_rotation(unreal.Rotator(pitch=0,yaw=degrees+180,roll=0),False)
        text=label.get_component_by_class(unreal.TextRenderComponent)
        text.set_text(f'32 m / {degrees}°'); text.set_world_size(75)
        marker_names.extend([name,label_name])
    entry.modify(); tags=list(entry.get_editor_property('tags'))
    if unreal.Name('GuLi.Review.GroundBlinkDirectional') not in tags: tags.append(unreal.Name('GuLi.Review.GroundBlinkDirectional'))
    entry.set_editor_property('tags',tags)
    assert level.save_current_level()
    actors={a.get_actor_label():a for a in subsystem.get_all_level_actors()}
    entities=[]
    for name in ['GroundMobility_Entry','GroundMobility_Flat',*preserved,*marker_names]:
        actor=actors[name]; center,extent=actor.get_actor_bounds(False)
        row={'label':name,'class':actor.get_class().get_name(),'location':actor.get_actor_location().to_tuple(),
             'scale':actor.get_actor_scale3d().to_tuple(),'bounds':[center.to_tuple(),extent.to_tuple()]}
        if name in preserved: assert actor.get_actor_transform().export_text()==preserved[name]
        if name.startswith('GroundBlink_Landing_') and not name.endswith('_Label'):
            assert abs((actor.get_actor_location()-position).length2d()-3200)<.01
            row['collision']=str(actor.static_mesh_component.get_collision_profile_name())
            assert row['collision']=='NoCollision'
        entities.append(row)
    center,extent=flat.get_actor_bounds(False)
    assert abs(center.x-position.x)<.01 and abs(center.y-position.y)<.01 and extent.x>=4000 and extent.y>=4000
    gm=unreal.get_default_object(world.get_world_settings().get_editor_property('default_game_mode'))
    pawn_class=gm.get_editor_property('default_ground_pawn_class')
    mesh=unreal.get_default_object(pawn_class).get_editor_property('mesh')
    assert 'BP_GroundMech_Light' in pawn_class.get_path_name()
    assert mesh.get_editor_property('skeletal_mesh_asset').get_name()=='SK_GroundMech_A4B2'
    assert 'ABP_GroundMech_A4B2' in mesh.get_editor_property('anim_class').get_path_name()
    report={'success':True,'map':world.get_path_name(),'saved':True,'entities':entities,
            'ground_class':pawn_class.get_path_name(),'mesh':mesh.get_editor_property('skeletal_mesh_asset').get_path_name(),
            'anim_class':mesh.get_editor_property('anim_class').get_path_name(),'PIE_started':False,'visual_approval':'pending_player'}
    path=Path(unreal.Paths.project_dir())/'Artifacts/GroundDirectionalBlink_20261107/scene-readback.json'
    path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    return report


if __name__=='__main__': main()
