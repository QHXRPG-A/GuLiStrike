"""Save construction review anchors in the existing gameplay map; never starts PIE."""
import gc
import json
from pathlib import Path
import unreal

MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TAG = 'BuildingConstructionReview'


def record(actor):
    return {'name': actor.get_name(), 'label': actor.get_actor_label(), 'class': actor.get_class().get_path_name(),
        'location': list(actor.get_actor_location().to_tuple()), 'rotation': list(actor.get_actor_rotation().to_tuple()),
        'scale': list(actor.get_actor_scale3d().to_tuple())}


def run():
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    assert editor.get_game_world() is None, 'Finish the active play session before scene authoring.'
    world = editor.get_editor_world()
    assert world.get_path_name() == MAP + '.LVL_CommanderMassPrototype', world.get_path_name()
    assert hasattr(unreal, 'GuLiBuildingConstructionVisualComponent'), 'Load the rebuilt project module first.'
    definition = unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap')
    economy = unreal.load_asset('/Game/GuLiStrike/Data/Resources/DA_ResourceEconomy')
    assert definition and economy
    anchors = definition.get_editor_property('spawn_anchors')
    assembly = anchors.get_editor_property('red_assembly')
    territories = definition.get_editor_property('territories')
    home = min((t for t in territories if t.initial_owner == unreal.GuLiTeam.RED),
        key=lambda t: (t.center.x - assembly.x) ** 2 + (t.center.y - assembly.y) ** 2)
    review_position = home.center + unreal.Vector(4000, 0, 500)
    api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = api.get_all_level_actors()
    owned = [a for a in actors if TAG in [str(t) for t in a.tags]]
    original = {a.get_path_name(): record(a) for a in actors if a not in owned}
    specs = [
        ('ConstructionReview_Placement', review_position,
         '建造表现样板：使用本地图正常指挥官入口。B进入建造模式，在建造菜单选择任意建筑（1–6）。\n'
         '鼠标移动位置按1米吸附；R每次旋转90度，四次恢复初始方向。\n'
         '建筑占地区域的格子：可放置为绿色，不可放置为红色；左键确认。'),
        ('ConstructionReview_Progress', review_position + unreal.Vector(0, 900, 0),
         '矿厂施工：落地后出现完整蓝色透明虚影；建造车到达后，实体随施工进度升起。\n'
         '选中施工车按S停工，观察模型保持当前高度；重新下达施工后继续。\n'
         '完工后显示正常建筑，蓝色虚影退去，边缘发光约0.65秒。'),
        ('ConstructionReview_Mining', review_position + unreal.Vector(1000, 0, 0),
         '通行回归：施工效果不移动建筑碰撞、坡道或矿车停靠点。\n'
         '矿厂完工后验证门动画、矿车入厂卸货与离厂；只在工厂完整落地后启用正常功能。\n'
         '六类建筑均已启用通用施工表现；施工框沿真实基底轮廓升起，放置网格与R旋转适用于全部可建造建筑。'),
    ]
    names = ['防空炮', '哨戒炮', '前哨', '兵营', '护盾发生器', '矿厂']
    for index, x in enumerate([-6500, -3800, -500, 3000, 5700, 9200], 1):
        specs.append((f'ConstructionReview_Type{index}', unreal.Vector(x, 76000, -1260),
            f'{index}：{names[index-1]}建造验证区。B进入建造模式，再选对应建筑。\n'
            'R每次旋转90度；观察基底轮廓、停工呼吸、双紫色光束及完工底部闪光。'))
    notes = []
    with unreal.ScopedEditorTransaction('Prepare construction review anchors'):
        for label, position, text in specs:
            found = [a for a in owned if a.get_actor_label() == label]
            assert len(found) <= 1
            note = found[0] if found else api.spawn_actor_from_class(unreal.Note, position)
            note.modify()
            note.set_actor_label(label)
            note.set_actor_location(position, False, False)
            note.set_folder_path('GuLiStrike/Review/Construction')
            note.set_editor_property('tags', [TAG])
            note.set_editor_property('is_editor_only_actor', True)
            note.set_editor_property('text', text)
            notes.append(note)
    current = {a.get_path_name(): record(a) for a in api.get_all_level_actors()
        if TAG not in [str(t) for t in a.tags]}
    assert current == original, 'An unrelated actor changed.'
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    return {'map': MAP, 'saved': True, 'anchors': [record(n) for n in notes],
        'game_mode': str(world.get_world_settings().get_editor_property('default_game_mode')),
        'construction_vehicles_per_team': economy.get_editor_property('initial_construction_vehicles_per_team'),
        'existing_actor_count': len(original), 'play_started': False}


def main():
    try:
        result = run()
        result['success'] = True
    except Exception as error:
        result = {'success': False, 'error': str(error)}
    output = Path('D:/UE5.7/test1/outputs/construction-vfx/scene.json')
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=True))


main()
gc.collect()
