"""Editor-only note update after the approved build is loaded. Never starts or stops PIE."""
from pathlib import Path
import json
import os
import re
import shutil
import unreal

MAP = '/Game/Maps/LVL_CommanderMassPrototype'
TITLE = 'Mass 按连接预算同步（协议 19，待玩家验收）'
editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert editor.get_game_world() is None, 'Finish the current PIE with user authorization first.'
world = editor.get_editor_world()
assert world and world.get_path_name() == MAP + '.LVL_CommanderMassPrototype'
root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
output = root / 'Artifacts/MassStateBatch/20260923'
# ProtocolVersion is a protected UPROPERTY; use the read-only GETALL CDO output
# from this exact cold-start editor rather than changing reflection exposure.
launch = json.loads((output / 'editor-launch.json').read_text(encoding='utf-8-sig'))
assert launch['pid'] == os.getpid(), 'Protocol evidence belongs to a different editor process.'
protocol_lines = re.findall(r'^.*Default__GuLiBattleGameState\.ProtocolVersion = (\d+)\s*$',
                           (output / 'editor-reloaded.log').read_text(encoding='utf-8-sig'), re.MULTILINE)
assert protocol_lines, 'Read the loaded CDO with GETALL before updating the note.'
protocol = int(protocol_lines[-1])
assert protocol == 19, 'Load the newly built source editor module first.'
actors_api = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
actors = actors_api.get_all_level_actors()
matches = [a for a in actors if a.get_actor_label() == 'StateTreeReview_Entry']
assert len(matches) == 1 and isinstance(matches[0], unreal.Note)
note = matches[0]
assert note.get_editor_property('is_editor_only_actor')
dirty_maps = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]
dirty_content = [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()]
assert MAP not in dirty_maps, 'Review pre-existing unsaved map changes before saving this entry.'
output.mkdir(parents=True, exist_ok=True)
backup = output / 'LVL_CommanderMassPrototype-before-note.umap'
if not backup.exists():
    shutil.copy2(root / 'Content/Maps/LVL_CommanderMassPrototype.umap', backup)
before_text = str(note.get_editor_property('text'))
before_location = list(note.get_actor_location().to_tuple())
before_rotation = list(note.get_actor_rotation().to_tuple())
before_scale = list(note.get_actor_scale3d().to_tuple())
appendix = '\n\n' + TITLE + '\n' + (
    '沿用本地图正常双客户端指挥官入口与现有单位；代码静态检查不代表运行效果通过。\n'
    '大量移动、连续改目标、S停止、明确传送；观察样本缺口及三倍追赶是否减少。\n'
    '排队期间死亡并等残骸回收；未列入单批的士兵应保留，最终回收不得留下幽灵。\n'
    '晚加入、重连及双连接对照：同步完成前禁用选择/命令，完成后数量与状态收敛。\n'
    '可手动开启 guli.Commander.StateStreamDiagnostics 1；MassStream/MassStreamRX 输出逐连接字节、队列、等待、合并、延期、未确认批次与样本间隔。\n'
    '状态批次最多1000字节/4批未确认；位置留额50%，最新样本留队，保留采样时间。\n'
    '120Hz补额限制、战斗特效/建筑复制及三倍纠偏规则本轮保持；不能只凭低Ping验收。'
)
if TITLE not in before_text:
    with unreal.ScopedEditorTransaction('Document Mass per-connection state batch acceptance'):
        note.modify()
        note.set_editor_property('text', before_text + appendix)
        tags = list(note.get_editor_property('tags'))
        if 'MassStateBatchReview' not in [str(t) for t in tags]: tags.append('MassStateBatchReview')
        note.set_editor_property('tags', tags)
saved = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
assert saved
# Read the relevant entity again after save; no viewport capture or runtime commands.
note = next(a for a in actors_api.get_all_level_actors() if a.get_actor_label() == 'StateTreeReview_Entry')
assert TITLE in str(note.get_editor_property('text'))
assert list(note.get_actor_location().to_tuple()) == before_location
assert list(note.get_actor_rotation().to_tuple()) == before_rotation
assert list(note.get_actor_scale3d().to_tuple()) == before_scale
mode = world.get_world_settings().get_editor_property('default_game_mode')
result = {
    'success': True, 'saved': saved, 'map': MAP, 'loaded_protocol': int(protocol),
    'protocol_evidence': 'GETALL protected ProtocolVersion on loaded GuLiBattleGameState CDO',
    'editor_pid': os.getpid(),
    'entry': {'path': note.get_path_name(), 'class': note.get_class().get_path_name(),
              'label': note.get_actor_label(), 'location': before_location,
              'editor_only': note.get_editor_property('is_editor_only_actor'),
              'tags': [str(t) for t in note.tags], 'text': str(note.get_editor_property('text'))},
    'game_mode': mode.get_path_name() if mode else None,
    'initial_dirty_maps': dirty_maps, 'initial_dirty_content': dirty_content,
    'dirty_maps_after': [p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
    'backup': str(backup), 'runtime_verified': False, 'pie_started': False,
}
(output / 'scene-delivery.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(result, ensure_ascii=False))
