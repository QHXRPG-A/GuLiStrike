"""Save editor-only uphill review notes in the real commander map; never start gameplay."""
import json
import traceback
from pathlib import Path

import unreal

MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'MassUphillRouteReview20261001'
FOLDER = 'GuLiStrike/Review/MassUphillRoute'
LANDSCAPE = 'Landscape_CommanderIsland_1800m_v1'
ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'outputs/commander-uphill-routing-20261001'
REPORT = {'success': False, 'map': MAP, 'saved': False, 'runtime_verified': False,
          'gameplay_started': False, 'navigation_rebuilt': False}


def record(actor):
    row = {'path': actor.get_path_name(), 'label': actor.get_actor_label(),
           'class': actor.get_class().get_name(),
           'location': list(actor.get_actor_location().to_tuple()),
           'rotation': list(actor.get_actor_rotation().to_tuple()),
           'scale': list(actor.get_actor_scale3d().to_tuple()),
           'editor_only': actor.get_editor_property('is_editor_only_actor'),
           'collision': actor.get_actor_enable_collision()}
    if isinstance(actor, unreal.Note):
        row['text'] = str(actor.get_editor_property('text'))
    if isinstance(actor, unreal.CameraActor):
        row['field_of_view'] = actor.camera_component.get_editor_property('field_of_view')
    return row


def run():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assert editor.get_game_world() is None, 'Leave the user-started PIE untouched; end it only with permission.'
    world = editor.get_editor_world()
    assert world and world.get_path_name() == MAP + '.LVL_CommanderMassPrototype'
    api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = api.get_all_level_actors()
    owned = {a.get_actor_label(): a for a in actors if TAG in [str(t) for t in a.tags]}
    original = {a.get_path_name(): record(a) for a in actors if a not in owned.values()}
    definition = unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap')
    layout_hash = definition.get_editor_property('layout_hash')
    army_layout = json.loads(unreal.GuLiResourceAuthoringLibrary.get_initial_army_spawn_layout_json())
    nav = [a for a in actors if isinstance(a, unreal.RecastNavMesh)
           and str(a.get_editor_property('nav_data_config').get_editor_property('name')) == 'CommanderSoldier']
    assert len(nav) == 1
    maximum_slope = nav[0].get_editor_property('agent_max_slope')
    assert maximum_slope == 44.0

    def ground(x, y):
        sample = unreal.LandscapeService.get_height_at_location(LANDSCAPE, x, y)
        assert sample.valid and sample.height > 0, f'Expected land at ({x}, {y})'
        return unreal.Vector(x, y, sample.height)

    def get_actor(label, cls, location):
        actor = owned.get(label)
        if actor is None:
            actor = api.spawn_actor_from_class(cls, location)
            owned[label] = actor
        assert actor and actor.get_class() == cls.static_class()
        actor.modify()
        actor.set_actor_label(label)
        actor.set_folder_path(FOLDER)
        actor.set_editor_property('tags', [TAG])
        actor.set_editor_property('is_editor_only_actor', True)
        actor.set_actor_hidden_in_game(True)
        actor.set_actor_enable_collision(False)
        actor.set_actor_location(location, False, False)
        return actor

    pairs = [
        ('135', (-15257.0, 32661.0), (-15478.57, 29771.38)),
        ('141', (-21681.57, 32687.03), (-14057.17, 32233.32)),
    ]
    note_rows = [('Entry', (-20000.0, 37000.0),
                  'Mass上坡绕行验收：先加载本次原生修复。使用当前真实指挥官玩法，'
                  '玩家自行开局后选本方单位按S停止自动推进，分别从Foot135/Foot141附近向Goal135/Goal141下移动令。'
                  '预期沿缓坡/绕行路线到高处，不直顶陡坡；有路径时持续停滞应进入重寻路。'
                  '数字来自原失败样本，重开后的SoldierId可能不同。')]
    ground_pairs = []
    for identifier, start, goal in pairs:
        ground_pairs.append({'sample_id': identifier, 'start': list(ground(*start).to_tuple()),
                             'goal': list(ground(*goal).to_tuple())})
        note_rows += [
            ('Foot' + identifier, start,
             f'原{identifier}号山脚卡点。由真实移动流程将一组单位移到附近，再向同编号Goal标记下令；'
             '沿导航绕行，途中允许暂时远离终点。'),
            ('Goal' + identifier, goal,
             f'原{identifier}号的高处落点。预期最终贴地到达；换远目标和按S仍应正常生效。'
             '复验矿体变化后重寻路，不能穿过陡岸或改变指令落点。'),
        ]
    for suffix, xy, text in note_rows:
        note = get_actor('MassUphill_' + suffix, unreal.Note, ground(*xy) + unreal.Vector(0, 0, 600))
        note.set_editor_property('text', text)
    center = ground(-17400.0, 31800.0)
    camera = get_actor('MassUphill_Overview', unreal.CameraActor, ground(-24500.0, 37000.0)
                       + unreal.Vector(0, 0, 12000))
    camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(camera.get_actor_location(), center), False)
    camera.camera_component.set_editor_property('field_of_view', 60.0)

    unrelated = {a.get_path_name(): record(a) for a in api.get_all_level_actors()
                 if TAG not in [str(t) for t in a.tags]}
    assert unrelated == original, 'An unrelated actor changed.'
    assert definition.get_editor_property('layout_hash') == layout_hash
    assert json.loads(unreal.GuLiResourceAuthoringLibrary.get_initial_army_spawn_layout_json()) == army_layout
    assert nav[0].get_editor_property('agent_max_slope') == maximum_slope
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    rows = [record(a) for a in api.get_all_level_actors() if TAG in [str(t) for t in a.tags]]
    assert len(rows) == 6 and all(r['editor_only'] and not r['collision'] for r in rows)
    assert editor.get_game_world() is None
    REPORT.update(success=True, saved=True, actors=rows, pairs=ground_pairs,
                  max_slope_degrees=maximum_slope, resource_layout_hash=layout_hash,
                  army_layout_preserved=True, unrelated_actors_preserved=len(original),
                  entry='MassUphill_Entry', observation_camera='MassUphill_Overview')


try:
    run()
except Exception:
    REPORT['error'] = traceback.format_exc()
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'scene-delivery.json').write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({k: REPORT[k] for k in
    ['success', 'saved', 'entry', 'observation_camera', 'error'] if k in REPORT}, ensure_ascii=False))
