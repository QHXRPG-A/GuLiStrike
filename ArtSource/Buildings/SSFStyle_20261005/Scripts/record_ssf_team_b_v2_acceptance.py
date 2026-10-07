"""Append explicit user acceptance without changing frozen art or UE packages."""
import hashlib
import json
import re
from pathlib import Path

ROOT = Path('D:/UE5.7/test1')
R = ROOT / 'ArtSource/Buildings/SSFStyle_20261005'
D = R / 'UE_Delivery_Team_v2'
O = R / 'TeamPalette_B_v2_20261007'
OUT = R / 'Acceptance_B_v2_20261007'
VERSION = 'SSF_TeamPalette_B_v2'
EXPECTED_BLEND = '5cee0988de1a4201cd9eb60de9cbd30ccddd108090b99b3e86f50c4bf2d584b6'
ARCHIVE = ROOT / 'Progress/Archive/20261007-SSF建筑蓝红正式资源B_v2审核通过.md'


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def sha(path):
    assert path.suffix.lower() not in {'.uasset', '.umap', '.uexp', '.ubulk', '.pak'}
    result = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def write(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf8')


def metadata(text, fields):
    parts = text.split('---', 2)
    assert len(parts) == 3
    for key, value in fields.items():
        parts[1], count = re.subn(r'^' + re.escape(key) + r':.*$', key + ': ' + value, parts[1], flags=re.M)
        assert count == 1, key
    return '---' + parts[1] + '---' + parts[2]


assert not OUT.exists() and not ARCHIVE.exists(), 'Acceptance has already been recorded; inspect rather than duplicate.'
formal = read(D / 'formal_delivery.json')
ue = read(D / 'ue_validation.json')
assert formal['success'] and formal['version'] == VERSION and formal['new_resource_count'] == 48
assert ue['success'] and ue['asset_count'] == 48
assert len(ue['component_animation_checks']) == 378 and len(ue['captures']) == 84
assert formal['source_blend_sha256'] == EXPECTED_BLEND
assert sha(Path(formal['source_blend'])) == EXPECTED_BLEND
frozen = []
for base, manifest in [(R, O / 'delivery_manifest.json'), (D, D / 'delivery_manifest.json')]:
    entries = read(manifest)['files']
    for entry in entries:
        path = (base / entry['file']).resolve()
        assert path.is_relative_to(base.resolve())
        assert sha(path) == entry['sha256'], ('Frozen file changed', str(path))
    frozen.append({'manifest': str(manifest.relative_to(R)).replace('\\', '/'),
                   'manifest_sha256': sha(manifest), 'verified_file_count': len(entries), 'unchanged': True})

req = ROOT / 'Progress/RequirementDocument/20261005-SSF建筑美术统一与三档LOD.md'
dev = ROOT / 'Progress/DevelopmentDocumentation/20261005-SSF建筑美术统一与三档LOD.md'
norm = ROOT / 'Progress/RequirementDocument/GuLiStrike美术规范.md'
ledger = ROOT / 'Progress/DevelopmentDocumentation/GuLiStrike美术规范.md'
decision_path = R / 'review_decisions.json'
readme_path = R / 'README.md'
targets = [req, dev, norm, ledger, decision_path, readme_path]
before = {str(path.relative_to(ROOT)).replace('\\', '/'): sha(path) for path in targets}
texts = {path: path.read_text(encoding='utf-8-sig') for path in targets}
decisions = json.loads(texts[decision_path])
assert decisions['current_version'] == VERSION

record = {
    'version': VERSION, 'date': '2026-10-07', 'status': 'approved',
    'stage': 'final_actual_asset_review', 'user_quote': '审核通过',
    'message_context': '用户在蓝红两队正式UE B_v2导入完成、实际UE预览和交付记录展示之后明确通过审核。',
    'source_blend': formal['source_blend'], 'source_blend_sha256': EXPECTED_BLEND,
    'target_roots': formal['target_roots'], 'accepted_new_resource_count': 48,
    'accepted_building_variants': 12, 'LOD_count_per_mesh': 3,
    'scope': '当前实际B_v2蓝红各六座建筑及对应材质贴图，包含当前可见版本已披露的几何与描边差异。',
    'approved_UE_preview': 'UE_Delivery_Team_v2/Sheets/Blue_Red_UE_Overview.png',
    'approved_UE_preview_sha256': sha(D / 'Sheets/Blue_Red_UE_Overview.png'),
    'formal_delivery': 'UE_Delivery_Team_v2/formal_delivery.json',
    'formal_delivery_sha256': sha(D / 'formal_delivery.json'),
    'frozen_delivery_checks': frozen,
    'earlier_import_authorization': 'approval_B_v2_import_20261007.json',
    'original_budget_caps_changed': False, 'global_art_revision': '1.3',
    'performance_validation': 'not_run', 'gameplay_integration_authorized': False,
    'gameplay_integrated': False, 'UE_reimport_performed_this_turn': False,
    'source_or_UE_assets_edited_this_turn': False,
    'record': 'Acceptance_B_v2_20261007/approval.json'
}
accepted_B = dict(decisions['B'])
accepted_B.update({'status': 'approved', 'approval_date': '2026-10-07',
                   'approval_user_quote': '审核通过', 'approval_record': record['record'],
                   'accepted_scope': record['scope']})
decisions['B'] = accepted_B
decisions['team_palette_B_v2'] = dict(accepted_B)
decisions['final_UE_review'] = record
decisions['current_asset_review_status'] = 'approved'
decisions['history'].append(record)
decisions['budget_exception_decision'].update({
    'status': 'original_caps_preserved_with_current_version_acceptance',
    'current_B_v2_asset_review': 'approved', 'current_B_v2_approval_record': record['record'],
    'meaning': '当前两队建筑作为已披露差异的实际成品审核通过，原上限继续保留；原B_v1差额记录及其他配套资产历史不重写。'
})

acceptance_link = '../../ArtSource/Buildings/SSFStyle_20261005/Acceptance_B_v2_20261007/README.md'
addition = f'''\n\n## 2026-10-07 蓝红正式 B_v2 用户审核通过

用户在正式UE交付后明确“审核通过”，[本次最终审核记录]({acceptance_link})固定版本为 `SSF_TeamPalette_B_v2`，来源Blender SHA256 `{EXPECTED_BLEND}`。蓝红各六座建筑、12网格各三档LOD、24材质实例、12图集共48新增正式资源已完成本轮美术与正式资源交付验收；规范v1.3保持。

本决定接续此前“导入至ue作为正式资源”的存储放行，确认当前实际成品的配色、结构、线稿、三档明暗与LOD。已披露的本体差额及局部描边差异随具体版本留档，原面数上限未提高。沿用已有36LOD回读、378动画姿态对照、84实际UE图及保存重载结果；本轮仅更新审核记录，没有重新导入、改模型或扩大验证。147个Blender冻结文件与196个UE交付证据文件哈希保持。

正式目录为 `/Game/GuLiStrike/Buildings/SSFStylized/Blue` 与 `/Game/GuLiStrike/Buildings/SSFStylized/Red`，继续复用B_v1正式共享依赖。原商城包、B_v1、平台、灯具和无人机保持，**暂不接入游戏**；实战性能、玩法和联机仍不在本次资产验收范围。前文待审和存储放行状态保留为历史，以本条最终通过决定为当前状态。
'''
fields = {'verification': 'passed', 'updated': "'2026-10-07'",
          'summary': 'SSF蓝红正式B_v2已获用户明确审核通过，48新增资源和三档LOD交付验收完成，暂不接入游戏。',
          'next_action': '本轮资产制作与交付已验收；后续游戏接入或新版本改造另按用户任务执行。',
          'status_note': '用户审核通过当前实际蓝红B_v2成品与正式UE交付，原预算和既有差异留档；本结论只覆盖资产验收，不代表玩法或性能验收。'}
texts[req] = metadata(texts[req], fields) + addition
texts[dev] = metadata(texts[dev], {'status': 'done', **fields}) + addition
old = '当前 **SSF_TeamPalette_B_v2已获正式存储放行并完成入库**：'
assert texts[dev].count(old) == 1
texts[dev] = texts[dev].replace(old, '当前 **SSF_TeamPalette_B_v2已获用户审核通过并完成正式入库**：', 1)
line = '**SSF当前队色修订（2026-10-07）：**'
assert texts[norm].count(line) == 1
texts[norm] = texts[norm].replace(line, '**SSF当前队色与验收（2026-10-07）：**', 1)
phrase = '暂不接入游戏，原预算差额保持。这是本批资产修订，不修改全局v1.3或其他资产已审配色。'
assert texts[norm].count(phrase) == 1
texts[norm] = texts[norm].replace(phrase,
    f'用户在正式交付后明确“审核通过”，[最终决定]({acceptance_link})已登记到当前B_v2。暂不接入游戏，原预算上限和已披露差额保持。这是本批资产验收，不修改全局v1.3或其他资产已审配色。', 1)
texts[ledger] += addition
phrase = '当前 **SSF_TeamPalette_B_v2** 已按用户“导入至ue作为正式资源”放行并完成正式入库。'
assert texts[readme_path].count(phrase) == 1
texts[readme_path] = texts[readme_path].replace(phrase,
    '当前 **SSF_TeamPalette_B_v2** 已完成正式入库，并获用户明确“审核通过”。', 1)
texts[readme_path] = texts[readme_path].replace('- [当前正式资源交付与路径]',
    '- [当前最终审核通过记录](Acceptance_B_v2_20261007/README.md)\n- [当前正式资源交付与路径]', 1)

for path in targets:
    assert sha(path) == before[str(path.relative_to(ROOT)).replace('\\', '/')], ('Concurrent change; re-read and merge', str(path))
OUT.mkdir()
write(OUT / 'approval.json', record)
write(OUT / 'document_snapshot_before.json', before)
write(OUT / 'frozen_delivery_validation.json', {'success': True, 'manifests': frozen, 'source_blend_sha256': EXPECTED_BLEND})
(OUT / 'README.md').write_text(f'''# SSF 蓝红正式 B_v2 · 用户审核通过

用户明确：“**审核通过**”。本次确认此前已展示并存入UE的实际 `SSF_TeamPalette_B_v2`，蓝红各六座建筑，共48新增正式资源，每个网格三档LOD。本轮美术与正式资源交付验收完成，暂不接入游戏。

- 蓝方：`/Game/GuLiStrike/Buildings/SSFStylized/Blue`
- 红方：`/Game/GuLiStrike/Buildings/SSFStylized/Red`
- [具体决定及版本哈希](approval.json)
- [正式交付清单](../UE_Delivery_Team_v2/README.md)
- [实际UE总览](../UE_Delivery_Team_v2/Sheets/Blue_Red_UE_Overview.png)
- [Blender源文件](../TeamPalette_B_v2_20261007/SSF_TeamPalette_B_v2.blend)
- [冻结文件保持核对](frozen_delivery_validation.json)

来源Blender SHA256 `{EXPECTED_BLEND}`。已有导出回读、材质/骨架/动画/LOD、保存重载和实际UE画面检查继续有效；本轮没有修改成品或重新导入。原本体面数差额与局部描边差异保留为具体版本记录，原面数上限和全局美术规范v1.3保持。原商城包、B_v1正式共享依赖及本次未调色的配套资产继续保留。当前通过决定只确认本轮实际资产，玩法、联机及实战性能另属后续任务。

旧Blender提交记录、UE交付证据和历史归档保持冻结，其当时“待审/存储放行”事实由本次通过决定接续。
''', encoding='utf8')
write(decision_path, decisions)
for path in [req, dev, norm, ledger, readme_path]:
    path.write_text(texts[path], encoding='utf8')
ARCHIVE.write_text(f'''---
schema: guli-progress/v1
id: ARC-20261007-003
work_id: ''
kind: archive
role: root
title: SSF建筑蓝红正式资源B_v2审核通过
areas: [art, assets, rendering]
categories: [art]
status: recorded
verification: passed
created: '2026-10-07'
updated: '2026-10-07'
summary: 用户明确通过当前SSF蓝红正式B_v2，48新增资源及三档LOD完成本轮资产验收，暂不接入游戏。
next_action: 本轮资产制作与正式交付已验收；游戏接入或后续美术修订按新任务处理。
relations:
  work_items: [WORK-20261005-003, WORK-20260917-001]
status_note: 本次为具体实际资产终验，沿用此前技术与视觉检查，不扩大为全局预算上调或实战性能验收。
---

# 2026-10-07：SSF 蓝红正式 B_v2 审核通过

用户在[正式UE交付](../../ArtSource/Buildings/SSFStyle_20261005/UE_Delivery_Team_v2/README.md)及实际预览之后明确“审核通过”。[本次通过记录]({acceptance_link})固定版本 `SSF_TeamPalette_B_v2` 和来源Blender SHA256 `{EXPECTED_BLEND}`，当前实际版本已完成美术和正式资源交付验收。

蓝红各六座建筑，共12网格各三档LOD、24材质实例和12基础色图集，继续使用 `/Game/GuLiStrike/Buildings/SSFStylized/Blue`、`/Game/GuLiStrike/Buildings/SSFStylized/Red`。本轮仅登记最终决定、同步需求/开发验收状态及美术规范的资产记录；没有修改模型、UE包、共享依赖或地图。商城包、B_v1及平台/灯具/无人机保持。

147个冻结Blender交付文件和196个冻结UE证据文件哈希保持，沿用已有36档导出回读、378组动画姿态对照、84张实际UE截图及保存重载结果。[此前入库归档](20261007-SSF建筑蓝红正式资源B_v2入库.md)保留当时的存储放行事实，由本条用户最终通过决定接续，不改写历史。

已披露面数和描边差异继续保留，原面数上限与规范v1.3不变。暂不接入游戏，未新增PIE、联机、性能验证或原生构建；资产审核通过不作为实战帧率结论。既有规范/台账长文档的拆分提示继续保留，另按维护任务处理。
''', encoding='utf8')
print('SSF_B_V2_USER_ACCEPTANCE_RECORDED', json.dumps({'version': VERSION, 'status': 'approved', 'new_assets': 48, 'source_files_unchanged': len(read(O / 'delivery_manifest.json')['files']), 'UE_delivery_files_unchanged': len(read(D / 'delivery_manifest.json')['files']), 'gameplay_integrated': False}))
