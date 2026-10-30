"""Save actual passive Mass deployments in the existing Ground map; no runtime or visual testing."""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Artifacts/GroundRogue_20261028'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
PREFIX = 'GroundRogue_'
FOLDER = 'Review/GroundRogue_20261028'

def main():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if level.is_in_play_in_editor() or world.get_path_name().split('.')[0] != MAP:
        raise RuntimeError('The original review map must be open and not playing')
    if unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages() or unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages():
        raise RuntimeError('Preserve unrelated unsaved editor work before scene authoring')
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    existing = {a.get_actor_label(): a for a in sub.get_all_level_actors()}
    if 'GroundMobility_Entry' not in existing or 'GroundMobility_Flat' not in existing:
        raise RuntimeError('The existing Ground mobility area is missing')
    before = [a.get_actor_label() for a in existing.values() if isinstance(a, unreal.GuLiCommanderDeploymentPoint) and not a.get_actor_label().startswith(PREFIX)]
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    expected = {}

    def actor(label, cls, pos):
        name = PREFIX + label
        value = existing.get(name)
        if value is None:
            value = sub.spawn_actor_from_class(cls, unreal.Vector(*pos))
        if not isinstance(value, cls):
            raise RuntimeError('Unexpected actor type: ' + name)
        value.modify()
        value.set_actor_label(name)
        value.set_folder_path(FOLDER)
        value.set_actor_location(unreal.Vector(*pos), False, False)
        value.set_actor_rotation(unreal.Rotator(0, 180, 0), False)
        value.set_editor_property('tags', [unreal.Name('GuLi.Review.GroundRogue')])
        expected[name] = value
        return value

    for label, pos, scale in [('Floor', (-8500, 54000, 880), (150, 40, .5)),
                              ('Wall', (-4300, 55100, 1505), (30, .8, 12))]:
        value = actor(label, unreal.StaticMeshActor, pos)
        value.set_actor_scale3d(unreal.Vector(*scale))
        mesh = value.static_mesh_component
        mesh.modify()
        mesh.set_static_mesh(cube)
        mesh.set_mobility(unreal.ComponentMobility.STATIC)
        mesh.set_collision_profile_name('BlockAll')
        mesh.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    actor('Entry', unreal.TargetPoint, (-2800, 54000, 1149.04))
    # Keep the existing certified ore clusters clear, including the 150 cm correction margin.
    positions = [(-4300,54000),(-5200,54000),(-6100,53400),(-7000,54000),(-7300,53400),
                 (-8800,54600),(-9700,53400),(-10600,54000),(-11100,54800)]
    for label, x, y, team in ([(f'Enemy_{i+1:02}', x, y, unreal.GuLiTeam.BLUE) for i, (x, y) in enumerate(positions)]
                              + [('BehindWall', -4300,56200,unreal.GuLiTeam.BLUE),
                                 ('OutOfRange',-15500,54000,unreal.GuLiTeam.BLUE),
                                 ('Friendly',-4000,53200,unreal.GuLiTeam.RED)]):
        value = actor(label, unreal.GuLiCommanderDeploymentPoint, (x, y, 905))
        for name, setting in [('team',team),('unit_type_id',1),('rows',1),('columns',1),
                              ('spacing_centimeters',480.0),('allow_automatic_fire',False),('start_idle',True)]:
            value.set_editor_property(name, setting)
    if not level.save_current_level():
        raise RuntimeError('Review map save failed')
    result = []
    after = []
    for value in sub.get_all_level_actors():
        label = value.get_actor_label()
        if isinstance(value, unreal.GuLiCommanderDeploymentPoint) and not label.startswith(PREFIX):
            after.append(label)
        if label not in expected:
            continue
        row = {'label': label, 'class': value.get_class().get_name(), 'location': list(value.get_actor_location().to_tuple()),
               'scale': list(value.get_actor_scale3d().to_tuple()), 'map': value.get_outermost().get_name()}
        if isinstance(value, unreal.GuLiCommanderDeploymentPoint):
            row.update(team=str(value.get_editor_property('team')), unit=value.get_editor_property('unit_type_id'),
                       rows=value.get_editor_property('rows'), columns=value.get_editor_property('columns'),
                       automatic_fire=value.get_editor_property('allow_automatic_fire'), idle=value.get_editor_property('start_idle'))
            if row['automatic_fire'] or not row['idle'] or row['rows'] != 1 or row['columns'] != 1:
                raise RuntimeError('Passive deployment readback failed: ' + label)
        if isinstance(value, unreal.StaticMeshActor):
            row.update(mesh=value.static_mesh_component.static_mesh.get_path_name(),
                       collision=str(value.static_mesh_component.get_collision_enabled()),
                       profile=str(value.static_mesh_component.get_collision_profile_name()))
        result.append(row)
    if len(result) != len(expected) or sorted(before) != sorted(after):
        raise RuntimeError('Scene entity readback or preservation failed')
    installation = OUT / 'asset_install.json'
    assets_ready = installation.exists() and json.loads(installation.read_text(encoding='utf-8')).get('applied', False)
    native_ready = unreal.load_class(None, '/Script/GuLiStrike.GuLiRogueCardGroundRicochetEffect') is not None
    ready = bool(assets_ready and native_ready)
    report = {'success':True, 'saved':True, 'map':MAP, 'entities':result, 'preserved_deployments':before,
              'runtime_ready':ready, 'ready_for_player':ready, 'runtime_verified':False,
              'reason':'ready_for_player_verification' if ready else 'native_build_and_guarded_asset_import_pending',
              'play_started':False, 'assistant_visual_test':False, 'visual_acceptance':'pending_user',
              'entry':'Select Red Ground; move to GroundRogue_Entry (-2800,54000,1149.04). F4 selects the sole eligible card; aim at Enemy_01 west of the entry. Reopen F4 for the next 6–9 segment upgrade.'}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'scene_delivery.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    return report

if (ROOT/'data/Excel/GuLiStrikeBuffs.xlsx').exists():
    current=ROOT/'Scripts/GroundMech/Rogue/prepare_burning_scene.py'
    exec(compile(current.read_text(encoding='utf8'),str(current),'exec'),{'__name__':'ground_rogue_current_scene'})
else:
    unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))
