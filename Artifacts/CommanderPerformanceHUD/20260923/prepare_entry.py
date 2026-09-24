import json
from pathlib import Path
import unreal

artifact = Path('D:/UE5.7/test1/Artifacts/CommanderPerformanceHUD/20260923')
assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name() == '/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'
note = unreal.find_object(None, world.get_path_name() + ':PersistentLevel.Note_0')
assert note and note.get_actor_label() == 'StateTreeReview_Entry'
before = str(note.get_editor_property('text'))
section = '\n\n2026-09-23 客户端连续纠偏 / 性能HUD\n左上角显示FPS与往返延迟RTT，每0.5秒刷新；本地游戏显示“延迟 本地”。\n普通移动保留历史并逐帧追赶，显示速度最多为标准速度3倍；明确传送仍走传送。\n玩家检查近远距离移动、转向、S后重新移动和明确传送，观察是否还会单兵跳步；服务端异常位移来源另待修复。'
assert '2026-09-23 客户端连续纠偏 / 性能HUD' not in before
note.modify()
note.set_editor_property('text', before + section)
assert unreal.EditorLoadingAndSavingUtils.save_map(world, '/Game/Maps/LVL_CommanderMassPrototype')
assert str(note.get_editor_property('text')) == before + section
result = {'map': world.get_path_name(), 'actor': note.get_path_name(),
          'saved': True, 'previous_text': before, 'text': str(note.get_editor_property('text'))}
(artifact / 'entry-save.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'map': result['map'], 'saved': True, 'actor': result['actor']}))
