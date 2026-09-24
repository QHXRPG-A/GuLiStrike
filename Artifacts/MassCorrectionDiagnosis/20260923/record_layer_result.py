import hashlib
import json
import re
import shutil
import sys
from collections import Counter
from pathlib import Path

root=Path('D:/UE5.7/test1')
a=Path(__file__).parent
log=(root/'Artifacts/CommanderPerformanceHUD/20260923/editor-reloaded.log').read_text(encoding='utf-8',errors='replace')
begin=log.index('LogPython: GULI_POSE_EFFECTIVE_BUDGET_BEGIN')
end=log.index('LogPython: GULI_POSE_EFFECTIVE_BUDGET_END',begin)
(a/'effective-budget.log').write_text(log[begin:end],encoding='utf-8')
layer=(a/'layer-network.log').read_text(encoding='utf-8')
drops=Counter(re.findall(r'Network saturated, not calling (\S+)',layer))
assert sum(drops.values())==4217
report={'duration_seconds':35,'dropped_pose_rpc_attempts':sum(drops.values()),'by_controller':dict(drops),
    'effective_connection_bytes_per_second':250000,'net_log_restored':'Log',
    'evidence_scope':'current PIE has confirmed server-side saturated unreliable RPC drops; historical frame6298 was not traced at send layer',
    'csv_capture':'not_started; STARTFILE selected filename only, STOP warned no capture; no CSV evidence used',
    'unresolved':['which traffic consumes the send budget','sustained throughput versus per-tick bursts','historical frame6298 exact drop path'],
    'traces':[]}
for prefix,name in (('layer-client1','prediction-baseline-20260923-142904-89418.csv'),('layer-client2','prediction-baseline-20260923-142907-89532.csv')):
    src=root/'outputs/commander-selection-20260831/diagnostics'/name
    shutil.copy2(src,a/name)
    j=json.loads((a/(prefix+'-analysis.json')).read_text())
    report['traces'].append({'file':name,'sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'fps':j['world_time_fps'],
        'maximum_sample_gap_seconds':j['sample_pairs']['local_dt']['max'],'cap_frames':j['display_at_3x_frames']})
(a/'layer-result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')

sys.path.insert(0,str(root/'.agents/skills/gulistrike-progress/scripts'))
import progress_docs as docs
archive=root/'Progress/Archive/20260923-Mass姿态RPC发送预算饱和诊断.md'
assert not archive.exists()
archive.write_text('''---
schema: guli-progress/v1
id: ARC-20260923-011
work_id: ''
kind: archive
role: root
title: Mass姿态RPC发送预算饱和诊断
areas: [commander, network, movement, performance]
categories: [gameplay, performance]
status: recorded
verification: partial
created: '2026-09-23'
updated: '2026-09-23'
summary: 当前PIE的35秒窗口确认4217次姿态RPC因连接发送预算饱和在UE发送入口被跳过；每连接有效限额250000字节每秒。
next_action: 分解按连接的复制流量和同帧突发量，设计姿态发送预算与调度修复；保留三倍纠偏兜底。
relations:
  work_items: [WORK-20260923-002, WORK-20260922-002]
status_note: 只做已有PIE被动诊断，临时LogNet VeryVerbose已恢复Log；未改游戏源码、发送限额、网络模拟或运行玩法，未编译或结束PIE。
---

# Mass姿态RPC发送预算饱和诊断

用户追问470ms间隔的成因及能否查明。本轮在PID5552、`/Game/Maps/LVL_CommanderMassPrototype`当前PIE中，利用引擎现成日志定位发送层，未加原生埋点或重启。470ms是两次有效姿态到达的间隔，不是该包在网络上传输了470ms。

## 直接证据

2026-09-23 14:28:34～14:29:09，短时将`LogNet`从`Log`调为`VeryVerbose`，35秒后自动恢复，并通过`Log list LogNet`读回确认。

共记录4217次：`Network saturated, not calling ...::ClientReceiveEncodedPoseBlock`，其中服务器PlayerController_0为2103次、PlayerController_1为2114次。这是被跳过的编码位置RPC块次数，一块可含多名单位，不是士兵数或UDP丢包率。详见[日志节选](../../Artifacts/MassCorrectionDiagnosis/20260923/layer-network.log)、[统计结果](../../Artifacts/MassCorrectionDiagnosis/20260923/layer-result.json)。

源码`UNetDriver::InternalProcessRemoteFunctionPrivate`（NetDriver.cpp:2929）在RPC不可靠、非组播且`Connection->IsNetReady()`为false时记录上述日志并直接返回。项目的`ClientReceiveEncodedPoseBlock`符合该类型。因而**这批更新在UE尝试发送前就被跳过**，无需真实互联网拥塞或高Ping即可发生。

`UNetConnection::IsNetReady()`通常检查`QueuedBits + SendBuffer.GetNumBits() <= 0`，若启用拥塞控制则走该控制器的就绪判断。Tick按`CurrentNetSpeed × 带宽计时步长 × 8`恢复字节预算，且对长帧计时进行限制。有限的每帧余额可被同帧的姿态和其他复制数据消耗；不能用平均Ping判断余额。

同期Soldier1862两客户端原生Trace平均36.80/36.92FPS，最长姿态间隔324/354ms，本窗口该兵未触发三倍追赶；这些直接证据证明发送跳过存在，但不能夸大为再次复现了先前Soldier764的同一470ms事件。日志自身有开销，常规帧率仍保持约37FPS，未启用网络模拟或新增压力。

## 有效限额与配置边界

Python常规属性读取报告Player/NetConnection等为protected，未通过内存访问绕过。改用引擎源码明确提供的只读`getall`控制台查询，四个活动IpConnection的`CurrentNetSpeed`均为250000，三份IpNetDriver的MaxClientRate/MaxInternetClientRate同为250000，单位字节/秒。[运行读回](../../Artifacts/MassCorrectionDiagnosis/20260923/effective-budget.log)。

另发现GameNetworkManager运行实例的TotalNetBandwidth=32000、AdjustedNetSpeed=7000，与DefaultEngine.ini中填写的500000等不同；该类声明`config=Game`，说明这组Engine配置未成为该实例值。但当前连接实际均为250000，**不能把7000误认成当前连接限额或据此归因470ms**。此配置位置问题留作后续修复核对。

本轮CsvProfile STARTFILE仅设置文件名，没有启动捕获；停止时明确报告无活动捕获，未得到CSV，不将其作为证据。结论依赖已恢复的引擎日志、原生单兵Trace和有效限额只读查询。

## 结论与下一步

当前局已证实一个直接源头：连接发送预算饱和→UE跳过不可靠姿态RPC→等下一次该单位有效更新→插值/外推耗尽后停顿→收到新样本后限速追赶。先前6298帧没有发送层日志，不能追溯认定该帧一定走此分支；那段已有证据仍见[上一轮诊断](20260923-Mass姿态缺帧与三倍追赶现场诊断.md)。

尚未区分是持续发送量超额，还是三相批量调度造成瞬时突发，也未量化具体由姿态、可靠名册、建筑、资源或战斗复制占用多少预算。下一步应按连接统计这些来源的字节数、每帧预算余额及姿态块发送/跳过原因，并用同一单位和块序号跨发送、接收、解码、入队对齐。修复应处理供给预算/调度与欠供给恢复，不把增加纠偏速度当作来源修复；历史服务器异常导航XY位移独立处理。

本轮未修改C++或地图、没有提高限额、没有编译、没有停止或重启PIE，没有Git提交推送。原图入口不变，当前平滑体验仍为未通过。
''',encoding='utf-8')

def meta(text,key,value):
    return re.sub(r'^'+re.escape(key)+r':.*$',key+': '+value,text,count=1,flags=re.M)

dev=root/'Progress/DevelopmentDocumentation/20260923-Mass历史插值与三倍速度纠偏.md'
parent=root/'Progress/Gameplay/指挥官.md'
children=[root/'Progress/Gameplay/指挥官'/name for name in ('01-战局选兵与移动.md','02-UI表现与性能.md','03-数据技能与武器表现.md','04-验证边界.md','05-数值与演进.md')]
(a/'layer-docs-before.json').write_text(json.dumps({str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in (dev,parent,*children)},indent=2),encoding='utf-8')
text=dev.read_text(encoding='utf-8')
text=meta(text,'summary','三倍限速生效但体验失败；35秒现场确认4217次姿态RPC被发送预算饱和检查跳过，来源修复待实施。')
text=meta(text,'next_action','分解每连接复制流量及同帧突发，修复姿态发送预算与调度，再核对缓冲恢复和导航异常XY。')
text=text.replace('## 玩家反馈与现场诊断（2026-09-23）','''## 最新：发送层饱和已取得直接证据（2026-09-23）

用户追问470ms来源后，在原PIE短时开启现成网络日志，35秒记录4217次位置RPC被UE以Network saturated直接跳过；有效连接限额250000字节/秒。持续流量超额与同帧突发仍待拆分，旧6298帧无发送层记录，不能倒推具体原因。日志已恢复，未改玩法参数或编译。[追加诊断](../Archive/20260923-Mass姿态RPC发送预算饱和诊断.md)。

## 玩家反馈与现场诊断（2026-09-23）''',1)
dev.write_text(text,encoding='utf-8')
addition='''### 最新：姿态发送预算饱和证据（2026-09-23）

35秒原PIE采样记录4217次位置RPC被UE发送预算检查跳过；连接实际限额250000字节/秒，同期原生Trace约37FPS。低Ping不能排除这种应用内发送跳过。持续流量与同帧突发仍待分解，历史470ms对应块未记录发送层，因此不倒推单帧原因；本轮只诊断且日志级别已恢复，平滑体验仍待源头修复。[证据及边界](../../Archive/20260923-Mass姿态RPC发送预算饱和诊断.md)。

'''
for p in (children[1],children[3]):
    text=p.read_text(encoding='utf-8').replace(docs.SPLIT_CONTENT_MARKER,docs.SPLIT_CONTENT_MARKER+addition,1)
    text=meta(text,'status_note','已确认姿态RPC受发送预算饱和而跳过；流量来源及调度修复未实施，三倍限速不等于体验通过。')
    segment=docs.split_front_matter(text)[1].split(docs.SPLIT_CONTENT_MARKER,1)[1]
    text=meta(text,'split_segment_sha256',docs.normalized_payload_hash(segment,p))
    p.write_text(text,encoding='utf-8')
text=parent.read_text(encoding='utf-8')
old_hash=re.search(r'^split_payload_sha256: (.+)$',text,re.M)[1]
segments=[docs.split_front_matter(p.read_text(encoding='utf-8'))[1].split(docs.SPLIT_CONTENT_MARKER,1)[1] for p in children]
new_hash=hashlib.sha256(''.join(docs.transform_markdown_links(s,p) for s,p in zip(segments,children)).encode()).hexdigest()
text=text.replace(old_hash,new_hash)
text=meta(text,'split_previous_payload_sha256',old_hash)
text=meta(text,'split_revision','20260923-pose-send-saturation')
text=meta(text,'status_note','当前PIE确认发送预算饱和跳过位置RPC；已恢复日志，流量/调度修复及玩家体验仍待完成。')
text=text.replace('## 当前摘要\n','## 当前摘要\n\n- 2026-09-23发送层追加诊断：35秒确认4217次姿态RPC因发送预算饱和被跳过，有效限额250000字节/秒；当前仅诊断，流量与调度修复待实施。[证据](../Archive/20260923-Mass姿态RPC发送预算饱和诊断.md)。\n',1)
parent.write_text(text,encoding='utf-8')
print(json.dumps(report,indent=2))
