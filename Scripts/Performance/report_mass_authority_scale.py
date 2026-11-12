"""Compare frozen 500/1000-mover captures without launching gameplay."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics
import struct

from report_circular_avoidance import summarize, read


def details(path):
    row, recipe = summarize(path)
    capture = read(path/'capture.json')
    movement = capture['movement']
    row['counter_totals'] = {k:v['total'] for k,v in capture['performance']['counters'].items()}
    hashes = []
    for frame in movement['observations']:
        h = hashlib.sha256()
        for member in frame['members']:
            h.update(struct.pack('<I3d4I', member['id'], *[round(v, 5) for v in member['position']],
                member['state'], member['order'], member['completed_order'], member['failed_order']))
        hashes.append(h.hexdigest())
    starts = {m['id']:m['position'] for m in recipe['members']}
    goals = {i:c['target'] for c in recipe['commands'] for i in c['members']}
    row['mean_goal_progress_cm'] = statistics.fmean(
        math.dist(starts[m['id']][:2], goals[m['id']][:2])-
        math.dist(m['final_position'][:2], goals[m['id']][:2])
        for m in movement['members'] if m['id'] in goals)
    row['frame_hashes'] = hashes
    return row, recipe


def mean(rows, key):
    return statistics.fmean(row[key] for row in rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path('Artifacts/MassAuthority20261111'))
    args = parser.parse_args()
    root = args.root.resolve()
    suites = {v:read(root/f'{v}-suite.json') for v in ('baseline','mass')}
    output = {'cases':{}}
    lines = ['# Mass 权威数据迁移：500 / 1000 移动单位对比', '',
             '源码版 UE5.7，GuLiStrikeEditor Win64 Development，地图 `/Game/Maps/LVL_CommanderMassPrototype`。'
             '两版各 4 场景 × 3 次 × 30 秒，保留 632 单位开局，再加入 500 或 1000 个本地权威移动单位；'
             '人墙场景另含静止单位。位置、兵种、命令、场景、相机和画质使用同一冻结快照。', '',
             '新增负载走正式单位初始化、NavMesh 规划、圆形侧绕、0.5 秒让路和位移提交，'
             '计时外直接准备，额外单位不做 Hybrid 网络分发。此报告比较移动 CPU，不代表千单位网络性能。', '',
             '[实现说明、相同轨迹对照与玩家操作](行为与使用说明.md)。', '',
             '## 主线程成本', '',
             '单位：ms / 10 Hz 逻辑步。包含该步的权威处理；分项存在包含关系，不能相加。'
             '串行侧绕、让路和导航提交仍在主线程，移动工作线程成本为 0。', '',
             '| 场景 | 基线均值 | Mass 均值 | 变化 | 最大重复极差 | 判断 |',
             '|---|---:|---:|---:|---:|---|']
    for case in suites['baseline']:
        data, reference = {}, None
        for version in ('baseline','mass'):
            data[version] = []
            for name in suites[version][case]:
                row, recipe = details(root/name)
                if reference is None:
                    reference = recipe
                assert recipe == reference, f'Frozen recipe differs: {name}'
                data[version].append(row)
        a, b = data['baseline'], data['mass']
        old, new = mean(a,'movement_ms_per_logic_step'),mean(b,'movement_ms_per_logic_step')
        noise = max(max(r['movement_ms_per_logic_step'] for r in rows)-
                    min(r['movement_ms_per_logic_step'] for r in rows) for rows in (a,b))
        judgment = '下降超出重复波动' if old-new>noise else '上升超出重复波动' if new-old>noise else '未超出重复波动'
        data['comparison'] = {'old_ms':old,'new_ms':new,'change_percent':(new/old-1)*100,
                              'repeat_range_ms':noise,'timing_judgment':judgment}
        data['first_different_step_after_by_before'] = [[next((i+1 for i,(x,y) in enumerate(zip(rb['frame_hashes'],ra['frame_hashes'])) if x!=y),None)
                                                        for ra in a] for rb in b]
        output['cases'][case] = data
        lines.append(f'| {case} | {old:.3f} | {new:.3f} | {(new/old-1)*100:+.1f}% | {noise:.3f} | {judgment} |')
    lines += ['', '## 活动数量、尾部成本与行进结果', '',
              '| 场景 | 版本 | 平均活动数 | 峰值范围 | P95 范围 ms | P99 范围 ms | µs/活动单位步 | 到达数（3轮） | 平均接近目标 cm | 连续反向最大值（3轮） |',
              '|---|---|---:|---|---|---|---:|---|---:|---|']
    for case, data in output['cases'].items():
        for version in ('baseline','mass'):
            rows = data[version]
            extent = lambda values:f'{min(values):.3f}–{max(values):.3f}'
            tails = [extent([r['movement_total_per_step_ms'][p] for r in rows]) for p in ('p95','p99')]
            peaks = [r['peak_active_units'] for r in rows]
            arrivals = '/'.join(str(r['arrived']) for r in rows)
            reversals = '/'.join(str(r['movement_behavior']['maximum_consecutive_reversals']) for r in rows)
            lines.append(f"| {case} | {version} | {statistics.fmean(r['active_units']['mean'] for r in rows):.1f} | "
                         f"{min(peaks)}–{max(peaks)} | {tails[0]} | {tails[1]} | {mean(rows,'movement_ms_per_active_unit_step')*1000:.2f} | "
                         f"{arrivals} | {mean(rows,'mean_goal_progress_cm'):.1f} | {reversals} |")
    lines += ['', '## 分项与工作量', '',
              '| 场景 | 版本 | 索引 ms/步 | 侧绕求解 ms/步 | 提交（含求解）ms/步 | NavMesh ms/步 | 桶访问/步 | 圆检测/步 | 侧绕探测/步 | 让路请求/步 |',
              '|---|---|---:|---:|---:|---:|---:|---:|---:|---:|']
    keys = ('Movement.GT.CircleIndexMs','Movement.GT.CircleSolveMs','Movement.GT.CommitInclusiveMs',
            'Movement.GT.NavMeshMoveMs','Movement.CircleBucketVisits','Movement.CircleChecks','Movement.SideProbes','Movement.YieldRequests')
    for case,data in output['cases'].items():
        for version in ('baseline','mass'):
            values = [statistics.fmean(r['metrics_per_step'].get(k,{}).get('mean',0) or 0 for r in data[version]) for k in keys]
            lines.append(f'| {case} | {version} | '+' | '.join(f'{v:.3f}' for v in values)+' |')
    lines += ['', '## 停滞、重寻路与内存', '',
              '| 场景 | 版本 | 最大停滞秒（3轮） | 阻塞数（3轮） | 重寻路次/步 | 重寻路 ms/步 | 内存变化 MiB（3轮） |',
              '|---|---|---|---|---:|---:|---|']
    for case,data in output['cases'].items():
        for version in ('baseline','mass'):
            rows = data[version]
            stalls = '/'.join(f"{r['maximum_stall_seconds']:.2f}" for r in rows)
            blocked = '/'.join(str(r['blocked']) for r in rows)
            memory = '/'.join(f"{r['memory_delta_bytes']/1048576:+.1f}" for r in rows)
            queries, cost = [statistics.fmean(r['counter_totals'].get(k,0)/r['logical_steps'] for r in rows)
                             for k in ('Navigation.RepathQueries','Navigation.RepathMs')]
            lines.append(f'| {case} | {version} | {stalls} | {blocked} | {queries:.3f} | {cost:.3f} | {memory} |')
    lines += ['', '## 边界', '',
              '- 大群对穿在 30 秒内仍可能行进或拥堵；到达率、活动量、目标进展和停滞一并保留，不把停住当作优化。',
              '- 原有世界帧时间预算和规划提交时序仍会造成同版重复的轨迹差异；逐步哈希与首次分歧步保存在 comparison.json。函数保持不变不等于每条实时轨迹逐点相同。',
              '- 原始采样包含内存变化、重寻路、停滞和碰撞查询记录；进程内存变化含采样缓冲，不能解释为纯 Mass 内存节省。千单位样本关闭了平方复杂度的全对穿透审计，保留原有扫掠观察。没有零穿透门槛。',
              '- 手感和画面由玩家验收。此处不进行截图或视觉检查。', '']
    (root/'comparison.json').write_text(json.dumps(output,ensure_ascii=False,indent=2),encoding='utf-8')
    (root/'性能对比.md').write_text('\n'.join(lines),encoding='utf-8')
    print(json.dumps({k:v['comparison'] for k,v in output['cases'].items()},ensure_ascii=False,indent=2))


if __name__ == '__main__':
    main()
