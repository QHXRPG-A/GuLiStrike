import hashlib
import json
import re
import sys
from pathlib import Path

base = Path('D:/UE5.7/test1')
evidence = base / 'Artifacts/MassStutterFix/20260923'
dev = base / 'Progress/DevelopmentDocumentation/20260922-导航内存优化与对局容量预算.md'
parent = base / 'Progress/Gameplay/指挥官.md'
child = base / 'Progress/Gameplay/指挥官/04-验证边界.md'
archive = base / 'Progress/Archive/20260923-客户端Mass硬校正修复编译交付.md'
previous_archive = base / 'Progress/Archive/20260923-客户端Mass硬校正最小修复.md'
paths = (dev, parent, child, previous_archive)
original_bytes = {p: p.read_bytes() for p in paths}
before = {str(p.relative_to(base)): hashlib.sha256(data).hexdigest() for p, data in original_bytes.items()}
(evidence / 'docs-before-build.json').write_text(json.dumps(before, ensure_ascii=False, indent=2), encoding='utf-8')
assert not archive.exists(), 'Do not overwrite an archive'

def field(text, key, value):
    result, count = re.subn(r'^' + re.escape(key) + r':.*$', lambda _: key + ': ' + value, text, count=1, flags=re.M)
    assert count == 1, key
    return result

def replace(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old, new, 1)

texts = {p: data.decode('utf-8').replace('\r\n', '\n') for p, data in original_bytes.items()}
text = texts[dev]
text = field(text, 'status', 'verification')
text = field(text, 'summary', '客户端硬校正最小修复已通过源码Editor编译并加载，原图入口读回；待玩家验证行走效果。')
text = field(text, 'next_action', '玩家在原图复测近远距离行走、启停和转向；发送降频、权威XY跳变及其他玩法另待办。')
text = field(text, 'status_note', '静态检查、源码Editor编译和8份BuildId核对通过；新DLL与原图入口已读回。玩家已自行开始PIE，助手未运行验收。')
text = replace(text,
    '硬校正改为相邻权威样本位移超过“速度上限×时间间隔＋200cm”才触发，保留显式瞬移。单文件静态检查与旧轨迹算术复核通过，尚未编译；原图入口无资产改动。[最小修复](../Archive/20260923-客户端Mass硬校正最小修复.md)、[原始分析](../Archive/20260923-客户端Mass行走顿挫分析.md)。',
    '硬校正改用相邻权威样本的时间与速度预算，保留显式瞬移。静态检查、源码Editor编译及8份BuildId核对通过；新DLL与原图入口已加载，玩家效果待验。[编译交付](../Archive/20260923-客户端Mass硬校正修复编译交付.md)、[最小修复](../Archive/20260923-客户端Mass硬校正最小修复.md)。')
texts[dev] = text

text = texts[parent]
text = field(text, 'status_note', '2026-09-23客户端硬校正最小修复已通过静态检查及源码Editor编译，新DLL和原图入口已读回；玩家自行PIE中，效果待反馈。低帧率与权威跳变另待办。')
text = replace(text,
    '静态检查通过，尚未编译加载。[变更与验证边界](../Archive/20260923-客户端Mass硬校正最小修复.md)。',
    '静态检查与源码Editor编译通过，新DLL已加载原图，待玩家验证。[编译交付与验证边界](../Archive/20260923-客户端Mass硬校正修复编译交付.md)。')
texts[parent] = text

text = texts[child]
text = field(text, 'summary', '客户端硬校正最小修复已通过静态检查和源码Editor编译，原图加载新DLL；待玩家验证，低帧率与权威跳变另待办。')
text = field(text, 'status_note', '2026-09-23获用户“编译，然后我来验证”授权，Editor构建退出0、8份BuildId一致；原图入口及新DLL读回，玩家自行PIE中，助手未运行验收。')
text = replace(text, '这是离线算术核对，未编译或运行新代码。', '这是离线算术核对，不代表修复后运行验收。')
text = replace(text,
    '复用已保存原地图的`StateTreeReview_Entry`及R2C4入口，当前编辑器无PIE、无脏包。加载新构建后仍需近远距离行走、启停、转向及明确传送复测；低帧率和权威跳变另待处理。[修复交付](../../Archive/20260923-客户端Mass硬校正最小修复.md)。',
    '用户批准后以源码UE5.7编译`GuLiStrikeEditor Win64 Development`，退出0、UBT耗时31.82秒，8份BuildId一致。PID28384冷启动已加载新DLL，已保存原图的`StateTreeReview_Entry`及R2C4实体读回一致、无脏包；用户已自行进入PIE，助手没有启动或操作游戏。近远距离行走、启停、转向及明确传送仍待玩家反馈；低帧率和权威跳变另待处理。[编译交付](../../Archive/20260923-客户端Mass硬校正修复编译交付.md)、[最小修复](../../Archive/20260923-客户端Mass硬校正最小修复.md)。')
texts[child] = text

sys.path.insert(0, str(base / '.agents/skills/gulistrike-progress/scripts'))
import progress_docs as docs
child_body = docs.split_front_matter(texts[child])[1]
child_segment = child_body.split(docs.SPLIT_CONTENT_MARKER, 1)[1]
child_hash = docs.normalized_payload_hash(child_segment, child)
texts[child] = field(texts[child], 'split_segment_sha256', child_hash)
parts = []
for name in ('01-战局选兵与移动.md', '02-UI表现与性能.md', '03-数据技能与武器表现.md', '04-验证边界.md', '05-数值与演进.md'):
    path = child.parent / name
    body = docs.split_front_matter(texts[path] if path in texts else path.read_text(encoding='utf-8'))[1]
    segment = body.split(docs.SPLIT_CONTENT_MARKER, 1)[1]
    parts.append(docs.transform_markdown_links(segment, path))
parent_hash = hashlib.sha256(''.join(parts).encode('utf-8')).hexdigest()
old_hash = re.search(r'^split_payload_sha256: (.+)$', texts[parent], re.M).group(1)
texts[parent] = field(texts[parent], 'split_previous_payload_sha256', old_hash)
texts[parent] = field(texts[parent], 'split_revision', '20260923-client-hardsnap-build')
texts[parent] = field(texts[parent], 'split_payload_sha256', parent_hash)
texts[parent] = texts[parent].replace('`' + old_hash + '`', '`' + parent_hash + '`')

archive_text = '''---
schema: guli-progress/v1
id: ARC-20260923-007
work_id: ''
kind: archive
role: root
title: 客户端Mass硬校正修复编译交付
areas: [commander, network, performance]
categories: [gameplay, performance]
status: recorded
verification: partial
created: '2026-09-23'
updated: '2026-09-23'
summary: 用户授权后完成源码Editor构建，退出0且8份BuildId一致；冷启动加载新DLL与原图入口，效果由玩家验证。
next_action: 玩家复测近远距离Mass持续行走、启停与转向，反馈周期性顿挫是否消失；低帧率与权威跳变另待办。
relations:
  work_items: [WORK-20260922-002]
status_note: 编译与加载通过，不等于效果验收；用户自行启动PIE，助手未启动或操作游戏，没有新增或运行测试，也未提交或推送。
---

# 2026-09-23：客户端Mass硬校正修复编译交付

## 授权与构建

接续[单文件最小修复](20260923-客户端Mass硬校正最小修复.md)。用户明确要求“编译，然后我来验证”。构建前进程检查确认无UnrealEditor；此前编辑器无PIE及脏包，关闭请求连接被拒绝后未强制结束任何进程。

以源码引擎`D:\\UnrealEngine-5.7`执行：

```powershell
& 'D:\\UnrealEngine-5.7\\Engine\\Build\\BatchFiles\\Build.bat' GuLiStrikeEditor Win64 Development '-Project=D:\\UE5.7\\test1\\GuLiStrike.uproject' -WaitMutex -NoHotReloadFromIDE
```

12:58:50至12:59:24（UTC+8），退出码0、Result Succeeded；UBT耗时31.82秒，外层墙钟33.754秒。编译记录包含`GuLiCommanderPresentationActor.cpp`并重新链接`UnrealEditor-GuLiStrike.dll`，本阶段未再修改源码。

引擎、项目及GuLiFlightNavigation、GuLiMapAuthoring、Tripo3DUEBridge、UnrealMCP、UnrealMCPython、VibeUE共8份模块清单的BuildId均为`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`。源文件SHA256为`41fa38a5a8196f66f330203073688593bb64eae67294e3ad2ea1f1d1fdaea9f1`；运行DLL写入时间12:59:23。构建报告既有StructUtils弃用以及VibeUE/Tripo依赖声明警告，没有编译错误。

## 加载与玩家入口

13:01:25冷启动源码编辑器PID28384，进程模块表确认加载本项目新生成的`UnrealEditor-GuLiStrike.dll`。原图`/Game/Maps/LVL_CommanderMassPrototype`与以下实体通过编辑器对象查询读回：

| 入口 | Actor | 位置cm |
|---|---|---|
| 既有玩法说明 | StateTreeReview_Entry / Note_0 | (0,72500,-1266.086) |
| 红方此前移动现场 | Outpost_R2C4 / GuLiMapMarker_27 | (-20000,60000,-1778.599) |

地图与内容包均无未保存改动。本修复仅改客户端判定，不需改动现有已保存资产或另建场景。读回时用户已自行启动原图PIE；助手未启动、停止或向该局下达输入，保留用户会话。加载证据只证明新模块和原图入口就绪，不证明行走效果通过。

玩家在原图沿用专服加客户端玩法，以红方Mass作近距/远距持续移动、S停止后重新移动与转弯，观察是否还出现周期性短停和跳动；有既有明确传送入口时确认传送仍立即校正。待反馈实际效果，不主动开展PIE、压力或自动化验收。整体低帧率、发送降频及此前权威位置跳变仍属独立范围。

## 证据

- [构建日志](../../Artifacts/MassStutterFix/20260923/build.log)、[退出码与授权](../../Artifacts/MassStutterFix/20260923/build-result.json)
- [8份BuildId、DLL及源码哈希](../../Artifacts/MassStutterFix/20260923/build-ids.json)、[新进程模块表](../../Artifacts/MassStutterFix/20260923/editor-loaded-modules.json)
- [加载后原图实体读回](../../Artifacts/MassStutterFix/20260923/scene-loaded-readback.json)、[编辑器日志](../../Artifacts/MassStutterFix/20260923/editor-fixed.log)
- [静态修复阶段](20260923-客户端Mass硬校正最小修复.md)、[当前开发记录](../DevelopmentDocumentation/20260922-导航内存优化与对局容量预算.md)
'''

for path, data in original_bytes.items():
    assert path.read_bytes() == data, f'Concurrent change: {path}'
for path in (dev, parent, child):
    path.write_text(texts[path], encoding='utf-8', newline='\n')
archive.write_text(archive_text, encoding='utf-8', newline='\n')
assert previous_archive.read_bytes() == original_bytes[previous_archive]
(evidence / 'doc-build-hash-update.json').write_text(json.dumps({
    'previous_parent': old_hash, 'parent': parent_hash, 'child04': child_hash,
    'archive_id': 'ARC-20260923-007', 'dev_bytes': dev.stat().st_size,
}, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'archive': str(archive), 'parent_hash': parent_hash, 'dev_bytes': dev.stat().st_size}, ensure_ascii=True))
