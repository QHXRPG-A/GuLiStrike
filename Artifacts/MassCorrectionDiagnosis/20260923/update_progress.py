import hashlib
import json
import re
import sys
from pathlib import Path

root=Path('D:/UE5.7/test1')
artifact=Path(__file__).parent
sys.path.insert(0,str(root/'.agents/skills/gulistrike-progress/scripts'))
import progress_docs as docs

def meta(text,key,value):
    return re.sub(r'^'+re.escape(key)+r':.*$',key+': '+value,text,count=1,flags=re.M)

req=root/'Progress/RequirementDocument/20260923-Mass历史插值与三倍速度纠偏.md'
dev=root/'Progress/DevelopmentDocumentation/20260923-Mass历史插值与三倍速度纠偏.md'
parent=root/'Progress/Gameplay/指挥官.md'
children=[root/'Progress/Gameplay/指挥官'/name for name in ('01-战局选兵与移动.md','02-UI表现与性能.md','03-数据技能与武器表现.md','04-验证边界.md','05-数值与演进.md')]
archive=root/'Progress/Archive/20260923-Mass姿态缺帧与三倍追赶现场诊断.md'
assert not archive.exists()
paths=[req,dev,parent,*children]
(artifact/'docs-before.json').write_text(json.dumps({str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},indent=2),encoding='utf-8')

archive.write_text('''---
schema: guli-progress/v1
id: ARC-20260923-010
work_id: ''
kind: archive
role: root
title: Mass姿态缺帧与三倍追赶现场诊断
areas: [commander, movement, network, performance]
categories: [gameplay, performance]
status: recorded
verification: partial
created: '2026-09-23'
updated: '2026-09-23'
summary: 三倍限速有效但玩家平滑体验未通过；常规帧率下抓到470ms样本缺口和随后三帧满速纠偏，另有权威导航跳变。
next_action: 定位每连接姿态帧缺失环节，修正低帧率发送供给及缓冲耗尽恢复，约束权威导航异常XY位移。
relations:
  work_items: [WORK-20260923-002, WORK-20260922-002]
status_note: 用户已开PIE并要求分析；全程被动采样，没有输入移动、改参数、编译、停止或重启游戏。缺帧的发送/投递/解码分界仍待定位。
---

# Mass姿态缺帧与三倍追赶现场诊断

## 玩家反馈与范围

用户确认三倍纠偏生效，但大量单位加速仍造成卡顿，要求分析已开启的PIE。此反馈不等于平滑效果验收通过。使用PID5552、原图`/Game/Maps/LVL_CommanderMassPrototype`的服务器与两个客户端，被动PredictionTrace baseline、现有只读导航日志、快照和短时ISM采样；未注入命令，未运行NetworkGate，未更改玩法/网络/性能参数。

## 正常服务器轨迹也能触发三倍追赶：实测链条

14:10:49开始，同一个Soldier764在两个客户端分别采30/33秒，启动时PlayerState Ping为23.714/23.393ms。这是采样起点的Ping，不是缺帧瞬间的逐帧RTT。客户端平均约36.9/36.7FPS，P99帧时间39.90ms，最长单帧173.71ms。

客户端1收到姿态帧6292、6294、6296、6298、6300、6302；客户端2缺少6298，从6296直接到6300。两者名义上均属远距隔帧发送；差异不能由正常奇偶抽样解释。客户端2的6296→6300接收间隔470.288ms，对应服务器时间差0.5秒、位移354.40cm，平均708.80cm/s，包内速度714.70cm/s，均接近720cm/s标准速度，**该案例没有服务端大距离跳变**。

客户端2本地时间1008.538527～1008.738073的位置保持为(-62478,52428,-相关Z)；直到1008.775004收到6300才重新推进，固定显示约0.236秒。该帧历史求值目标推进179.03cm/36.931ms，显示按2160cm/s限速；随后两帧继续约2160cm/s，共三帧满速追赶，再恢复普通速度。历史未被清空，PredictionTrace没有本地移动预测或HardSnap事件。

33秒中17帧超过最后样本100ms外推上限，3帧触及三倍速度；同一时段客户端1缺帧更少，没有三倍速度帧。该证据解释了低Ping下的部分单位“先停、后追”：Ping不是单兵姿态到达间隔，保留历史也不保证当前播放时间左右都有样本。详见[客户端2数值](../../Artifacts/MassCorrectionDiagnosis/20260923/foreground-client2-analysis.json)、[客户端1对照](../../Artifacts/MassCorrectionDiagnosis/20260923/foreground-client1-analysis.json)、[完整CSV清单与哈希](../../Artifacts/MassCorrectionDiagnosis/20260923/trace-manifest.json)。

## 已确认的供给与缓冲缺口

1. `GuLiCommanderWorldReplicationComponent::PublishSoldierSnapshotAndPoses`仅在上一组三相块全部清空后捕获下一帧；清空发生在Tick末，下一次捕获要等下一Tick。发送节奏因此受世界帧率和相位等待影响，不是独立稳定的10Hz。
2. 320m之外的移动单位隔捕获帧发送。33FPS第一段对照的单兵到达间隔平均234.79ms，名义5Hz实际约4.26Hz；序号始终+2，供给本身已慢于名义值。
3. 客户端缓冲按平滑后的到达间隔和抖动估算，最长350ms，只外推100ms；单兵播放时钟即使需要增加滞后也至少按0.9倍帧时间推进。遇到突发缺帧会先耗尽可用样本并停住，之后用三倍上限追赶。这是当前实测停走的直接机制。
4. 帧6298究竟在发送、UE不可靠RPC投递、基线解码还是队列消费环节消失，现有单兵Trace无法区分。不能把它直接判为互联网丢包或带宽不足。编辑器当前网络模拟关闭，入/出方向附加延迟与模拟丢包均为0，客户端固定帧率列表为空；这不等同于完整读取各NetDriver即时状态。

## 3FPS时的放大效应与采样边界

14:08:03的16秒采样稳定约3FPS，每帧333ms，ISM回调自身平均4.07ms；随后同一单位两个客户端的原生Trace也稳定3FPS，排除了该回调作为333ms主要开销的解释。姿态平均1.333秒才到一次、捕获序号仍只+2；服务器样本移动均速正常，客户端绝大多数时间已超出外推窗口，反复停住/三倍追赶。该段不能作为正常前台性能结论。

用户对当时是否后台/最小化回答“不清楚，现在我切回前台”。引擎`UEditorEngine::ShouldThrottleCPUUsage/GetMaxTickRate`在后台限流或所有窗口隐藏时可固定3FPS，即使`bThrottleCPUWhenNotForeground=false`，所有窗口隐藏仍可触发；没有采到当时窗口状态，因此限流来源仅为高概率解释，不记为已证实。14:02日志还出现11.33秒/5.86秒Tick间断，来源未定，不能混入后续常规帧率对照结论。

ISM概览按初始位置匹配ID，重叠位置与复用槽位存在歧义，部分ID一帧被计数两次；其全军百分比及超过三倍的极值不可用于结论。它只用于选择原生Trace候选，结论采用后续逐ID原生Trace。首轮两客户端CSV同帧停止导致同名覆盖，保留的是客户端2，未伪称双端对照；后续时长错开避免覆盖。

## 独立的服务器异常位移

13:58:54 Soldier40在0.1秒内从(-34981.2,-16896.2,-1956.5)到(-33801,-17404,-1972.2)，移动12.849m；13:59:00 Soldier34在0.1秒内移动15.850m并落到同一点。14:09～14:10还有多名单位落到(25574,16302,...)附近边界，单步约6m。

源码确认导航推进`IsSurfaceMoveResultAcceptable`仅检查成功、有限值及Z高度差，未约束返回XY与本步速度预算；`FindMoveAlongCurrentNavigationSurface`仅在NodeRef无效时刷新。该返回值接受处是异常位移的重点嫌疑，具体写入调用尚未逐步追踪证明。显式建筑清场会标记位移，不能把全部清场或以上未标记跳变混为一类。这条问题独立于Soldier764的正常轨迹缺帧案例。

## 后续修复顺序与交付边界

先补齐同一单兵/姿态帧在发送、接收、解码与消费各环节的原因记录，定位缺失；让到期发送不因上一帧收尾额外空等一Tick。再按实际样本余量调整播放推进和欠供给恢复，避免外推耗尽后只依赖三倍追赶。另行限制普通导航返回的XY步长并处理引用/当前位置不一致；保留明确传送规则。三倍仅作为大误差上限，不提高常态移动速度或无限扩大缓冲。

本轮只分析并记录，没有改游戏C++或配置，没有编译或结束PIE，没有修改地图/资产及提交GitHub。原图玩家入口保持。归档[上次交付](20260923-客户端连续纠偏与性能HUD交付.md)保留原历史事实；当前平滑体验未通过，来源修复及运行效果待后续实施验收。
''',encoding='utf-8')

feedback='''## 玩家反馈与现场诊断（2026-09-23）

用户确认三倍限速生效，但卡顿体验未通过。已在常规帧率、采样起点Ping约23ms的双客户端对照抓到同一单兵姿态缺帧：470ms无新样本，外推耗尽后显示停住约0.24秒，继而三帧三倍追赶；对应服务器位移仍约正常速度。另有独立的服务器6～16米异常位移。缺帧具体发生在发送/投递/解码/消费哪层仍待定位，3FPS段单独记录，不归为正常前台体验。详见[现场诊断](../Archive/20260923-Mass姿态缺帧与三倍追赶现场诊断.md)。

本轮仅被动分析已开的PIE并更新记录，没有修改运行参数、C++、地图或编译重启。后续修复应优先解决姿态供给与播放缓冲耗尽，以及服务器导航异常XY返回；继续提高纠偏速度不能消除源头。

'''
for path in (req,dev):
    text=path.read_text(encoding='utf-8')
    text=meta(text,'next_action','定位每连接姿态缺帧环节，修复发送供给和缓冲耗尽恢复，约束权威导航异常XY位移。')
    text=meta(text,'status_note','用户确认三倍限速生效，但平滑体验失败；本局被动采样已抓到缺帧后停顿追赶，源头修复尚未实施。')
    if path==dev:
        text=meta(text,'status','in_progress')
        text=meta(text,'verification','failed')
        text=meta(text,'summary','三倍显示限速已生效，但平滑体验失败；实测姿态缺帧导致停顿追赶，另有服务端导航跳变。')
        text=text.replace('- [ ] 用户验证实际效果；服务端异常位移来源与长间隔姿态供给仍待后续修复。','- [ ] 玩家平滑体验未通过：三倍限速生效，但样本缺口与服务端异常位移仍需源头修复。')
    text=text.replace('# Mass历史插值与三倍速度纠偏\n\n','# Mass历史插值与三倍速度纠偏\n\n'+feedback,1)
    path.write_text(text,encoding='utf-8')

note='用户确认三倍限速生效但平滑体验未通过；现场已抓到缺帧停顿后追赶，发送/接收分界与异常导航位移待修复。'
addition='''### 最新玩家反馈：三倍限速未解决停走（2026-09-23）

用户确认限速有效，但大量单位追赶影响体验，当前不能视为平滑验收通过。常规帧率的双客户端同兵对照抓到470ms样本缺口：服务器正常行走，缺帧端外推耗尽后停约0.24秒，再三帧三倍追赶；3FPS段的1.333秒供给单独归类。另有服务器6～16米异常位移，需独立修复。当前仍是原3倍实现，本轮没有改代码或运行参数。[诊断与证据边界](../../Archive/20260923-Mass姿态缺帧与三倍追赶现场诊断.md)。

'''
for path in (children[1],children[3]):
    text=path.read_text(encoding='utf-8')
    text=meta(text,'status_note',note)
    text=text.replace(docs.SPLIT_CONTENT_MARKER,docs.SPLIT_CONTENT_MARKER+addition,1)
    segment=docs.split_front_matter(text)[1].split(docs.SPLIT_CONTENT_MARKER,1)[1]
    text=meta(text,'split_segment_sha256',docs.normalized_payload_hash(segment,path))
    path.write_text(text,encoding='utf-8')
text=parent.read_text(encoding='utf-8')
old_hash=re.search(r'^split_payload_sha256: (.+)$',text,re.M)[1]
segments=[docs.split_front_matter(p.read_text(encoding='utf-8'))[1].split(docs.SPLIT_CONTENT_MARKER,1)[1] for p in children]
new_hash=hashlib.sha256(''.join(docs.transform_markdown_links(s,p) for s,p in zip(segments,children)).encode()).hexdigest()
text=text.replace(old_hash,new_hash)
text=meta(text,'split_previous_payload_sha256',old_hash)
text=meta(text,'split_revision','20260923-pose-starvation-diagnosis')
text=meta(text,'status_note',note)
text=text.replace('## 当前摘要\n','## 当前摘要\n\n- 2026-09-23最新反馈：三倍限速生效，但平滑体验未通过。常规帧率下已抓到缺帧端停顿后连续三帧三倍追赶；另有服务端异常导航位移。仅分析，尚未实施来源修复。[现场诊断](../Archive/20260923-Mass姿态缺帧与三倍追赶现场诊断.md)。\n',1)
parent.write_text(text,encoding='utf-8')
print('Recorded archive010, player feedback, development failure and current gameplay evidence.')
