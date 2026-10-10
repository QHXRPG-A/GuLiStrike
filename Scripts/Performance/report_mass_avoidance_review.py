"""Generate offline per-window/per-connection CSVs and an evidence inventory."""
from __future__ import annotations
import csv
import json
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Artifacts/MassAvoidance20261011'

def write_csv(path, rows):
    keys = list(dict.fromkeys(k for row in rows for k in row))
    with path.open('w',encoding='utf-8-sig',newline='') as f:
        writer = csv.DictWriter(f,keys)
        writer.writeheader()
        writer.writerows(rows)

def avg(rows, key):
    return statistics.fmean(r[key] for r in rows)

def main():
    data = json.loads((OUT/'analysis.json').read_text(encoding='utf-8'))
    windows, connections = [], []
    for row in data['windows']:
        flat={k:row[k] for k in ['run','scene','candidate','variant','round','moving_before','moving_after','moved','capture_errors','setup_errors','presentation_eligible','build']}
        flat.update(row['metrics'])
        flat.update(row['work'])
        for name,stats in row['csv'].items():
            for k,v in stats.items():flat[name+'_'+k]=v
        windows.append(flat)
        for world in row['network']:
            meta={k:v for k,v in world.items() if k!='connections'}
            for peer in world['connections']:
                connections.append({'run':row['run'],'scene':row['scene'],'variant':row['variant'],**meta,**peer})
        native=json.loads((ROOT/row['directory']/'native-capture.json').read_text(encoding='utf-8'))
        (ROOT/row['directory']/'network-timeseries.json').write_text(json.dumps({
            'bytes_per_kb':1000,'basis':'World and connection identified; endpoint rates are separate, never added',
            'worlds':[{'world':w['world'],'samples':w['network_samples']} for w in native]
        },ensure_ascii=False,indent=2),encoding='utf-8')
    write_csv(OUT/'window-summary.csv',windows)
    write_csv(OUT/'connection-summary.csv',connections)
    lines=['# Mass 避障与转向固定对照证据', '',
        '同进程专服＋双客户端＋编辑器；双客户端均为 1280×720。1 KB = 1000 B；每条连接预算为 250000 B/s。',
        '每轮重建 PIE、预热 10 秒、采样 30 秒。基线与候选来自同一个构建，仅切本地开关。',
        '压力窗口实际为 600 单位＋现有三来源 500 附加飞行物（167/167/166）。原计划四来源之一已退役，本轮未恢复；基线与候选一致，结论限定于实际场景。',
        '整帧时间包含服务器、两个客户端和编辑器工作，不能推算独立客户端 FPS。', '',
        '## 门槛', '', '| 场景 | 候选 | CPU 节省 ms/帧 | A/A CPU 波动 | 各配对 P95 差 ms | A/A P95 波动 | 原始门槛 |',
        '|---|---|---:|---:|---|---:|---|']
    if (OUT/'adoption.json').exists():
        decision=json.loads((OUT/'adoption.json').read_text(encoding='utf-8'))
        lines[7:7]=['采用决定：'+decision['summary'],
            '代码、失败配对诊断、网络归因与地图操作见[开发记录](../../Progress/DevelopmentDocumentation/20261011-Mass避障简化与转向提速.md)。', '']
    for g in data['groups']:
        deltas=', '.join(f"{p['p95_delta_ms']:+.3f}" for p in g['pairs'])
        saving=g['cpu_savings_mean_ms']
        outcome='待完成' if not g['complete'] else '通过' if g['p95_pass'] and g['cpu_pass'] else '未全通过'
        lines.append(f"| {g['scene']} | {g['candidate']} | {saving:.3f} | {g['aa_avoidance_noise_ms']:.3f} | {deltas} | {g['aa_frame_p95_noise_ms']:.3f} | {outcome} |" if saving is not None else f"| {g['scene']} | {g['candidate']} | — | {g['aa_avoidance_noise_ms']:.3f} | {deltas} | {g['aa_frame_p95_noise_ms']:.3f} | {outcome} |")
    lines += ['', 'CPU 指标为服务端预测避障总耗时＋软避障总耗时；其内部 Query/Grid/Prepare 不能再次相加。转向属于行为要求，CPU 节省门槛不适用，但仍检查整帧退化。原始门槛逐配对检查，失败样本保留，不用平均值覆盖失败。', '',
        '## 各窗口', '', '| 窗口 | 总避障 ms | 查询 μs/次 | 趋势/查询 | 缓存命中 | 整帧均值/P95 ms | GPU 均值/P95 ms | 枪口出生（两个客户端） |',
        '|---|---:|---:|---:|---:|---|---|---|']
    for r in data['windows']:
        m=r['metrics'];f=r['csv']['FrameTime'];gpu=r['csv']['GPUTime']
        lines.append(f"| {r['run']} | {m['CombinedAvoidanceMs']:.3f} | {m['query_us_per_query']:.3f} | {r['work']['trends_per_query']:.2f} | {r['work']['CacheHits']} | {f['mean']:.3f}/{f['p95']:.3f} | {gpu['mean']:.3f}/{gpu['p95']:.3f} | {','.join(str(x['muzzle_born']) for x in r['feedback'])} |")
    lines += ['', '## 网络', '', '| 场景/候选 | 服务器连接发送 KB/s（每条，均值） | 产生事件/墙钟秒 | 产生事件/模拟秒 | 发送调用/秒 | 飞行队列增长/秒 | 结束队首年龄 s |',
        '|---|---|---:|---:|---:|---:|---:|']
    for scene in ['dense200','stress']:
        for candidate in ['avoidance','turn','combined']:
            for variant in ['baseline',candidate]:
                group=[r for r in data['windows'] if r['scene']==scene and r['candidate']==candidate and r['variant']==variant]
                if not group:continue
                servers=[next(w for w in r['network'] if '/UEDPIE_0_' in w['world']) for r in group]
                peers=[p for w in servers for p in w['connections']]
                sends=', '.join(f'{avg([w["connections"][i] for w in servers],"send_bytes_per_second")/1000:.1f}' for i in range(2))
                lines.append(f'| {scene}/{candidate}/{variant} | {sends} | {avg(servers,"created_per_wall_second"):.1f} | {avg(servers,"created_per_world_second"):.1f} | {avg(peers,"flight_flush_calls_per_second"):.2f} | {avg(peers,"flight_queue_growth_per_second"):.1f} | {avg(peers,"flight_head_age_end_ms")/1000:.2f} |')
    lines += ['', 'UE 收发为连接层计数，飞行/单位字节为应用载荷，不能相加作为总出口；同一连接的服务器发送与客户端接收也不能相加。事件年龄和时钟估计误差见 connection-summary.csv 的独立列。DroppedShots 包括过期及其他拒绝原因，不能全部标为过期。', '',
        '压力场景枪口出生为零的窗口仅用于 CPU/网络诊断，不用于证明完整客户端表现收益。采样窗口运行错误与初始化阶段错误分别保留。', '',
        '## 原始证据', '',
        '- `analysis.json`：原始门槛及全部 World/连接统计；`window-summary.csv`、`connection-summary.csv`：便于筛选比较。',
        '- `Paired/<窗口>/frames.csv`、`session.utrace`：帧数据与原始 Trace。',
        '- `native-capture.json`、`network-timeseries.json`：原生阶段计数与每秒网络时间序列。',
        '- `result.json`、`counters-before/after.json`、`context-before/after.json`、`clock.json`、`runtime.log`：场景配置、前后状态、反馈、时钟与日志。',
        '- `Sessions/`：临时控制的原值/恢复值及正式配置哈希；`Build/verification.json`、构建日志及 `QA/results.json`。',
        '- `Attempt1/`：首版与无效早期采样，完整保留但不混入最终配对。',
        '', '默认采用：[adoption.json](adoption.json)；实际场景核对：[scenario-audit.json](scenario-audit.json)；行为：[Behavior/summary.json](Behavior/summary.json)；恢复及退出：[cleanup-final.json](cleanup-final.json)。']
    (OUT/'PairedReport.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(json.dumps({'windows':len(windows),'connections':len(connections),'report':str(OUT/'PairedReport.md')}))

if __name__=='__main__':main()
