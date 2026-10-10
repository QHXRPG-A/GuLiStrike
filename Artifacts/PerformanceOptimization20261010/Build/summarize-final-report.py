"""Offline closeout tables from completed captures; does not control the editor."""
from pathlib import Path
import collections
import csv
import json
import statistics

OUT = Path(__file__).resolve().parents[1]
analysis = json.loads((OUT / 'analysis.json').read_text(encoding='utf-8'))
runs = {r['name']: r for r in analysis['runs']}
labels = {'group': '小组索引', 'snapshot': '条件缓存', 'ui_balanced': 'UI 几何加权分块',
          'avoidance': '避障候选', 'combined': '条件缓存＋避障', 'selected': '避障隔离复测'}
cvars = {'group': 'gs.StateTree.GroupIndex', 'snapshot': 'gs.StateTree.ConditionSnapshot',
         'ui_balanced': 'gs.SceneUI.ParallelGeometry', 'avoidance': 'gs.Avoidance.ParallelCandidates'}
median = statistics.median


def save(name, value):
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def server_metrics(run):
    world = next(w for w in run['network'] if w['net_mode'] == 1)
    peers = [c['flight_peer'] for c in world['connections'] if 'flight_peer' in c]
    return {
        'created_per_second': world['created_per_second'],
        'ended_per_second': world['ended_per_second'],
        'produced_record_Bps': world.get('produced_record_bytes_per_second'),
        'server_two_connections_send_KBps': sum(c['send_bytes_per_second'] for c in world['connections']) / 1000,
        'mean_per_connection_enqueue_events_per_second': statistics.fmean(p['enqueued_events_per_second'] for p in peers),
        'mean_per_connection_sent_events_per_second': statistics.fmean(p['sent_events_per_second'] for p in peers),
        'mean_per_connection_queue_growth_per_second': statistics.fmean(p['queue_growth_per_second'] for p in peers),
        'mean_per_connection_flight_payload_KBps': statistics.fmean(p['sent_bytes_per_second'] for p in peers) / 1000,
        'mean_per_connection_flush_Hz': statistics.fmean(p['flush_calls_per_second'] for p in peers),
        'connections': world['connections'],
    }


network_pairs = []
for pair in analysis['pairs']:
    a, b = (runs[n] for n in pair['paths'])
    network_pairs.append({k: pair[k] for k in ['scene', 'candidate', 'round', 'paths']} | {
        'baseline': server_metrics(a), 'candidate_metrics': server_metrics(b)})
save('network-pairs.json', network_pairs)

view_groups, adoption = [], {}
for scene in ['dense200', 'stress']:
    threshold = analysis['decisions'][f'{scene}/ui_balanced']
    for candidate in ['ui_balanced', 'snapshot']:
        for view in ['full', 'mixed', 'offscreen', 'asymmetric']:
            group = sorted([p for p in analysis['view_pairs'] if p['scene'] == scene and
                            p['candidate'] == candidate and p['view'] == view], key=lambda p: p['round'])
            row = {'scene': scene, 'candidate': candidate, 'view': view, 'pairs': len(group),
                   'frame_p95_deltas_ms': [p['frame_p95_delta_ms'] for p in group],
                   'gt_p95_deltas_ms': [p['gt_p95_delta_ms'] for p in group],
                   'median_target_saved_ms': median([p['target_saved_ms'] for p in group]) if group else None,
                   'frame_p95_reference_ms': threshold['frame_p95_noise_ms'],
                   'gt_p95_reference_ms': threshold['gt_p95_noise_ms'],
                   'paths': [p['paths'] for p in group]}
            row['p95_within_reference'] = len(group) == 3 and all(
                p['frame_p95_delta_ms'] <= threshold['frame_p95_noise_ms'] and
                p['gt_p95_delta_ms'] <= threshold['gt_p95_noise_ms'] and p['capture_errors'] == 0 for p in group)
            view_groups.append(row)

for candidate, cvar in cvars.items():
    main = [analysis['decisions'][f'{s}/{candidate}']['accepted'] for s in ['dense200', 'stress']]
    views = [g for g in view_groups if g['candidate'] == candidate]
    view_ok = all(g['p95_within_reference'] for g in views) and len(views) == 8 if views else None
    isolation_ok = all(analysis['decisions'][f'{s}/selected']['accepted'] for s in ['dense200', 'stress']) if candidate == 'avoidance' else None
    eligible = all(main) and (view_ok is not False) and (isolation_ok is not False)
    adoption[cvar] = {'candidate': candidate, 'main_screening_passed_both_loads': all(main),
                      'additional_view_p95_check': view_ok, 'isolation_passed_both_loads': isolation_ok,
                      'recommended_default': int(eligible), 'reason': '完整默认采用门槛通过' if eligible else '存在未通过门槛的负载或视角，保持候选关闭'}
cleanup_path = OUT / 'cleanup-evidence.json'
observed_defaults = {}
if cleanup_path.exists():
    cleanup = json.loads(cleanup_path.read_text(encoding='utf-8-sig'))
    observed_defaults = {k: cleanup['controls'][k] for k in cvars.values()}
    assert all(observed_defaults[k] == value['recommended_default'] for k, value in adoption.items())
save('adoption.json', {'candidates': adoption, 'view_groups': view_groups,
                     'observed_final_runtime_defaults': observed_defaults,
                     'view_reference_limit': '视角追加检查沿用同 build4 原镜头 A/A 工程范围，未为每个视角另采 A/A；不是因果或统计置信结论。',
                     'existing_optimizations': '此前已采用的枪口/冲击批处理、数据池、LOD、裁剪、降频、投影缓存和避障查询优化保持原值。',
                     'actual_defaults_evidence': '最终以 cleanup-evidence.json 的恢复读回与 Build/final-static-verification.json 的源码默认值为准。'})

# capture_seconds is measured elapsed time (usually slightly above 30), not the requested duration.
formal = [r for r in runs.values() if r['name'].split('-')[0] in
          ['aa', 'aa_final', 'single', 'combined', 'selected', 'views'] and not r['result']['verification_enabled']]
eligible_formal = [r for r in formal if r['result'].get('view_eligible', True)]
connections = [c for r in formal for w in r['network'] for c in w['connections']]
validation = {'completed_captures': len(runs), 'completed_by_phase': dict(collections.Counter(n.split('-')[0] for n in runs)),
              'formal_30_second_captures': len(formal), 'eligible_formal_captures': len(eligible_formal),
              'formal_measured_duration_seconds_min_max': [min(r['result']['capture_seconds'] for r in formal), max(r['result']['capture_seconds'] for r in formal)],
              'excluded_calibration_captures': [r['name'] for r in formal if not r['result'].get('view_eligible', True)],
              'view_pairs_completed': len(analysis['view_pairs']),
              'all_observed_connection_budgets_250000': all(c['budget_min_Bps'] == c['budget_max_Bps'] == 250000 for c in connections),
              'max_queue_conservation_error': max(abs(c['flight_peer']['conservation_error']) for c in connections if 'flight_peer' in c),
              'formal_capture_errors': {r['name']: r['result']['capture_errors'] for r in formal if r['result'].get('capture_errors')},
              'failed_or_partial_windows_without_result': '保留在原目录和 Build/interruption.json；未伪造完成结果。',
              'functional': {}}
for name in ['dense200', 'stress', 'actor-dense200', 'move-continuity', 'move-continuity-baseline', 'business', 'business-baseline']:
    path = OUT / 'Functional' / name / 'report.json'
    if path.exists():
        report = json.loads(path.read_text(encoding='utf-8-sig'))
        validation['functional'][name] = {'passed': report.get('passed'), 'checks': report.get('checks'), 'errors': report.get('errors'), 'path': str(path.relative_to(OUT))}
save('validation-summary.json', validation)

lines = ['# 配对汇总与最终采用判断', '', '所有时间单位为 ms；正的“节省”表示目标成本下降，正的 P95 增量表示退化。原始配对、归一化工作量和全部阈值见 `analysis.json`。', '',
         '## 原镜头三轮配对', '', '| 候选 | 负载 | 目标节省中位数 | A/A 目标波动 | 整帧 P95 增量（三轮） | 单项门槛 |', '|---|---|---:|---:|---|---|']
for candidate, label in labels.items():
    for scene in ['dense200', 'stress']:
        d = analysis['decisions'][f'{scene}/{candidate}']
        p = [x for x in analysis['pairs'] if x['candidate'] == candidate and x['scene'] == scene]
        deltas = ' / '.join(f"{x['candidate_frame_p95_ms']-x['baseline_frame_p95_ms']:+.4f}" for x in p)
        lines.append(f"| {label} | {scene} | {d['target_saved_median_ms']:.4f} | {d['target_noise_ms']:.4f} | {deltas} | {'通过初筛' if d['accepted'] else '未通过'} |")
lines += ['', '小组/条件目标为 StateTree 调度，UI 为 Paint，避障为 Total；组合和隔离复测目标为整帧 GT 均值。表内不同目标不能相加。部分初筛为 build3，最终版本为 build4，逐对构建记录保留，不能混用两个构建的绝对成本。', '',
          '## 追加视角', '', 'P95 检查沿用同 build4 原镜头 A/A 范围：200 单位整帧 0.4225、GT 0.8081；压力整帧 4.9010、GT 5.6207。这里只作保守采用检查，没有逐视角 A/A，也不以此证明候选是退化原因。实际可见比例按 World 记录于 CSV/JSON，数组顺序不代表客户端编号。', '',
          '| 候选 | 负载 | 视角 | 整帧 P95 增量（三轮） | GT P95 增量（三轮） | 目标节省中位数 | P95 范围检查 |', '|---|---|---|---|---|---:|---|']
for g in view_groups:
    deltas = ' / '.join(f'{d:+.4f}' for d in g['frame_p95_deltas_ms'])
    gt_deltas = ' / '.join(f'{d:+.4f}' for d in g['gt_p95_deltas_ms'])
    cost = f"{g['median_target_saved_ms']:.4f}" if g['median_target_saved_ms'] is not None else '未完成'
    verdict = '范围内' if g['p95_within_reference'] else '未通过' if g['pairs'] == 3 else '不完整'
    lines.append(f"| {labels[g['candidate']]} | {g['scene']} | {g['view']} | {deltas} | {gt_deltas} | {cost} | {verdict} |")
lines += ['', '最终采用结果以 `adoption.json` 和恢复读回为准。全屏内高位镜头和压力零枪口出生样本只支持单位/UI CPU、几何合同和网络诊断，不用于证明枪口表现收益。', '',
          '## 组合压力下的事件产量与积压', '', '下表的队列增长是两个客户端连接的算术平均，事件产量为服务器单次产生量。没有把同一连接两端相加；完整逐连接值见 `network-pairs.json`。', '',
          '| 轮次/开关 | 出生＋结束事件/s | 每连接入队事件/s | 每连接发出事件/s | 每连接队列增长/s | 每连接实际 flush Hz |', '|---|---:|---:|---:|---:|---:|']
for pair in network_pairs:
    if pair['scene'] != 'stress' or pair['candidate'] != 'combined': continue
    for arm, key in [('串行', 'baseline'), ('组合', 'candidate_metrics')]:
        m = pair[key]
        lines.append(f"| {pair['round']}/{arm} | {m['created_per_second']+m['ended_per_second']:.2f} | {m['mean_per_connection_enqueue_events_per_second']:.2f} | {m['mean_per_connection_sent_events_per_second']:.2f} | {m['mean_per_connection_queue_growth_per_second']:.2f} | {m['mean_per_connection_flush_Hz']:.3f} |")
lines += ['', 'CPU 优化会改变实际每秒模拟推进量和事件产量。必须联合比较产生、入队、发出和队列守恒；本轮没有网络协议改动，也没有以产量减少或零真实反馈宣称网络/表现优化成功。', '',
          '## 分阶段成本代表窗口', '', '以下为 build4 原镜头单项第 1 轮，按各原生捕获的实际引擎帧数归一化；UI 合计两个客户端，避障为服务器。它是拆分证据，不替代三轮采用判定。ComputeCpu 是各工作批计时区间之和，DispatchJoin 是调用方墙钟，DispatchWait 包含调度与等待，三者不可相加。', '']
for candidate, prefix, phases in [
    ('ui_balanced', 'SceneUI', ['PrepareMs', 'PartitionMs', 'ComputeCpuMs', 'DispatchJoinMs', 'DispatchWaitMs', 'MergeMs', 'SubmitMs', 'PaintMs']),
    ('avoidance', 'Avoidance', ['PrepareMs', 'ComputeCpuMs', 'DispatchJoinMs', 'DispatchWaitMs', 'MergeMs', 'SubmitMs', 'TotalMs'])]:
    lines += [f'### {labels[candidate]}', '', '| 阶段 ms/引擎帧 | 200 串行 | 200 候选 | 600 串行 | 600 候选 |', '|---|---:|---:|---:|---:|']
    for phase in phases:
        values = []
        for scene in ['dense200', 'stress']:
            pair = next(p for p in analysis['pairs'] if p['scene'] == scene and p['candidate'] == candidate and p['round'] == 1)
            for name in pair['paths']:
                run = runs[name]
                value = run['counters'].get(f'{prefix}.{phase}', {}).get('per_engine_frame')
                values.append(f'{value:.5f}' if value is not None else '未记录')
        lines.append(f'| {phase} | ' + ' | '.join(values) + ' |')
    lines.append('')
lines += ['各轮完整 GT、RT、RHI、GPU、内存、原始 CPUUsage 与阶段成本见 `paired-summary.csv`。CPUUsage 是引擎原始计数，未据此推断其它进程造成卡顿。', '']
(OUT / 'RESULTS.md').write_text('\n'.join(lines), encoding='utf-8')
print(json.dumps({'completed': len(runs), 'view_pairs': len(analysis['view_pairs']), 'recommended_defaults': {k:v['recommended_default'] for k,v in adoption.items()}, 'validation': {k:v for k,v in validation.items() if k not in ['functional']}}, ensure_ascii=False))
