"""Summarize a qualified before/after capture and compare fixed-step trajectories.

Uses existing movement captures; does not launch PIE or impose new gameplay gates.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics

NAMES = dict(crossing='两队对穿', hold='静止队伍挡路', gather='分层集结',
             mixed='大小混合集结', attack='围攻', buildings='建筑绕行',
             ore='矿石变化', work='采矿、回厂、施工')
COUNTERS = ('Movement.CircleBucketVisits', 'Movement.CircleChecks', 'Movement.SideProbes',
            'Movement.YieldRequests', 'Movement.YieldRelayAttempts', 'Movement.YieldSteps')


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def trajectory(row):
    movement = read(Path(row['directory']) / 'capture.json')['movement']
    frames = [{m['id']: (m['position'], tuple(m.get(k, 0) for k in
               ('state', 'order', 'completed_order', 'failed_order')))
               for m in o['members']} for o in movement['observations']]
    canonical = [[(i, [round(v, 6) for v in p], s) for i, (p, s) in sorted(f.items())] for f in frames]
    queries = [tuple(a.get('step_counters', {}).get(k, 0) for k in COUNTERS)
               for a in movement['applications'] if not a['immediate']]
    return {'frames': frames, 'queries': queries,
            'hash': hashlib.sha256(json.dumps(canonical, separators=(',', ':')).encode()).hexdigest()}


def difference(a, b):
    positions = states = members = 0
    maximum = 0.
    for x, y in zip(a['frames'], b['frames']):
        members += len(x.keys() ^ y.keys())
        for identity in x.keys() & y.keys():
            p, state = x[identity]
            q, other = y[identity]
            distance = math.dist(p, q)
            positions += distance > .0001
            maximum = max(maximum, distance)
            states += state != other
    return {'position_steps_different': positions, 'state_steps_different': states,
            'missing_members': members, 'maximum_position_delta_cm': maximum,
            'step_count_equal': len(a['frames']) == len(b['frames']),
            'query_counter_sequence_equal': a['queries'] == b['queries'],
            'trajectory_hash_equal': a['hash'] == b['hash']}


def mean_metric(rows, key, field='mean'):
    return statistics.fmean(r['metrics_per_step'].get(key, {}).get(field, 0) for r in rows)


def percentile_range(rows, key):
    values = [r['movement_total_per_step_ms'][key] for r in rows]
    return f'{min(values):.3f}–{max(values):.3f}'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--comparison', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    data = read(args.comparison)
    args.output.mkdir(parents=True, exist_ok=True)
    trajectories = {}
    lines = ['# Mass移动数据去重与批量读写：同场景性能比较', '',
             '源码版UE5.7，GuLiStrikeEditor Win64 Development；地图 `/Game/Maps/LVL_CommanderMassPrototype`。'
             '两版各8场景×3次×30秒，使用同一冻结布局、完整632单位开局、命令时间线和画质。'
             '准备与预热不计时；基线为本轮实际采集，旧版采完后才编译加载新版本。', '',
             '本次删除无消费者的移动目标、受力、落位副本；复用工作缓冲，直接构建动态圆索引，'
             '按Mass Chunk批量发布位置、速度和指令结果。圆形侧绕与让路函数保持一致，'
             '仍按SoldierId顺序求解并立即更新动态圆。未加入锁侧或新的恢复规则。', '',
             '## 主线程逻辑步总耗时', '',
             '单位为ms/10Hz逻辑步，三轮均值；重复极差取两版中较大者。'
             '沿用现有TickAuthority应用耗时口径，包含同一步内的其他权威处理，不能当作Mass写回独占成本。'
             '分项包含关系不能相加。工作线程移动成本仍为0，Hybrid网络线程另计。', '',
             '| 场景 | 优化前 | 优化后 | 变化 | 最大重复极差 | 到达数 前→后 | 耗时判断 |',
             '|---|---:|---:|---:|---:|---|---|']
    for scenario, name in NAMES.items():
        before, after = data['old'][scenario], data['new'][scenario]
        c = data['comparison'][scenario]
        arrivals = lambda rows: '/'.join(str(r['arrived']) for r in rows)
        judgment = ('下降超出波动' if c['saving_ms'] > c['repeat_range_ms'] else
                    '上升超出波动' if -c['saving_ms'] > c['repeat_range_ms'] else '未超出波动')
        lines.append(f"| {name} | {c['old_ms']:.3f} | {c['new_ms']:.3f} | "
                     f"{(c['new_ms']/c['old_ms']-1)*100:+.1f}% | {c['repeat_range_ms']:.3f} | "
                     f"{arrivals(before)} → {arrivals(after)} | {judgment} |")
        a, b = [trajectory(r) for r in before], [trajectory(r) for r in after]
        pairs = [[difference(x, y) for x in a] for y in b]
        trajectories[scenario] = {'before_hashes': [x['hash'] for x in a],
            'after_hashes': [x['hash'] for x in b], 'after_by_before': pairs,
            'before_internal': [difference(a[0], x) for x in a[1:]],
            'after_internal': [difference(b[0], x) for x in b[1:]]}
    lines += ['', '## 尾部耗时与活动单位', '',
              '| 场景 | 版本 | P95范围 ms | P99范围 ms | ms/活动单位步 |', '|---|---|---:|---:|---:|']
    for scenario, name in NAMES.items():
        for key, version in [('old', '前'), ('new', '后')]:
            rows = data[key][scenario]
            costs = [r['movement_ms_per_active_unit_step'] for r in rows if r['movement_ms_per_active_unit_step'] is not None]
            cost = f'{statistics.fmean(costs):.5f}' if costs else '—'
            lines.append(f'| {name} | {version} | {percentile_range(rows,"p95")} | {percentile_range(rows,"p99")} | {cost} |')
    lines += ['', '活动单位成本用整步总耗时除以实际移动单位步数，包含完整开局的固定开销。', '',
              '## 到达与停滞', '',
              '单位秒，区间为三轮范围；未到达者不进入到达时间均值。停滞按现有采样器口径统计。', '',
              '| 场景 | 版本 | 平均到达时间范围 | 到达P95范围 | 最大停滞范围 |',
              '|---|---|---:|---:|---:|']
    for scenario, name in NAMES.items():
        for key, version in [('old', '前'), ('new', '后')]:
            rows = data[key][scenario]
            ranges = []
            for field in ('mean', 'p95'):
                values = [r['arrival_seconds'][field] for r in rows if r['arrival_seconds'][field] is not None]
                ranges.append(f'{min(values):.2f}–{max(values):.2f}' if values else '—')
            stalls = [r['maximum_stall_seconds'] for r in rows]
            lines.append(f'| {name} | {version} | {ranges[0]} | {ranges[1]} | {min(stalls):.2f}–{max(stalls):.2f} |')
    lines += ['', '## 分项成本', '',
              '单位ms/逻辑步，三轮均值。提交包含求解、导航和Mass结果发布。', '',
              '| 场景 | 版本 | 索引 | 求解 | 提交 | NavMesh移动 | 重寻路 | 矿簇更新 |',
              '|---|---|---:|---:|---:|---:|---:|---:|']
    keys = ('Movement.GT.CircleIndexMs', 'Movement.GT.CircleSolveMs', 'Movement.GT.CommitInclusiveMs',
            'Movement.GT.NavMeshMoveMs', 'Navigation.RepathMs', 'Movement.GT.OreClusterMs')
    for scenario, name in NAMES.items():
        for key, version in [('old', '前'), ('new', '后')]:
            values = ' | '.join(f'{mean_metric(data[key][scenario], k):.4f}' for k in keys)
            lines.append(f'| {name} | {version} | {values} |')
    lines += ['', '## 逐步轨迹与计算量', '',
              '位置与指令状态按逻辑步比较；位置指纹取厘米坐标小数点后6位。'
              '每个新版窗口与三个基线分别比较，同时保留基线内部差异；不把时间预算调度产生的既有波动直接归因于算法变化。', '',
              '| 场景 | 新版与任一基线轨迹完全一致的轮数 | 基线不同轨迹数 | 新版不同轨迹数 | 圆检查/步 前→后 | 侧向试探/步 前→后 |',
              '|---|---:|---:|---:|---:|---:|']
    for scenario, name in NAMES.items():
        t = trajectories[scenario]
        matches = sum(any(r['trajectory_hash_equal'] for r in row) for row in t['after_by_before'])
        before, after = data['old'][scenario], data['new'][scenario]
        checks = [mean_metric(r, 'Movement.CircleChecks') for r in (before, after)]
        probes = [mean_metric(r, 'Movement.SideProbes') for r in (before, after)]
        lines.append(f'| {name} | {matches}/3 | {len(set(t["before_hashes"]))} | {len(set(t["after_hashes"]))} | '
                     f'{checks[0]:.1f} → {checks[1]:.1f} | {probes[0]:.1f} → {probes[1]:.1f} |')
    lines += ['', '| 场景 | 版本 | 桶访问/步 | 让路请求/步 | 传递尝试/步 | 让路移动/步 | 重寻路/步 | 连续反向最大值 | 内存变化MiB三轮 |',
              '|---|---|---:|---:|---:|---:|---:|---:|---|']
    for scenario, name in NAMES.items():
        for key, version in [('old', '前'), ('new', '后')]:
            rows = data[key][scenario]
            values = ' | '.join(f'{mean_metric(rows, k):.2f}' for k in (COUNTERS[0], *COUNTERS[3:], 'Navigation.RepathQueries'))
            reverse = max(r['movement_behavior']['maximum_consecutive_reversals'] for r in rows)
            memory = '/'.join(f'{r["memory_delta_bytes"]/1048576:.1f}' for r in rows)
            lines.append(f'| {name} | {version} | {values} | {reverse} | {memory} |')
    lines += ['', '## 数据与边界', '',
              '- [完整逐轮统计及分项耗时](comparison.json)，含索引、求解、提交、NavMesh、重寻路、矿簇更新、查询、让路、内存、到达和停滞。',
              '- [逐步轨迹比较](trajectories.json)，包括九组前后配对、各版本内部差异和查询计数序列。',
              '- 进程内存包含分配器、GC与其他系统波动，不能作为避障独占内存收益。',
              '- 完整30秒窗口包含300或301个逻辑步；帧起点、异步导航与按时间预算调度会影响回放，完整轨迹指纹也计入窗口长度。混合集结、矿石变化、工作场景的三次基线自身均不完全相同。',
              '- 围攻窗口两版都在原地射击，没有活动移动单位；它只覆盖该负载下的逻辑步成本，不能验证运动中的包围表现。',
              '- 穿透记录仅供观察，不作为门槛；本轮不修复既有两点往返。',
              '- 结构化轨迹核对不代替玩家手感与视觉验收。', '']
    (args.output/'trajectories.json').write_text(json.dumps(trajectories, ensure_ascii=False, indent=2), encoding='utf-8')
    (args.output/'性能对比.md').write_text('\n'.join(lines), encoding='utf-8')
    print(json.dumps({s: sum(any(r['trajectory_hash_equal'] for r in row) for row in t['after_by_before'])
                      for s, t in trajectories.items()}, ensure_ascii=False))


if __name__ == '__main__':
    main()
