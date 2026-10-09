"""Add and save two exact-source, editor-only color references in the existing Mass map."""
import json
from pathlib import Path
import unreal

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'ArtSource/SweeperTeamColor_v1_20261008/UE'
MAP='/Game/Maps/LVL_CommanderMassPrototype'
TAG='GuLi.SweeperOrangeTeam.Acceptance.20261009'


def run():
    editor=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if levels.is_in_play_in_editor() or world.get_path_name().split('.')[0]!=MAP:
        raise RuntimeError('Leave another gameplay session/map untouched.')
    formal=json.loads((OUT.parent/'formal-ue-import.json').read_text(encoding='utf8'))
    if not json.loads((OUT/'runtime-local-team-readback.json').read_text(encoding='utf8'))['success']:
        raise RuntimeError('Finish the actual runtime hookup before final scene handoff.')
    mesh=unreal.load_object(None,formal['mesh'])
    def make(label,cls,position):
        existing=[a for a in editor.get_all_level_actors() if a.get_actor_label()==label]
        if len(existing)>1 or (existing and not existing[0].actor_has_tag(TAG)):
            raise RuntimeError('Unowned/duplicate review actor: '+label)
        a=existing[0] if existing else editor.spawn_actor_from_class(cls,position)
        a.modify();a.set_actor_label(label);a.set_folder_path('GuLiStrike/Review/Sweeper_20261009')
        a.set_editor_property('tags',[TAG,'ModelId=1005'])
        a.set_editor_property('is_editor_only_actor',True)
        a.set_actor_enable_collision(False)
        return a
    definitions=[]
    # Preserve all gameplay deployments, including the original Q summon entrance.
    reference=next(a for a in editor.get_all_level_actors() if a.get_actor_label()=='ModelRegistry_Mass_SweeperSummon')
    base=reference.get_actor_location()
    for relation,delta,color in [('OwnBlue',-500,[.144128470858,.371237680474,.514917665377,1]),
                                  ('EnemyRed',500,[.366252595599,.051269458374,.086500462037,1])]:
        location=base+unreal.Vector(delta,-800,0)
        a=make('ModelRegistry_Sweeper_'+relation+'_20261009',unreal.StaticMeshActor,location)
        a.set_actor_location_and_rotation(location,unreal.Rotator(),False,True)
        a.set_actor_scale3d(unreal.Vector(.2,.2,.2))
        c=a.static_mesh_component;c.modify();c.set_static_mesh(mesh)
        c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        c.set_editor_property('can_ever_affect_navigation',False)
        c.set_evaluate_world_position_offset(False)
        c.set_default_custom_primitive_data_vector4(8,unreal.Vector4(*color))
        c.set_default_custom_primitive_data_float(16,1.)
        definitions.append({'label':a.get_actor_label(),'expected_primary':color})
    center=base+unreal.Vector(0,-800,100)
    camera=make('ModelRegistry_Sweeper_Camera_20261009',unreal.CameraActor,center+unreal.Vector(700,1600,1800))
    camera.set_actor_location_and_rotation(center+unreal.Vector(700,1600,1800),unreal.MathLibrary.find_look_at_rotation(center+unreal.Vector(700,1600,1800),center),False,True)
    camera.camera_component.set_editor_property('projection_mode',unreal.CameraProjectionMode.ORTHOGRAPHIC)
    camera.camera_component.set_editor_property('ortho_width',1900.)
    note=make('ModelRegistry_Sweeper_Notes_20261009',unreal.TextRenderActor,center+unreal.Vector(0,-500,100))
    note.set_actor_location(center+unreal.Vector(0,-500,100),False,True)
    note.text_render.set_text('SWEEPER 1005 / ORIGINAL ORANGE = TEAM COLOR\nOWN BLUE / ENEMY RED\nPLAY: SELECT PIONEER, PRESS Q')
    note.text_render.set_world_size(55)
    if not levels.save_current_level():
        raise RuntimeError('Acceptance map save failed.')
    return {'map':MAP,'owned_actors':[d['label'] for d in definitions]+[camera.get_actor_label(),note.get_actor_label()],
            'definitions':definitions,'saved':True,
            'gameplay_entrance':'Existing Pioneer deployments; select own Pioneer and press Q; no summon-only initial deployments added'}


result=run()
(OUT/'acceptance-scene-save.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(result))
