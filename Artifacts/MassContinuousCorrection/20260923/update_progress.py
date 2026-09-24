import hashlib
import json
import re
import sys
from pathlib import Path

root = Path('D:/UE5.7/test1')
artifact = root / 'Artifacts/MassContinuousCorrection/20260923'
hud_artifact = root / 'Artifacts/CommanderPerformanceHUD/20260923'
sys.path.insert(0, str(root / '.agents/skills/gulistrike-progress/scripts'))
import progress_docs as docs

scene = json.loads((hud_artifact / 'loaded-scene.json').read_text(encoding='utf-8'))
build = json.loads((artifact / 'build-result.json').read_text(encoding='utf-8-sig'))
assert scene['correction_speed_multiplier'] == 3.0 and scene['text_readback_matches_source']
assert build['exit_code'] == 0
before_hashes = json.loads((artifact / 'docs-before.json').read_text(encoding='utf-8'))
for name, expected in before_hashes.items():
    assert hashlib.sha256((root / name).read_bytes()).hexdigest() == expected, name

def metadata(text, field, value):
    return re.sub(r'^' + re.escape(field) + r':.*$', field + ': ' + value, text, count=1, flags=re.M)

def create(path, ident, work, kind, title, areas, status, relation, summary, next_action, body):
    dest = root / 'Progress' / path
    assert not dest.exists(), dest
    relation_key, relation_value = relation
    note = ('用户已确认；实现、编译与场景配置核对不替代玩家效果验收。' if kind == 'requirement'
            else '代码静态检查及源码Editor编译通过，原图已有入口和已保存文本资产读回；用户自行开始PIE，具体效果由用户核对。')
    text = f'''---
schema: guli-progress/v1
id: {ident}
work_id: {work}
kind: {kind}
role: root
title: {title}
areas: [{areas}]
categories: [gameplay, performance]
status: {status}
verification: partial
created: '2026-09-23'
updated: '2026-09-23'
summary: {summary}
next_action: {next_action}
relations:
  {relation_key}: {relation_value}
status_note: {note}
---

# {title}

{body}
'''
    dest.write_text(text, encoding='utf-8')

hud_name = '20260923-游戏左上角帧率与延迟显示.md'
correction_name = '20260923-Mass历史插值与三倍速度纠偏.md'
archive_name = '20260923-客户端连续纠偏与性能HUD交付.md'
create('RequirementDocument/' + hud_name, 'REQ-20260923-001', 'WORK-20260923-001', 'requirement',
       '游戏左上角帧率与延迟显示', 'commander, ui, performance', 'approved',
       ('development', 'DEV-20260923-001'), '左上角常驻显示本地FPS及客户端到服务器的往返延迟，便于区分整帧卡顿与单兵跳步。',
       '玩家在原地图核对文字、刷新、联网延迟及输入不受影响。', '''## 已确认需求与范围

用户要求“给游戏左上角加上性能指标，延迟、帧率”，随后批准现在编译并重启，明确具体效果由用户核对。

显示FPS与延迟ms；延迟定义为往返时间RTT，不把插值缓冲约0.3秒算作网络延迟。约每0.5秒刷新，位于现有顶部状态条下方；未取得测量时显示占位，本地主机/单机标为本地。显示不拦截框选、移动或键盘输入，不新增网络探测RPC。

## 玩家验收

- [ ] `/Game/Maps/LVL_CommanderMassPrototype`正常进入指挥官后可见左上角FPS和RTT，文字不与状态条重叠。
- [ ] 数字按节拍更新，窗口尺寸变化后仍处于视口内。
- [ ] 框选、移动、停止和角色切换的输入仍正常。

文本遵循现有Excel → JSON/CSV → DataTable管线。见[开发记录](../DevelopmentDocumentation/20260923-游戏左上角帧率与延迟显示.md)。''')

create('DevelopmentDocumentation/' + hud_name, 'DEV-20260923-001', 'WORK-20260923-001', 'development',
       '游戏左上角帧率与延迟显示', 'commander, ui, performance', 'verification',
       ('requirement', 'REQ-20260923-001'), '已有Commander Canvas HUD增加只读性能条，每0.5秒刷新；源表及191行DataTable已保存并全量核对。',
       '由玩家核对原地图左上角显示、窗口适配和RTT；不自动启动PIE。', '''## 实现

`GuLiCommanderHUD.h/.cpp`在既有DrawHUD尾部绘制小型半透明条。Canvas拥有显示，现有HUD生命周期负责销毁；没有额外Widget、输入热区、计时器或委托。每个本地HUD缓存文本，按真实时钟与GFrameCounter差值计算0.5秒平均FPS，避免暂停或时间缩放污染；同进程PIE各视口共用引擎帧数，这不是专服Tick率。

客户端优先读取ServerConnection的AvgLag，回退RawPingInSeconds、PlayerState Ping；秒转毫秒并标RTT。无有效测量为“延迟 --”，非客户端为“延迟 本地”。DPI缩放后的(16,64)处位于现有Y=12..56顶部状态条下方，文本随视口可用空间约束。数据只读，无RPC或Actor扫描，保持原焦点和输入所有权。

使用excelize追加`GuLiStrikeGameTexts.xlsx/Texts`五个`UI.Performance.*`键，原186行保持不变；复用标准导出函数与限定单表导入，191行完整内容回读一致。生成结构及其他表未修改。

## 实施状态

- [x] 完成HUD及文本管线，静态接口、DPI、单位换算和差异检查。
- [x] 按用户授权编译源码Editor并冷启动加载，最终构建与连续纠偏合并。
- [x] 在已有原地图玩法入口核对GameMode/Controller/HUD引用，保存文本DataTable并读回；地图既有入口复用。
- [ ] 玩家核对实际显示及输入效果。

## 场景交付

Map为`/Game/Maps/LVL_CommanderMassPrototype`；`StateTreeReview_Entry / Note_0`位于(0,72500,-1266.086)cm，附近使用原指挥官出生入口。`Outpost_R2C4 / GuLiMapMarker_27`位于(-20000,60000,-1778.599)cm。GameMode=GuLiCommanderGameMode、Controller=GuLiCommanderPlayerController、HUD=GuLiCommanderHUD已读回。

玩家按原流程进入指挥官，观察左上角FPS/RTT，再框选、移动、S停止及切角色。若本地专服同机，RTT很低是有效测量，不包含显示插值滞后。用户已自行进入PIE；助手未启动或操作新游戏。尝试补充Note说明时检测到PIE并在任何修改前退出，未中断玩家对局，复用已保存入口。地图仍按忽略规则仅本地保留。

## 构建与边界

用户批准“现在编译并重启”，并继续要求连续历史/三倍速度纠偏，一并收尾。源码`D:\\UnrealEngine-5.7`，`GuLiStrikeEditor Win64 Development`最终退出0、30.46秒，8份BuildId一致。PID5552加载最终DLL；没有运行自动化或额外压测。导入旧PIE会话残留文本表时发生UObject引用断言，尚未写资产；冷启动后限定单表导入成功。重启遇GPU启动超时，一次恢复后编辑器响应正常，未改RHI、电源或渲染配置。

详见[合并交付归档](../Archive/20260923-客户端连续纠偏与性能HUD交付.md)、[文本回读](../../Artifacts/CommanderPerformanceHUD/20260923/text-import.json)、[场景数据](../../Artifacts/CommanderPerformanceHUD/20260923/loaded-scene.json)。''')

create('RequirementDocument/' + correction_name, 'REQ-20260923-002', 'WORK-20260923-002', 'requirement',
       'Mass历史插值与三倍速度纠偏', 'commander, movement, network, performance', 'approved',
       ('development', 'DEV-20260923-002'), '普通位置更新保留历史并连续追赶插值目标，纠偏显示速度最多为标准移动速度3倍。',
       '玩家验证近远距离移动、转向、停止恢复及明确传送，确认残余单兵跳步是否改善。', '''## 用户确认

用户明确希望“只要历史位置还在，客户端就尽可能插值平滑到对应的位置上，而不是直接设置过去”，随后确认“允许3倍标准速度去做纠偏”。

普通更新不再因为误差超过距离阈值而清空历史或覆盖显示位置。历史仍按现有时间戳插值，显示从上一帧连续追赶求值目标，最大总显示速度为标准速度的3倍；不是正常移动速度外再叠加3倍。追上后恢复原插值轨迹，不为正常移动额外增加固定延迟。

显式传送、可靠位移事件及首次无显示状态仍独立处理；网络身份重置可清理旧世代缓存。保持有限历史、100ms最大外推、服务器权威和碰撞规则。本阶段解决客户端普通校正的可见硬跳，不宣布服务端异常导航位移来源已修复。

## 玩家验收

- [ ] 普通大误差或恢复收包时持续追赶，不出现清空历史后直接落到新位置。
- [ ] 纠偏显示总速度不超过3倍标准速度；普通移动、启停和转向仍正常。
- [ ] 明确传送仍及时到达目标，不沿普通移动速度横穿地图。

在`/Game/Maps/LVL_CommanderMassPrototype`沿用原入口，由用户核对效果，见[开发记录](../DevelopmentDocumentation/20260923-Mass历史插值与三倍速度纠偏.md)。''')

create('DevelopmentDocumentation/' + correction_name, 'DEV-20260923-002', 'WORK-20260923-002', 'development',
       'Mass历史插值与三倍速度纠偏', 'commander, movement, network, performance', 'verification',
       ('requirement', 'REQ-20260923-002'), '普通样本始终进入有限历史，最终显示位置以3倍标准速度收敛；源码Editor已编译加载，效果由用户验证。',
       '记录玩家当前局近远距离移动反馈；服务端异常XY位移和发送供给另待定位。', '''## 最小实现

`GuLiCommanderPresentationActor::InsertPoseSample`删除非传送的大距离硬校正分支；新样本继续加入原8条有界历史，排序、迟到窗口、播放时钟和预测生命周期保持。仅推进时间线的显式Teleport清空旧历史并立即落位。旧非标记HardSnap诊断字段保留以兼容现有快照格式，不再被普通样本触发。

最终显示提交处，在历史三次插值/有界外推、本地预测和接触表现合成之后，从上一帧显示位置朝目标线性追赶，每帧最大位移为`标准速度 × 3 × DeltaSeconds`。距离小于预算则精确到达求值点，不添加持续插值滞后或渐近尾巴。旋转仍用既有最短角插值；此限制只约束显示位置，不改变服务器导航、速度、选敌、伤害或碰撞。

标准速度采用GameState复制的有效Mass速度，包括运行时调参，回退已验证数据定义；不以异常位移除时间或包内速度放宽上限。当前Mass权威共用720cm/s，纠偏最大2160cm/s（21.6m/s）。`MaximumCorrectionSpeedMultiplier=3.0`替换失效的200cm硬跳阈值；运行时限制1..3。明确传送/可靠位移设置一次性豁免，消费后清除，网络/世代重置亦清除；首次显示无需从世界原点追赶。

## 实施与验证

- [x] 保留历史并加入最终三倍显示位移限制，维持瞬移与世代边界。
- [x] 静态核对唯一普通显示提交点、有限历史和外推、标记复位、现有数据依赖及差异检查。
- [x] 同步既有Scale020用例的过期阈值/硬跳预期；未新增用例或执行测试。
- [x] 获准源码Editor构建退出0、30.46秒，8份BuildId一致；原地图加载新DLL，CDO纠偏倍率=3.0读回。
- [ ] 用户验证实际效果；服务端异常位移来源与长间隔姿态供给仍待后续修复。

## 场景和玩家操作

沿用`/Game/Maps/LVL_CommanderMassPrototype`已保存的`StateTreeReview_Entry`与R2C4，位置及原生GameMode/Controller/HUD已读回；新增HUD文本资产已保存。没有另造兵或改变地图/导航。用户已自行开始新PIE，助手未输入命令或再启动游戏。

在指挥官视角分别选近处与远处部队，连续移动、转向、S后再移动，观察部分单位是否仍直接跳步，并结合左上角FPS/RTT区分整帧卡顿；明确传送应保持原体验。若仍出现异常，反馈位置和操作顺序，后续针对该局只读采样；当前不能以编译通过宣称玩家效果通过。

三倍限速下，大误差会产生短时追赶。若服务器位置持续异常或整个客户端长时间无帧，不能保证完全无可见顿挫；服务端6～13米异常位移和姿态供给不均并未在此补丁修改。较大误差的显示过渡不等于重新寻路，最终仍以服务器结果为准。

证据：[来源差异](../../Artifacts/MassContinuousCorrection/20260923/source-fix.diff)、[静态检查](../../Artifacts/MassContinuousCorrection/20260923/static-review.json)、[构建](../../Artifacts/MassContinuousCorrection/20260923/build-result.json)、[加载模块](../../Artifacts/MassContinuousCorrection/20260923/loaded-module.json)、[合并归档](../Archive/20260923-客户端连续纠偏与性能HUD交付.md)。''')

create('Archive/' + archive_name, 'ARC-20260923-009', "''", 'archive',
       '客户端连续纠偏与性能HUD交付', 'commander, movement, network, ui, performance', 'recorded',
       ('work_items', '[WORK-20260923-001, WORK-20260923-002, WORK-20260922-002]'),
       '保留普通位置历史并以最多3倍标准速度纠偏，新增FPS/RTT；源码Editor编译加载与资产读回完成，用户自行验收。',
       '用户反馈当前局效果；继续定位服务端异常XY位移及姿态供给不均。', '''## 本轮决定

用户批准结束旧局、编译并重启新增HUD，明确效果自行核对；随后要求只要有历史就连续插值，并允许3倍标准速度纠偏。最终实现普通更新不清空历史、不直接覆盖显示位置，按最后显示位置向历史求值结果收敛，总速度上限为已发布Mass标准速度×3；显式传送和首次显示单独处理。当前720cm/s对应2160cm/s。

FPS/RTT在既有Canvas HUD左上角状态条下方显示，每0.5秒刷新。FPS使用真实经过时间和引擎帧计数，RTT复用连接统计；文本源新增5键、原186行不变，191行DataTable全量回读一致。没有额外网络RPC、输入热区或后台计时器。

## 变更范围与证据

| 文件/资产 | 本次变更 |
|---|---|
| `Commander/Presentation/GuLiCommanderHUD.h/.cpp` | 只读FPS与RTT性能条 |
| `Commander/Presentation/GuLiCommanderPresentationActor.h/.cpp` | 普通样本保留历史，最终显示限速纠偏，明确传送豁免及复位 |
| `Config/DefaultGame.ini` | 200cm硬跳配置替换为3倍纠偏上限 |
| 既有`GuLiScale020InterpolationTests.cpp` | 仅更新过期合同预期，未执行测试 |
| `Data/Excel/GuLiStrikeGameTexts.xlsx`与导出JSON/CSV | 新增五个性能文本键，原行保持 |
| `/Game/GuLiStrike/Data/DT_GuLiStrikeGameTexts_Texts` | 单表导入、保存、191行精确核对 |

源码diff、静态审查、编译、BuildId和模块加载证据保存在[纠偏目录](../../Artifacts/MassContinuousCorrection/20260923)，HUD/资产和场景读回在[HUD目录](../../Artifacts/CommanderPerformanceHUD/20260923)。既有工作区改动保留，本次没有提交或推送GitHub。

## 编译、异常恢复与场景

源码引擎`D:\\UnrealEngine-5.7`；先编译HUD退出0、29.83秒，用户补充纠偏要求后，同目标`GuLiStrikeEditor Win64 Development`增量构建退出0、30.46秒。8份模块BuildId均为`dd3ee083-a0fd-45c8-814e-67fe5ef95e31`，PID5552加载13:41:01生成的最终DLL，CDO倍率3.0。编译旧测试源码不等于执行测试。

旧PID28384在无脏包时按授权结束PIE。之后替换旧会话仍有引用的文本表触发`UObjectArray.cpp:398`断言，导入未到保存阶段、原资产未写入。新Editor首次启动遇D3D12 GPU超时；只对本轮冷启动PID14056执行关闭/恢复，第二次启动曾短时GPU超时后恢复响应。未改渲染设置、电源或驱动；这不是移动代码运行失败。冷启动且未进入PIE时完成限定单表导入和精确回读，无脏包。

原图`/Game/Maps/LVL_CommanderMassPrototype`的Commander GameMode/Controller/HUD、`StateTreeReview_Entry`(0,72500,-1266.086)cm和R2C4(-20000,60000,-1778.599)cm实体读回完成。新增功能直接使用原图正常玩法入口和已保存文本资产。用户随后自行进入PIE，补充Note说明的脚本在修改前检测到PIE并退出；未中断玩家、未改地图。地图继续仅本地保留。

## 验证边界

代码静态核对与源码编译通过，配置、资产和原图入口读回通过。用户当前在自行核对FPS/RTT、移动、启停、转向与明确传送，助手没有运行新PIE、自动化或额外压测，玩家效果仍为待验收。

此补丁处理客户端普通校正的可见硬跳，未改变服务端异常XY位移的来源、旧三相发送供给或实际友伤规则。此前[残余跳步诊断](20260923-电源模式与Mass残余跳步诊断.md)继续有效；不能宣布所有抖动已杜绝或内存/性能收益已测得。正式说明与操作分别见[HUD开发](../DevelopmentDocumentation/20260923-游戏左上角帧率与延迟显示.md)、[连续纠偏开发](../DevelopmentDocumentation/20260923-Mass历史插值与三倍速度纠偏.md)。''')

dev_path = root / 'Progress/DevelopmentDocumentation/20260922-导航内存优化与对局容量预算.md'
text = dev_path.read_text(encoding='utf-8')
text = metadata(text, 'summary', '客户端改为保留历史并以最多3倍标准速度连续纠偏，新增FPS/RTT；源码Editor加载完成，效果由用户验证。')
text = metadata(text, 'next_action', '玩家核对连续纠偏与HUD；服务端异常XY位移、姿态供给及专服容量测量继续后续处理。')
text = metadata(text, 'status_note', '静态检查、源码Editor构建、8份BuildId与原图配置读回通过；用户自行进入新PIE，效果待反馈。')
start = text.index('## 当前修复：客户端Mass硬校正')
end = text.index('## 玩家反馈：红方选中但不移动', start)
text = text[:start] + '''## 当前：Mass连续纠偏（2026-09-23）

普通样本保留历史，显示按最多3倍标准速度追赶；FPS/RTT已接入并完成源码Editor编译加载，效果由用户核对。[交付](../Archive/20260923-客户端连续纠偏与性能HUD交付.md)。平衡电源缓解持续卡顿，但日志仍有服务端6～13米位移，来源及供给不均待查；[诊断](../Archive/20260923-电源模式与Mass残余跳步诊断.md)。

''' + text[end:]
dev_path.write_text(text, encoding='utf-8')

children = [root / 'Progress/Gameplay/指挥官' / name for name in
    ['01-战局选兵与移动.md', '02-UI表现与性能.md', '03-数据技能与武器表现.md', '04-验证边界.md', '05-数值与演进.md']]
sections = {
    children[1]: '''### 当前：连续纠偏与FPS/RTT（2026-09-23）

普通Mass位置更新保留8条有界历史，不再按距离阈值清空缓冲；沿用原时间插值和100ms外推，再从上一帧显示位置向求值目标连续追赶。最大总显示速度为已发布标准速度3倍，当前720→2160cm/s；追上后直接恢复原轨迹，普通行走不额外增加固定延迟。明确传送、首次显示和网络世代重置单独处理；服务器导航、伤害和碰撞规则保持。[实现与边界](../../DevelopmentDocumentation/20260923-Mass历史插值与三倍速度纠偏.md)。

左上角现有顶部状态条下方增加只读FPS/延迟RTT，每0.5秒刷新。FPS基于真实时间和引擎帧数；RTT来自连接统计，不包含客户端插值缓冲。非客户端标“本地”，尚无测量标“--”。Canvas随DPI和视口适配，不拦截输入；文本维护于GuLiStrikeGameTexts.xlsx的UI.Performance系列键。新代码和文本资产已编译加载，实际显示与行走由用户核对。[HUD交付](../../DevelopmentDocumentation/20260923-游戏左上角帧率与延迟显示.md)。

''',
    children[3]: '''### 当前：三倍速度连续纠偏与性能HUD（2026-09-23）

用户切换平衡电源后持续卡顿缓解，但确认仍有部分部队跳步且镜头/UI正常。本局分析记录17次服务端非标记位移，最近两次0.1秒内6.06/6.65米并落到同一点；源头写入分支尚未确认。先前最小修复仅排除正常插值滞后的误判，不能判为全部问题已解决。[残余诊断](../../Archive/20260923-电源模式与Mass残余跳步诊断.md)。

按用户后续明确要求，普通位置保留历史，最终显示按最多3倍标准速度连续纠偏；FPS/RTT常驻条一并完成。源码Editor退出0、30.46秒，8份BuildId一致；PID5552加载新DLL，原图入口、3.0倍率及191行文本表读回，保存资产后无脏包。用户自行开始新PIE，实际效果由用户核对；助手未运行验收。服务端异常XY位移、供给不均与弹道观感来源仍待定位，不以客户端平滑代替源头修复。[最新交付](../../Archive/20260923-客户端连续纠偏与性能HUD交付.md)。

'''
}
for path, addition in sections.items():
    text = path.read_text(encoding='utf-8')
    text = metadata(text, 'updated', "'2026-09-23'")
    text = metadata(text, 'status_note', '2026-09-23连续历史纠偏及FPS/RTT已编译加载和读回；用户自行验证，服务端异常位移源头仍待修复。')
    if path == children[3]:
        text = metadata(text, 'summary', '三倍速度连续纠偏与FPS/RTT已编译加载，用户自行核对效果；服务端异常位移与供给不均未修复。')
    text = text.replace(docs.SPLIT_CONTENT_MARKER, docs.SPLIT_CONTENT_MARKER + addition, 1)
    segment = docs.split_front_matter(text)[1].split(docs.SPLIT_CONTENT_MARKER, 1)[1]
    text = metadata(text, 'split_segment_sha256', docs.normalized_payload_hash(segment, path))
    path.write_text(text, encoding='utf-8')

parent = root / 'Progress/Gameplay/指挥官.md'
text = parent.read_text(encoding='utf-8')
old_hash = re.search(r'^split_payload_sha256: (.+)$', text, re.M)[1]
segments = [docs.split_front_matter(p.read_text(encoding='utf-8'))[1].split(docs.SPLIT_CONTENT_MARKER, 1)[1] for p in children]
new_hash = hashlib.sha256(''.join(docs.transform_markdown_links(s, p) for s, p in zip(segments, children)).encode('utf-8')).hexdigest()
text = text.replace(old_hash, new_hash)
text = metadata(text, 'split_previous_payload_sha256', old_hash)
text = metadata(text, 'split_revision', '20260923-continuous-correction-performance-hud')
text = metadata(text, 'status_note', '2026-09-23连续历史及3倍速度纠偏、FPS/RTT已编译加载并核对配置；用户自行PIE验收，服务器异常位移源头待修复。')
text = text.replace('## 当前摘要\n', '''## 当前摘要

- 2026-09-23最新：按用户确认，普通Mass更新保留历史并以最多3倍标准速度连续追赶，新增左上角FPS/RTT。源码Editor已编译加载，3.0配置、原图入口和文本资产读回；用户自行PIE核对效果。服务端异常位移源头仍未修复。[最新交付](../Archive/20260923-客户端连续纠偏与性能HUD交付.md)。
''', 1)
parent.write_text(text, encoding='utf-8')
print('Created two requirement/development pairs, archive009 and updated active gameplay records.')
