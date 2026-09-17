"""Render the measured six-run pose comparison, retaining individual rounds and metric definitions."""
from __future__ import annotations
import json
from pathlib import Path
from statistics import median
import argparse


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('root', type=Path)
    args = parser.parse_args()
    root = args.root
    comparison = json.loads((root / 'comparison.json').read_text(encoding='utf-8'))
    assert comparison['valid_comparison_load']
    runs = comparison['runs']
    baseline, optimized = runs[:3], runs[3:]
    roles = ('server', 'client1', 'client2')
    labels = {'server': '服务端（两连接合计）', 'client1': '客户端1', 'client2': '客户端2'}
    text = []
    def line(value=''): text.append(value)
    def number(value, digits=3): return '—' if value is None else f'{value:,.{digits}f}'
    def change(before, after): return '新增' if not before else f'{(after / before - 1) * 100:+.2f}%'
    def med(group, role, getter): return median(getter(r['roles'][role]) for r in group)
    def compare_table(columns, metrics):
        line('| 进程 | 指标 | 优化前 | 优化后 | 变化 |')
        line('|---|---|---:|---:|---:|')
        for role in columns:
            for title, getter, divisor in metrics:
                a, b = med(baseline, role, getter) / divisor, med(optimized, role, getter) / divisor
                line(f'| {labels[role]} | {title} | {number(a)} | {number(b)} | {change(a,b)} |')
        line()
    def rpc_metric(role_data, names, field):
        return sum(value[field] for name, value in role_data['outbound_profile']['rpcs'].items() if name in names)
    pose_names = ('ClientReceiveSoldierPoseChunk', 'ClientReceiveEncodedPoseBlock')
    ack_names = ('ServerAcknowledgePoseBlocks',)
    line('# 指挥官姿态压缩：协议8→9实测')
    line()
    line('源码版 UE5.7；每轮一个 NullRHI 服务端、两个真实 D3D12 渲染客户端，1,200 单位持续折返。前后各三轮均通过实际移动人数、10Hz步进和解码有效性检查。下面的“前/后”为各组三轮中位数，所有单轮数据保留。')
    line()
    line('服务端下行375.09→134.46 KB/s（−64.15%），姿态参数333.52→107.58 KB/s（−67.74%）。新增ACK约0.8 KB/s/客户端。整进程CPU未表现出稳定的整体下降：服务端均值−0.69%，客户端1+3.43%、客户端2−1.82%；新增缓存为服务端两连接合计4.35 MiB、每客户端2.06 MiB。')
    line()
    line('GitHub开发前备份：[4a2d831e818f62b3869af245655c3970faec9ec6](https://github.com/QHXRPG-A/GuLiStrike/commit/4a2d831e818f62b3869af245655c3970faec9ec6)。原始旧截帧已按约定忽略并提交清单；本次截帧也留本地，路径/大小/SHA-256见 [raw-trace-manifest.json](raw-trace-manifest.json)。')
    line()
    line('## 数据量')
    line()
    line('KB/s采用十进制1,000字节；网络总量采用实际连接的In/OutTotalBytes增量，包含UE与IP/UDP开销。RPC和姿态参数是总量内部归因，不能再次加到总量上。')
    line()
    compare_table(roles, [
        ('网络入站 KB/s', lambda v: v['network']['in_bytes_per_second'], 1000),
        ('网络出站 KB/s', lambda v: v['network']['out_bytes_per_second'], 1000),
        ('出站包/s', lambda v: v['outbound_profile']['packets_per_second'], 1),
    ])
    compare_table(('server',), [
        ('姿态参数 KB/s', lambda v: rpc_metric(v, pose_names, 'parameter_bytes_per_second'), 1000),
        ('姿态RPC含RPC封装 KB/s', lambda v: rpc_metric(v, pose_names, 'bytes_per_second'), 1000),
        ('姿态RPC/s', lambda v: rpc_metric(v, pose_names, 'calls') / 30, 1),
    ])
    compare_table(('client1', 'client2'), [
        ('新增姿态ACK参数 KB/s', lambda v: rpc_metric(v, ack_names, 'parameter_bytes_per_second'), 1000),
        ('新增姿态ACK含RPC封装 KB/s', lambda v: rpc_metric(v, ack_names, 'bytes_per_second'), 1000),
        ('姿态ACK RPC/s', lambda v: rpc_metric(v, ack_names, 'calls') / 30, 1),
    ])
    line('姿态块内部采用ID差值、10位字段掩码与预测残差；RPC封装不含外层bunch/IP/UDP。ACK会与已有连接流量复用数据包，所以它的独立RPC字节数不等于上行总量的净增量。')
    line()
    line('## CPU与帧时间')
    line()
    line('1核心当量表示每秒消耗1秒CPU时间；进程数据以约1秒QPC采样计算。机器为i9-14900KF（24核/32线程）、32GiB系统内存；两个渲染客户端使用同一D3D12适配器，驱动记录见各轮日志。')
    line()
    compare_table(roles, [
        ('平均核心当量', lambda v: v['cpu_core_equivalents']['mean'], 1),
        ('P95核心当量', lambda v: v['cpu_core_equivalents']['p95'], 1),
        ('GameThread活动墙钟 平均ms', lambda v: v['trace_cpu']['game_thread_active_ms']['mean'], 1),
        ('GameThread活动墙钟 P95ms', lambda v: v['trace_cpu']['game_thread_active_ms']['p95'], 1),
        ('世界帧P95 ms', lambda v: v['frame_ms']['p95'], 1),
        ('平均世界帧率', lambda v: 1000 / v['frame_ms']['mean'], 1),
    ])
    compare_table(('server',), [
        ('固定步平均 ms', lambda v: v['fixed_step_ms']['mean'], 1),
        ('固定步P95 ms', lambda v: v['fixed_step_ms']['p95'], 1),
    ])
    line('GameThread活动墙钟 = `FEngineLoop::Tick` 扣除 `FEngineLoop_UpdateTimeAndHandleMaxTickRate`，其他等待仍保留；它不是CPU占用率。客户端不限帧，CPU用量需要结合实际帧率理解。')
    line()
    line('| 进程 | 共用测量边界 | 前 ms/s | 后 ms/s | 变化 |')
    line('|---|---|---:|---:|---:|')
    for role in roles:
        for op, label in (('capture','权威姿态采样'),('send','发送，含编码与RPC提交'),('wire_read','原生载荷读取'),('receive','接收，含解码与入队'),('ack','ACK构造/确认与提交')):
            a = med(baseline, role, lambda v: v['timing'][op]['ms_per_second'])
            b = med(optimized, role, lambda v: v['timing'][op]['ms_per_second'])
            if a or b: line(f'| {labels[role]} | {label} | {number(a)} | {number(b)} | {change(a,b)} |')
    line()
    line('| 协议9函数 | 进程 | 调用均值 µs | P95 µs | 合计 ms/s |')
    line('|---|---|---:|---:|---:|')
    for role, scopes in [('server',('GuLiPose_Encode','GuLiPose_Confirm')),('client1',('GuLiPose_Decode',)),('client2',('GuLiPose_Decode',))]:
        for name in scopes:
            def scope(v): return v['trace_cpu']['pose_scopes_ms'][name]
            line(f'| `{name}` | {labels[role]} | {number(med(optimized, role, lambda v: scope(v)["mean"]) * 1000)} | {number(med(optimized, role, lambda v: scope(v)["p95"]) * 1000)} | {number(med(optimized, role, lambda v: scope(v)["ms_per_second"]))} |')
    line()
    line('新函数计时嵌套在共用发送/接收/ACK边界中，不能再次累加。每轮`insights/timers-gamethread.csv`和`events-project.csv`保留函数级统计与时间线；分析排除了导出器混入的GPU行。')
    line()
    line('接收函数单项变慢主要反映解码职责移入该函数；应合并原生读取、接收和ACK后再比较整个客户端链路。')
    line('')
    compare_table(roles, [('协议链路合计 ms/s', lambda v: sum(v['timing'][op]['ms_per_second']
                                                           for op in ('capture','send','receive','wire_read','ack')), 1)])
    line('## 内存')
    line()
    line('MiB为1,048,576字节。工作集、私有提交是整个进程的采样值；缓存容量单独采用UE容器实际容量API，包含空容器基础开销，不能用进程内存的小幅波动反推缓存是否分配。')
    line()
    compare_table(roles, [
        ('工作集平均 MiB', lambda v: v['working_set_mib']['mean'], 1),
        ('工作集P95 MiB', lambda v: v['working_set_mib']['p95'], 1),
        ('工作集峰值 MiB', lambda v: v['working_set_mib']['max'], 1),
        ('私有提交平均 MiB', lambda v: v['private_commit_mib']['mean'], 1),
        ('私有提交P95 MiB', lambda v: v['private_commit_mib']['p95'], 1),
        ('私有提交峰值 MiB', lambda v: v['private_commit_mib']['max'], 1),
        ('编解码缓存平均 MiB', lambda v: v['codec_cache_bytes']['mean'], 1048576),
        ('编解码缓存P95 MiB', lambda v: v['codec_cache_bytes']['p95'], 1048576),
        ('编解码缓存峰值 MiB', lambda v: v['codec_cache_bytes']['max'], 1048576),
    ])
    line('## 三轮原始对比与有效性')
    line()
    line('| 组/轮 | 服务端出站KB/s | 客户端1入站KB/s | 客户端2入站KB/s | 服务端平均核 | 客户端1平均核 | 客户端2平均核 |')
    line('|---|---:|---:|---:|---:|---:|---:|')
    for r in runs:
        v = r['roles']
        values = [v['server']['network']['out_bytes_per_second']/1000, v['client1']['network']['in_bytes_per_second']/1000,
                  v['client2']['network']['in_bytes_per_second']/1000] + [v[role]['cpu_core_equivalents']['mean'] for role in roles]
        line(f'| [{r["run"]}]({r["run"]}/comparison-window.json) | ' + ' | '.join(number(value) for value in values) + ' |')
    line()
    line('| 组/轮 | 三端移动人数P05 | 服务器Hz / 丢步 | 客户端1有效姿态Hz / 间隔均值/P95 ms | 客户端2有效姿态Hz / 间隔均值/P95 ms | 解码失败 |')
    line('|---|---|---|---|---|---:|')
    for r in runs:
        v=r['roles']; s=v['server']
        fields=[]
        for role in ('client1','client2'):
            c=v[role]; gaps=c['sample_intervals']
            fields.append(f'{c["mean_unit_pose_hz"]:.3f} / {gaps["mean_ms"]:.2f}/{gaps["p95_ms"]}')
        line(f'| {r["run"]} | ' + '/'.join(str(int(v[role]['actual_moving_p05'])) for role in roles)
             + f' | {s["effective_sim_hz"]:.4f} / {s["dropped_steps"]} | ' + ' | '.join(fields)
             + f' | {int(v["client1"]["decode_failures"] + v["client2"]["decode_failures"])} |')
    line()
    line('有效姿态Hz按实际完整解码样本数÷1,200÷窗口时长计算；间隔来自同一SoldierId相邻新样本的服务器模拟步差，未拿“收到某帧一个块”替代整帧或单兵确认。保留近处10Hz、远处/静止5Hz降频。超过200ms的间隔数量可在各轮逐单位间隔直方图中检查。')
    line('所有轮次存活数为1,200。优化后第3轮移动人数P05为1199/1200/1199，其余轮次P05均为1200；这只表示分位数差异1人（0.083%），不表示逐帧最多差1人。以下同时列出逐帧平均和最低值，保留转向、接收与显示中的短时波动；未将请求人数直接当作实际移动人数。')
    line()
    line('| 组/轮 | 服务端移动人数 平均/最低 | 客户端1移动人数 平均/最低 | 客户端2移动人数 平均/最低 |')
    line('|---|---:|---:|---:|')
    for r in runs:
        line(f'| {r["run"]} | ' + ' | '.join(f'{r["roles"][role]["actual_moving"]["mean"]:.3f}/{r["roles"][role]["actual_moving"]["min"]:.0f}' for role in roles) + ' |')
    line()
    line('基线第3轮客户端1逐帧最低为1160，属于原始观测，未作为异常点删除。服务器Hz来自共同30秒内实际固定步数；全采集Hz另保留在JSON的full_capture_sim_hz。内存峰值行仍为各轮窗口峰值的中位数，各轮原始峰值可查JSON。')
    line()
    line('## 复现与证据')
    line()
    line('引擎：`D:/UnrealEngine-5.7`；地图：`/Game/Maps/LVL_CommanderMassPrototype`；1,200单位8秒折返，先加入再以100人批次出兵。客户端1920×1080、Epic、屏幕比例100%、VSync关闭、镜头锁定、真实渲染但不显示窗口；服务端NullRHI、世界帧上限60。客户端采40秒、服务端50秒，取最晚开始采集的进程之后5秒起的共同30秒。')
    line()
    line('```powershell')
    line('& ./Scripts/run_commander_move_stress.ps1 -Population 1200 -Clients 2 -Seconds 40 `')
    line('  -Label optimized-1 -EvidenceRoot TestResults/CommanderPoseCompression `')
    line('  -JoinBeforePopulation -CaptureNetwork -CaptureTrace -Port 18081')
    line('python Scripts/analyze_commander_pose_compression.py TestResults/CommanderPoseCompression --run optimized-1-n1200-c2')
    line('python Scripts/export_commander_pose_traces.py TestResults/CommanderPoseCompression/optimized-1-n1200-c2')
    line('python Scripts/analyze_commander_pose_compression.py TestResults/CommanderPoseCompression')
    line('python Scripts/report_commander_pose_compression.py TestResults/CommanderPoseCompression')
    line('```')
    line()
    line('目录已存在时启动器拒绝覆盖，重跑须换Label和端口。三轮基线使用18071–18073，优化后三轮使用18081–18083；完整参数、PID和配置在各轮`launch.json`。')
    line()
    line('- [baseline-source-manifest.json](baseline-source-manifest.json)及`baseline-source/`：协议8与完整共用采集点；基于GitHub备份恢复这些文件即可重建基线，基线阶段不存在协议9编解码器文件。')
    line('- [optimized-source-manifest.json](optimized-source-manifest.json)及`optimized-source/`：采样时协议9源码SHA与副本。采样后仅移除两处文件末尾空行，见[格式记录](post-capture-format.json)。')
    line('- [verified-build-id.json](verified-build-id.json)、[最终Editor构建](final-gate-editor.log)、[最终Game构建](final-gate-game.log)：源码版双目标成功，BuildId一致。')
    line('- [自动化结果](verified-automation/index.json)：65项成功、0失败、0未运行；含23项网络、34项导航与8项战斗既有/获准协议测试。')
    line('- [comparison.json](comparison.json)：全部单轮指标、分连接流量、CPU/内存、逐单位间隔及函数统计。')
    line('- 每轮三端`frames.csv`、`pose-metrics.csv`、`network.csv`、`network.nprof`、`engine.csv`、`process.utrace`；根目录`processes.csv`提供三进程CPU/内存QPC时间线。')
    line()
    line('原始截帧由源码版运行进程生成，Unreal Insights 5.7离线导出。通过`$Trace.NewTrace`的StartCycle/CycleFrequency读取QPC原点；Windows UE单调时钟为QPC秒+2^24。三端网络、进程及截帧均按同一窗口对齐，CSV引擎汇总另标明full_capture，不混入共同30秒统计。')
    line()
    line('## 解释边界')
    line()
    line('本次收益同时包含位置/速度/朝向精度调整、预测残差、字段省略、ID差值及移除动态锚点处理；没有量化单独实验，不能把收益全部归给预测差分。没有引入Oodle或rANS。新增缓存是确定的内存代价，进程整体内存受资产和分配器影响；三轮样本只支持同机相对比较。')
    line()
    line('此前的`baseline-1`、`baseline-final-*`为试采，后者缺少逐单位间隔直方图，均未纳入最终六轮比较。实际回环网络与编解码单测已运行；跨机、真实公网劣网和生产专用服务器容量未在本次测量。')
    (root / 'REPORT.md').write_text('\n'.join(text) + '\n', encoding='utf-8')
    print(root / 'REPORT.md')


if __name__ == '__main__': main()
