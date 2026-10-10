"""Account complete GT frames with exclusive scopes; never sum nested inclusive timers."""
import collections
import csv
import gzip
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent


def category(name):
    if name in ['Slate::Tick (Time and Widgets)', 'Slate::Tick (Platform and Input)',
                'ProcessLocalPlayerSlateOperations', 'Tick_SlateInput'] or name.startswith(('GuLiSceneUI_', 'GuLiCommanderHealthBars_')):
        return 'UI与Slate（游戏和编辑器）'
    if 'Avoidance' in name or name.startswith('GuLiCommander_SharedRouteWork'):
        return '避障与共享路径处理'
    if name == 'GuLiCombatEffects_Runtime' or name.startswith('GuLiProjectilePool'):
        return '服务器弹丸模拟、碰撞和结算'
    if name in ['CombatEffectReplication', 'GameNetDriver', 'CommanderNetSync', 'SoldierWorldReplication'] or name.startswith(('UNetConnection_', 'UNetDriver_', 'UChannel_', 'ClientReceive', 'Multicast', 'ServerMove', 'ServerSubmitPose', 'GuLiPose_')):
        return '事件复制、姿态与网络收发'
    if 'StateTree' in name or name.startswith('GuLiUnitTask'):
        return 'StateTree'
    if name in ['Projectile Movement', 'UCharacterMovementComponent_TickComponent', 'BP_CombatAvatarFly01_C',
                'WingmanRelay', 'WingmanCollision']:
        return '移动组件、飞行Pawn及僚机相关Tick'
    if name == 'GuLiCombatEffects_Presentation' or name.startswith(('GuLiClientFlight', 'GuLiMissileCluster', 'GuLiWingmanPresentation', 'GuLiLaser')):
        return '客户端飞行与战斗特效表现'
    if 'Niagara' in name or name.startswith('NS_'):
        return 'Niagara和特效组件CPU工作'
    if name.startswith(('GuLiCommanderPresentation', 'USkinnedMeshComponent_', 'USkeletalMeshComponent_', 'UAnim')) or name == 'DeferredRenderUpdates_GameThread':
        return '单位呈现、动画与渲染状态更新'
    return None


def aggregate(directory):
    source = json.loads((directory / 'input.json').read_text(encoding='utf-8'))
    begin, end = source['trace_window']
    stack, frames, frame = [], [], None
    exclusive = collections.defaultdict(float)
    inclusive = collections.defaultdict(float)
    calls = collections.Counter()
    phases = collections.defaultdict(float)
    thread_ids = set()
    minimum_exclusive = 0
    groups = collections.defaultdict(float)
    group_timers = collections.defaultdict(lambda: collections.defaultdict(float))

    def finish():
        nonlocal minimum_exclusive
        event = stack.pop()
        own = event['duration'] - event['children']
        minimum_exclusive = min(minimum_exclusive, own)
        exclusive[event['name']] += own
        inclusive[event['name']] += event['duration']
        calls[event['name']] += 1
        frame['exclusive_sum'] += own
        group = event['category']
        if group is None:
            if event['name'] == 'UWorld_Tick':
                group = 'UWorld_Tick内未细分的自身耗时'
            elif any(s in event['name'] for s in ['WaitFor', 'WaitingQueue', 'ProcessUntilTasksComplete', 'WaitUntil', 'BusyWait']):
                group = '任务调度和等待Scope自身耗时'
            else:
                group = '其余引擎、编辑器与玩法Scope'
        groups[group] += own
        group_timers[group][event['name']] += own
        if event['depth'] == 2:
            phases[event['name']] += event['duration']
        if stack:
            stack[-1]['children'] += event['duration']

    events_path = directory / 'events.csv'
    stream = events_path.open(encoding='utf-8-sig', newline='') if events_path.exists() else gzip.open(
        directory / 'events.csv.gz', 'rt', encoding='utf-8-sig', newline='')
    with stream:
        for row in csv.DictReader(stream):
            name = row['TimerName']
            depth = int(row['Depth'])
            if name == 'FEngineLoop::Tick' and depth == 0:
                while stack:
                    finish()
                if frame is not None:
                    frames.append(frame)
                start, stop = float(row['StartTime']), float(row['EndTime'])
                frame = {'start': start, 'end': stop, 'duration': float(row['Duration']), 'exclusive_sum': 0} if start >= begin and stop <= end else None
            if frame is None:
                continue
            start, stop = float(row['StartTime']), float(row['EndTime'])
            if start < frame['start'] - 1e-6 or stop > frame['end'] + 1e-6:
                continue
            while stack and depth <= stack[-1]['depth']:
                finish()
            thread_ids.add(row['ThreadId'])
            inherited = stack[-1]['category'] if stack else None
            stack.append({'name': name, 'depth': depth, 'duration': float(row['Duration']), 'children': 0,
                          'category': inherited or category(name)})
    while stack:
        finish()
    if frame is not None:
        frames.append(frame)
    assert frames and len(thread_ids) == 1
    closure = max(abs(f['duration'] - f['exclusive_sum']) for f in frames)
    assert closure < 1e-7, closure
    assert minimum_exclusive > -1e-6, minimum_exclusive
    n = len(frames)
    total = sum(f['duration'] for f in frames)
    ticks = [{'timer': name, 'exclusive_ms_per_frame': value * 1000 / n,
              'inclusive_ms_per_frame': inclusive[name] * 1000 / n, 'calls_per_frame': calls[name] / n}
             for name, value in sorted(exclusive.items(), key=lambda r: r[1], reverse=True)]
    result = {'source': source, 'frames': n, 'engine_tick_mean_ms': total * 1000 / n,
              'exclusive_sum_mean_ms': sum(exclusive.values()) * 1000 / n,
              'closure_error_max_ms': closure * 1000, 'minimum_exclusive_ms': minimum_exclusive * 1000,
              'between_tick_mean_ms': sum(max(0, b['start'] - a['end']) for a, b in zip(frames, frames[1:])) * 1000 / (n - 1),
              'phases_depth2_mean_ms': dict(sorted(((k, v * 1000 / n) for k, v in phases.items()), key=lambda x: x[1], reverse=True)),
              'timers': ticks,
              'disjoint_groups_ms': dict(sorted(((k, v * 1000 / n) for k, v in groups.items()), key=lambda x: x[1], reverse=True)),
              'group_timer_self_ms': {k: dict(sorted(((t, v * 1000 / n) for t, v in values.items()), key=lambda x: x[1], reverse=True)) for k, values in group_timers.items()},
              'method': 'Only complete FEngineLoop::Tick intervals inside host capture. Exclusive = duration minus immediate children; per-frame sums checked against root. Wall time, not on-CPU execution time.'}
    (directory / 'accounting.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'run': directory.name, 'frames': n, 'engine_ms': result['engine_tick_mean_ms'],
                      'gap_ms': result['between_tick_mean_ms'], 'phases': list(result['phases_depth2_mean_ms'].items())[:12],
                      'groups': result['disjoint_groups_ms'],
                      'remaining_top': list(result['group_timer_self_ms'].get('其余引擎、编辑器与玩法Scope', {}).items())[:25]}, ensure_ascii=False), flush=True)


if __name__ == '__main__':
    for path in sorted(ROOT.glob('*/input.json')):
        aggregate(path.parent)
