"""Analyze the completed moving-combat muzzle pairs, without operating PIE.

Frame eligibility and muzzle attribution are separate: a pressure scene with no
accepted muzzle events can describe frame cost but cannot prove batching gains.
"""
from __future__ import annotations

import argparse
import bisect
import json
import statistics
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts'))
from Performance.export_review_traces import export, analyze, rows
from Performance import analyze_stress_frames as frame_tools

OUT = ROOT / 'outputs/performance/20261010-muzzle-batch'
OWNER_TIMERS = {'GuLiMuzzles_Accept', 'GuLiMuzzles_Lifecycle', 'GuLiMuzzles_Publish'}


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def stats(values):
    if not values:
        return None
    values = sorted(values)
    return {'mean': statistics.fmean(values), 'median': statistics.median(values),
            'p95': values[min(len(values)-1, int(len(values)*.95))],
            'maximum': values[-1], 'n': len(values)}


def is_muzzle(name):
    return ('NS_MachineGunMuzzle_AllOptimizations' in name
            or 'NS_MachineGunMuzzle_Batch' in name)


def native(worlds):
    return next(w for w in worlds if 'native_population' in w)


def views(context):
    return [c for w in context['worlds'] for c in w['controllers'] if c['local']]


def frame_distribution(directory, summary):
    """Union selected GT scopes within each real engine frame; never sum parents.

    The union includes owner work plus actual muzzle asset-labelled scopes.
    Generic child work outside those scopes is deliberately not attributed.
    Exclusive asset cost comes separately from the full timer-statistics export.
    """
    dest = directory / 'insights-windowed'
    target = dest / 'muzzle-events.csv'
    begin, end = summary['trace_interval_seconds']
    if not target.exists():
        analyze(directory, [f'TimingInsights.ExportTimingEvents "{target.as_posix()}" '
                '-threads=GameThread -timers=GuLiMuzzles_*,*NS_MachineGunMuzzle_* '
                f'-startTime={begin:.9f} -endTime={end:.9f} '
                '-columns=TimerName,StartTime,EndTime,Duration,Depth'], dest)
    ticks = [r for r in rows(dest / 'engine-events.csv')
             if float(r['StartTime']) >= begin and float(r['EndTime']) <= end]
    starts = [float(r['StartTime']) for r in ticks]
    per_frame = [[] for _ in ticks]
    for r in rows(target):
        if r['TimerName'] not in OWNER_TIMERS and not is_muzzle(r['TimerName']):
            continue
        a, b = float(r['StartTime']), float(r['EndTime'])
        i = bisect.bisect_right(starts, a)-1
        if i < 0 or b > float(ticks[i]['EndTime']) + 1e-5:
            continue
        per_frame[i].append((a, b, float(r['Duration'])*1000, int(r['Depth'])))
    accounted = []
    for intervals in per_frame:
        # Parent-first native export plus depth distinguishes tiny rounded starts.
        stack = []
        total = 0.
        for a, b, duration, depth in intervals:
            while stack and (stack[-1][3] >= depth or a > stack[-1][1]):
                stack.pop()
            if not stack:
                total += duration
            stack.append((a, b, duration, depth))
        accounted.append(total)
    result = {'selected_gt_union_ms': stats(accounted),
              'scope': 'Union of GuLiMuzzles owner and real muzzle asset-labelled GT scopes, '
                       'excluding preview assets. Nested scopes counted once; this is an '
                       'attributed subset, not a complete end-to-end latency measurement.'}
    write(dest / 'muzzle-frame-distribution.json', result)
    return result


def representative_frames(directory, summary):
    begin, end = summary['trace_interval_seconds']
    ticks = [r for r in rows(directory / 'insights-windowed/engine-events.csv')
             if float(r['StartTime']) >= begin and float(r['EndTime']) <= end]
    ticks.sort(key=lambda r: float(r['Duration']))
    choices = {'median': ticks[len(ticks)//2],
               'p95': ticks[min(len(ticks)-1, int(len(ticks)*.95))], 'worst': ticks[-1]}
    dest = directory / 'selected-frames'
    dest.mkdir(exist_ok=True)
    commands = []
    for name, tick in choices.items():
        a, b = float(tick['StartTime']), float(tick['EndTime'])
        target = dest / f'{name}-events.csv'
        if not target.exists():
            commands.append(f'TimingInsights.ExportTimingEvents "{target.as_posix()}" '
                            f'-startTime={a:.9f} -endTime={b:.9f} '
                            '-columns=ThreadId,ThreadName,TimerId,TimerName,StartTime,EndTime,Duration,Depth')
    if commands:
        analyze(directory, commands, dest)
    result = []
    for name, tick in choices.items():
        a, b = float(tick['StartTime']), float(tick['EndTime'])
        threads = frame_tools.events(dest / f'{name}-events.csv', a, b)
        gt = threads.get('GameThread', [])
        engine_ms = float(tick['Duration'])*1000
        accounted = sum(e['exclusive_ms'] for e in gt)
        item = {'kind': name, 'trace_seconds': [a, b], 'engine_tick_ms': engine_ms,
                'gt_accounted_ms': accounted, 'accounting_ok': abs(accounted-engine_ms) < .15,
                'top_gt': frame_tools.aggregate(gt)[:40],
                'muzzle_gt': frame_tools.aggregate([e for e in gt if is_muzzle(e['timer'])
                                                   or e['timer'] in OWNER_TIMERS]),
                'threads': [{'name': key, 'exclusive_ms': sum(e['exclusive_ms'] for e in values),
                             'top_timers': frame_tools.aggregate(values)[:8]}
                            for key, values in threads.items()],
                'scope': 'One actual engine frame. Thread and GPU spans overlap; do not add '
                         'them to GT or infer server/client identity from anonymous UWorld order.'}
        write(dest / f'{name}-analysis.json', item)
        result.append(item)
    write(dest / 'frame-analysis.json', result)
    return result


def case(directory, with_frames=False):
    r = load(directory / 'result.json')
    before, after = [load(directory / f'counters-{suffix}.json')['worlds']
                     for suffix in ('before', 'after')]
    cb, ca = [load(directory / f'context-{suffix}.json') for suffix in ('before', 'after')]
    server_b, server_a = native(before), native(after)
    expected = 600 if r['scene'] == 'stress' else 200
    fixed_views = views(cb) == views(ca)
    frame_ok = (server_b['native_population']['alive'] == server_a['native_population']['alive']
                == expected and r['movement_units_displaced'] > 0 and fixed_views)
    if r['scene'] == 'stress':
        frame_ok = frame_ok and all(w.get('load', {}).get('domains') == [125]*4
                                   for w in (server_b, server_a))
    clients = []
    for b, a in zip(before[1:], after[1:]):
        fields = a['effects'].keys()
        delta = {k: a['effects'][k]-b['effects'][k] for k in fields
                 if k.startswith('muzzle_') and k not in ('muzzle_active', 'muzzle_components')}
        clients.append({'world': a['world'], 'delta': delta,
                        'components_end': a['effects']['muzzle_components'],
                        'active_slots_end': a['effects']['muzzle_active'],
                        'ui_before': b['ui'], 'ui_after': a['ui']})
    muzzle_ok = frame_ok and r['adoption_eligible']
    data = {'name': directory.name, 'scene': r['scene'], 'round': r['round'], 'mode': r['mode'],
            'frame_comparison_eligible': frame_ok, 'muzzle_gain_eligible': muzzle_ok,
            'csv': r['csv'], 'clients': clients,
            'moved_over_10cm': r['movement_units_displaced'],
            'moving_before': server_b['native_population']['moving'],
            'moving_after': server_a['native_population']['moving'],
            'load_before': server_b.get('load'), 'load_after': server_a.get('load'),
            'flight_network_before': server_b.get('flight_network'),
            'flight_network_after': server_a.get('flight_network'),
            'explanation': None if muzzle_ok else 'No actual muzzle feedback was accepted/born. '
                'Frame/workload data remains usable, but zero-event samples cannot prove muzzle gains.'}
    summary_path = directory / 'insights-windowed/summary.json'
    if summary_path.exists():
        summary = load(summary_path)
        data['cpu_eligible'] = summary['cpu_eligible']
        if summary['cpu_eligible']:
            timers = summary['timers']
            own = [t for t in timers if t['timer'] in OWNER_TIMERS]
            asset = [t for t in timers if is_muzzle(t['timer'])]
            preview = [t for t in timers if 'NS_MachineGunMuzzle_PressurePreview' in t['timer']]
            born = sum(c['delta']['muzzle_born'] for c in clients)
            asset_ms = sum(t['exclusive_ms_per_frame'] for t in asset)
            data['cpu'] = {'frames': summary['frames'], 'owner_scopes': own, 'asset_scopes': asset,
                'muzzle_asset_gt_self_ms_per_frame': asset_ms,
                'preview_asset_gt_self_ms_per_frame': sum(t['exclusive_ms_per_frame'] for t in preview),
                'muzzle_asset_all_cpu_self_ms_per_frame': sum(t['exclusive_ms_per_frame']
                    for t in summary['all_cpu_vfx'] if is_muzzle(t['timer'])),
                'parent_presentation': [t for t in timers if t['timer'] == 'GuLiCombatEffects_Presentation'],
                'top_gt': timers[:20],
                'asset_gt_self_ms_per_1000_born': asset_ms*summary['frames']*1000/born if born else None,
                'event_normalization': 'Both client Worlds; actual born-counter delta from snapshots '
                    'immediately outside the window. Minor boundary events make per-1000 cost approximate.',
                'frame_distribution': frame_distribution(directory, summary)}
            if with_frames:
                data['representative_frames'] = representative_frames(directory, summary)
    write(directory / 'analysis.json', data)
    return data


def comparison(cases, scene, baseline):
    a = [c for c in cases if c['scene'] == scene and c['mode'] == baseline]
    b = [c for c in cases if c['scene'] == scene and c['mode'] == 2]
    pairs = []
    for old in a:
        new = next(c for c in b if c['round'] == old['round'])
        x, y = [c['csv']['FrameTime']['mean'] for c in (old, new)]
        pairs.append({'round': old['round'], 'old_ms': x, 'new_ms': y,
                      'saved_ms': x-y, 'frame_reduction_percent': (1-y/x)*100,
                      'fps_increase_percent': (x/y-1)*100,
                      'muzzle_gain_eligible': old['muzzle_gain_eligible'] and new['muzzle_gain_eligible']})
    old_vals = [c['csv']['FrameTime']['mean'] for c in a]
    new_vals = [c['csv']['FrameTime']['mean'] for c in b]
    fluctuation = max(max(old_vals)-min(old_vals), max(new_vals)-min(new_vals))
    mean_old, mean_new = statistics.fmean(old_vals), statistics.fmean(new_vals)
    return {'baseline_mode': baseline, 'candidate_mode': 2, 'pairs': pairs,
            'old_mean_ms': mean_old, 'new_mean_ms': mean_new,
            'old_fps_from_mean': 1000/mean_old, 'new_fps_from_mean': 1000/mean_new,
            'frame_reduction_percent': (1-mean_new/mean_old)*100,
            'fps_increase_percent': (mean_old/mean_new-1)*100,
            'within_mode_mean_range_ms': fluctuation,
            'gain_confirmed': all(p['muzzle_gain_eligible'] for p in pairs)
                and min(p['saved_ms'] for p in pairs) > fluctuation,
            'adoption_limit': 'Pressure zero-event measurements do not establish muzzle improvement, '
                              'even if whole-frame numbers differ.' if scene == 'stress' else None}


def report(cases):
    groups = []
    for scene in ('dense200', 'stress'):
        for mode in (0, 1, 2):
            selected = [c for c in cases if c['scene'] == scene and c['mode'] == mode]
            metrics = {k: {'mean_of_three_means': statistics.fmean(c['csv'][k]['mean'] for c in selected),
                           'mean_of_three_p95': statistics.fmean(c['csv'][k]['p95'] for c in selected),
                           'mean_range': [min(c['csv'][k]['mean'] for c in selected),
                                          max(c['csv'][k]['mean'] for c in selected)]}
                       for k in selected[0]['csv']}
            groups.append({'scene': scene, 'mode': mode, 'metrics': metrics,
                           'all_frames_eligible': all(c['frame_comparison_eligible'] for c in selected),
                           'all_muzzle_gains_eligible': all(c['muzzle_gain_eligible'] for c in selected)})
    comparisons = {scene: {f'mode{m}_to_2': comparison(cases, scene, m) for m in (0, 1)}
                   for scene in ('dense200', 'stress')}
    result = {'scope': 'Whole source-editor process: one dedicated server and two clients. '
                       'Frame/FPS is not exclusive Client1 standalone performance.',
              'sampling': 'Each scene/mode: three paired samples, 10s warmup, 30s capture; '
                          'movement/avoidance/fire remain enabled. No screenshot/inventory/asset writes inside.',
              'memory_limit': 'Same editor process across sessions: allocator/cache/GC history is cumulative. '
                              'Reported process memory cannot establish isolated mode memory gains.',
              'groups': groups, 'comparisons': comparisons, 'cases': cases}
    write(OUT / 'performance-analysis.json', result)
    lines = ['# 枪口批量化运行与截帧结果', '',
             '同一源码编辑器进程包含一专服和双客户端。整帧/FPS 是该进程的共同帧率，不等于独立 Client1 成本。', '',
             '两种场景各三轮模式0/1/2，热身10秒、记录30秒；移动、避障及双方正常开火保留。测量窗口内没有截图、对象盘点或资源写入。', '',
             '| 场景 | 模式 | 整帧平均 ms | 三轮P95均值 ms | FPS | GT均值 ms | GPU均值 ms |',
             '|---|---|---:|---:|---:|---:|---:|']
    for g in groups:
        m = g['metrics']; f = m['FrameTime']
        lines.append(f"| {g['scene']} | {g['mode']} | {f['mean_of_three_means']:.2f} | "
                     f"{f['mean_of_three_p95']:.2f} | {1000/f['mean_of_three_means']:.2f} | "
                     f"{m['GameThreadTime']['mean_of_three_means']:.2f} | {m['GPUTime']['mean_of_three_means']:.2f} |")
    for scene, comps in comparisons.items():
        lines += ['', f'## {scene}', '']
        for key, c in comps.items():
            lines.append(f"- {key}: 帧耗时减少 {c['frame_reduction_percent']:.1f}%，FPS增加 "
                         f"{c['fps_increase_percent']:.1f}%；枪口收益{'已证实' if c['gain_confirmed'] else '未证实'}。")
    lines += ['', '压力场景真实枪口接纳/出生为零：可靠弹丸事件排队导致射击到达时过期。'
              '250000 B/s 是每客户端配置预算，500000 B/s 是总预算，不是任务管理器实测；'
              '提高临时预算到1 MB/s仍未解除积压。发送端每次最多8包，单包仍最多16条/1000字节。'
              '本轮保留正式网络配置；预览枪口独立资源单列，不能算作真实反馈收益。', '',
              '## 逐份负载与枪口证据', '',
              '| 样本 | 前/后移动状态 | >10cm净位移单位 | 双客户端实际出生 | 末端枪口组件 | GT枪口资源自身 ms/帧 | 每千次出生 GT资源自身 ms（近似） |',
              '|---|---:|---:|---:|---:|---:|---:|']
    for c in cases:
        born = sum(x['delta']['muzzle_born'] for x in c['clients'])
        cpu = c.get('cpu', {})
        cost = cpu.get('muzzle_asset_gt_self_ms_per_frame')
        per = cpu.get('asset_gt_self_ms_per_1000_born')
        cost_text = f'{cost:.3f}' if cost is not None else 'N/A'
        per_text = f'{per:.2f}' if per is not None else 'N/A'
        components = '/'.join(str(x['components_end']) for x in c['clients'])
        lines.append(f"| {c['name']} | {c['moving_before']}/{c['moving_after']} | {c['moved_over_10cm']} | "
                     f"{born} | {components} | {cost_text} | {per_text} |")
    lines += ['', '每千次反馈使用测量前后实际出生计数差，边界存在少量反馈，属近似归一化。'
              '资源自身取完整Trace的GT独占时间；父表现Scope、枪口承载Scope、异步CPU和等待另列JSON，禁止重复相加。', '',
              '进程内存受跨PIE的缓存、分配器及GC影响，各样本平均/P95保留在JSON，未宣称独立内存收益。', '',
              '完整数据：[performance-analysis.json](performance-analysis.json)。'
              '逐帧事件树在各样本 selected-frames；CPU统计与窗口核对在 insights-windowed。'
              'GPU Ribbon结果由测量外的补充ProfileGPU/静态资源读回单独记录。玩家视觉验收待操作。']
    lines += ['', '## 枪口CPU、P95与提交', '',
              '下表均为两个客户端合计、按真实引擎帧归一化，再取三轮均值。'
              '“承载+资源Scope并集”去除嵌套重复，仍只是已归属范围，不能再与资源自身或父Scope相加。', '',
              '| 模式 | 枪口资源GT自身 ms/帧 | 枪口资源全CPU线程自身 ms/帧 | 承载+资源Scope并集平均/P95 ms | 父表现Scope含子项 ms/帧 |',
              '|---|---:|---:|---:|---:|']
    for mode in (0, 1, 2):
        selected = [c for c in cases if c['scene'] == 'dense200' and c['mode'] == mode and c.get('cpu')]
        mean = lambda fn: statistics.fmean(fn(c) for c in selected)
        union = mean(lambda c: c['cpu']['frame_distribution']['selected_gt_union_ms']['mean'])
        p95 = mean(lambda c: c['cpu']['frame_distribution']['selected_gt_union_ms']['p95'])
        parent = mean(lambda c: sum(t['inclusive_ms_per_frame'] for t in c['cpu']['parent_presentation']))
        lines.append(f"| {mode} | {mean(lambda c:c['cpu']['muzzle_asset_gt_self_ms_per_frame']):.3f} | "
                     f"{mean(lambda c:c['cpu']['muzzle_asset_all_cpu_self_ms_per_frame']):.3f} | "
                     f"{union:.3f}/{p95:.3f} | {parent:.3f} |")
    lines += ['', '| 样本 | 双World接纳/出生 | 生命周期数组上传 | 姿态数组上传 | 姿态查询 |',
              '|---|---:|---:|---:|---:|']
    for c in cases:
        delta = lambda key: sum(x['delta'][key] for x in c['clients'])
        lines.append(f"| {c['name']} | {delta('muzzle_accepted')}/{delta('muzzle_born')} | "
                     f"{delta('muzzle_life_uploads')} | {delta('muzzle_pose_uploads')} | {delta('muzzle_pose_queries')} |")
    lines += ['', '第三轮批量补采中，实际出生量18968高于单次16104，净位移单位158高于68，'
              '并使用两个LOD组件；这些差异保留在结果中。整体FPS属于实际固定入口结果，'
              '不能宣称运动轨迹和交火时间线完全一致；每千次实际反馈成本用于补充比较。', '',
              '## 进程内存', '', '| 场景 | 模式 | 物理内存三轮均值/P95 MiB | 虚拟使用均值/P95 MiB |',
              '|---|---:|---:|---:|']
    for g in groups:
        mem = g['metrics']['PhysicalUsedMB']; virt = g['metrics']['VirtualUsedMB']
        lines.append(f"| {g['scene']} | {g['mode']} | {mem['mean_of_three_means']:.0f}/{mem['mean_of_three_p95']:.0f} | "
                     f"{virt['mean_of_three_means']:.0f}/{virt['mean_of_three_p95']:.0f} |")
    selected = next(c for c in cases if c['name'] == 'stress-r1-mode2')
    lines += ['', '## 压力场景的实际负载与截帧瓶颈', '',
              f"额外入口保持四来源各125枚；正常交火弹丸也在运行。服务器计数起/止："
              f"`{selected['flight_network_before'][0]}` → `{selected['flight_network_after'][0]}`。", '',
              '代表性压力样本GT独占时间（不能与含子项的World/父Scope重复相加）：', '',
              '| Scope | 平均 ms/引擎帧 | 最大单次 ms |', '|---|---:|---:|']
    for t in selected['cpu']['top_gt'][:14]:
        lines.append(f"| {t['timer']} | {t['exclusive_ms_per_frame']:.3f} | {t['max_call_ms']:.3f} |")
    lines += ['', '该样本中位/P95/最差实际GT引擎帧：'
              + ' / '.join(f"{f['engine_tick_ms']:.2f} ms" for f in selected['representative_frames'])
              + '。最差帧的 CombatEffectReplication 单次约28.9ms；SceneUI Paint约7.9ms/帧，'
                '避障候选查询约5.5ms/帧，Ship等 Projectile Movement约5.2ms含子项。'
                '这些属于本轮截帧发现，未在聚焦ID52的任务中改动其他管线。', '',
              '## GPU Ribbon', '',
              '六份旧/批量枪口资源均为CPU模拟，Ribbon Renderer的bUseGPUInit=false；'
              '运行Niagara.Ribbon.GpuInitMode=0，没有强制GPU初始化。因此ID52的GPU Ribbon初始化Dispatch为0。'
              '其他飞行/光束资源的GPU Ribbon工作由测量外的完整ProfileGPU另列：'
              '[资源读回](ribbon-renderer-readback.json)、[补充GPU记录](gpu-supplement/summary.json)。'
              'GPU平均/P95使用主采样CSV；补充GPU截帧不替代30秒窗口统计。']
    (OUT / 'performance-report.md').write_text('\n'.join(lines)+'\n', encoding='utf-8')
    print(json.dumps({'comparisons': comparisons, 'groups': groups}, ensure_ascii=False), flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--export', action='store_true')
    ap.add_argument('--workers', type=int, default=2)
    ap.add_argument('--frames', action='store_true')
    args = ap.parse_args()
    directories = sorted(p.parent for p in (OUT / 'paired').glob('*/result.json'))
    assert len(directories) == 18, len(directories)
    if args.export:
        with ThreadPoolExecutor(max_workers=args.workers) as pool:
            summaries = list(pool.map(export, directories))
        write(OUT / 'trace-window-summaries.json', summaries)
    cases = [case(p, args.frames and load(p / 'result.json')['round'] == 1) for p in directories]
    report(cases)


if __name__ == '__main__':
    main()
