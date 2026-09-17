"""Summarize the existing six-run acceptance matrix; does not launch tests."""
import json
from collections import defaultdict
from pathlib import Path
from statistics import median

ROOT = Path(__file__).resolve().parent


def read(name):
    return json.loads((ROOT / name).read_text(encoding='utf-8-sig'))


clients = read('comparison.json')
movement = read('movement-results.json')
servers = [r for r in movement if r['role'] == 'server']
assert len(clients) == 9 and len(servers) == 6
assert all(r['scopes'] for r in clients), 'Export all nine client traces first'
groups = defaultdict(list)
for row in clients:
    groups[(row['clients'], row['role'])].append(row)
assert all(len(rows) == 3 for rows in groups.values())

grouped = []
for (count, role), rows in sorted(groups.items()):
    grouped.append({
        'clients': count, 'role': role, 'rounds': len(rows),
        'median_frame_ms': {key: median(r['frame_ms'][key] for r in rows)
                            for key in ('mean', 'p95', 'p99')},
        'moving_p05_min': min(r['moving_p05'] for r in rows),
        'moving_p05_max': max(r['moving_p05'] for r in rows),
        'maximum_frame_ms': max(r['frame_ms']['max'] for r in rows),
        'load_and_frame_pass': all(r['load_pass'] and r['frame_budget_pass'] for r in rows),
    })

scope_names = [
    'GuLiCommanderRoster_ApplyDelta',
    'GuLiCommanderPresentation_StateChanges',
    'GuLiCommanderPresentation_PoolReconcile',
    'GuLiCommanderPresentation_PoolDelta',
    'GuLiCommanderPresentationActor',
    'GuLiCommanderPresentation_Interpolation',
    'GuLiCommanderPresentation_InstanceSubmit',
    'GuLiCommanderPresentation_MassPositionSample',
    'GuLiCommanderPresentation_MassBatchSubmit',
    'GuLiCommanderPresentation_MassBatchExecute',
    'MassDeferredCommand_Execute',
    'GuLiCommanderMiniMap_Snapshot',
    'GuLiCommanderMiniMap_TerrainPaint',
    'GuLiCommanderMiniMap_DynamicPaint',
    'GuLiCommanderHealthBars_UpdateInstances',
    'GuLiCommanderHealthBars_MaintainActivity',
    'DeferredRenderUpdates_GameThread',
    'Recreate',
]
scopes = {}
for name in scope_names:
    counts = [r['scopes'].get(name, {}).get('count', 0) for r in clients]
    scopes[name] = {'count_range_per_30s': [min(counts), max(counts)], 'groups': {}}
    for (count, role), rows in sorted(groups.items()):
        scopes[name]['groups'][f'c{count}-{role}'] = {
            'median_inclusive_ms_per_frame': median(
                r['scopes'].get(name, {}).get('inclusive_ms_per_frame', 0) for r in rows),
            'median_inclusive_ms_per_call': median(
                r['scopes'].get(name, {}).get('mean_ms_per_call', 0) for r in rows),
            'median_count': median(r['scopes'].get(name, {}).get('count', 0) for r in rows),
        }

cache_keys = sorted({key for r in clients for key in r['explicit_cache_high_water_bytes']})
cache = {key: max(r['explicit_cache_high_water_bytes'].get(key, 0) for r in clients)
         for key in cache_keys}
gates = {
    'six_runs_nine_clients_complete': True,
    'all_clients_alive_1200_and_moving_p05_at_least_1140': all(r['load_pass'] for r in clients),
    'all_clients_within_p95_budget': all(r['frame_budget_pass'] for r in clients),
    'all_servers_zero_dropped_steps': all(r['dropped_steps'] == 0 for r in servers),
    'zero_moving_samples_absent': all(r['zero_moving_fraction'] == 0 for r in movement),
    'no_static_roster_full_pool_reconcile': scopes['GuLiCommanderPresentation_PoolReconcile']['count_range_per_30s'][1] == 0,
    'idle_healthbar_no_instance_updates': scopes['GuLiCommanderHealthBars_UpdateInstances']['count_range_per_30s'][1] == 0,
    'mass_position_sampling_at_most_10hz': scopes['GuLiCommanderPresentation_MassPositionSample']['count_range_per_30s'][1] <= 300,
    'minimap_sampling_at_most_10hz': scopes['GuLiCommanderMiniMap_Snapshot']['count_range_per_30s'][1] <= 300,
}
summary = {'groups': grouped, 'gates': gates, 'scopes': scopes,
           'explicit_cache_component_high_water_bytes': cache,
           'explicit_cache_component_high_water_sum_bytes': sum(cache.values())}
(ROOT / 'results-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')

gate_labels = {
    'six_runs_nine_clients_complete': '单客／双客各三轮，九份客户端采样完整',
    'all_clients_alive_1200_and_moving_p05_at_least_1140': '存活1200，实际移动人数P05≥1140',
    'all_clients_within_p95_budget': '单客P95≤16.67ms，双客P95≤33.33ms',
    'all_servers_zero_dropped_steps': '服务端丢步为0',
    'zero_moving_samples_absent': '零移动采样为0',
    'no_static_roster_full_pool_reconcile': '静态名册没有完整实例池校准',
    'idle_healthbar_no_instance_updates': '空闲血条没有逐兵实例更新',
    'mass_position_sampling_at_most_10hz': 'Mass位置采样不超过10Hz',
    'minimap_sampling_at_most_10hz': '小地图采样不超过10Hz',
}
lines = ['# 客户端 CPU 优化修复后复测（2026-09-16）', '',
         '## 本次结论', '',
         '本次独立重新运行既有 1200 单位单客／双客各三轮，共九份客户端采样。', '',
         '**本轮固定镜头CPU性能验收通过。**' if all(gates.values()) else '**本轮仍有验收项未通过，见下表。**', '',
         '| 验收项 | 结果 |', '|---|---|']
for name, passed in gates.items():
    lines.append(f'| {gate_labels[name]} | {"通过" if passed else "未通过"} |')
lines += ['', '## 帧时间与实际移动负载', '',
          '以下为每组各轮结果的中位数；P95/P99 先在各轮共同 30 秒窗口计算，再取三轮中位数。', '',
          '| 场景 | 平均 ms | P95 ms | P99 ms | 移动人数 P05 范围 | P95 门槛 ms |',
          '|---|---:|---:|---:|---:|---:|']
for row in grouped:
    f = row['median_frame_ms']
    lines.append(f"| {row['clients']}客 {row['role']} | {f['mean']:.3f} | {f['p95']:.3f} | {f['p99']:.3f} | {row['moving_p05_min']:.0f}–{row['moving_p05_max']:.0f} | {16.67 if row['clients'] == 1 else 33.33:.2f} |")
lines += ['', '| 轮次 | 客户端 | 平均 ms | P95 ms | P99 ms | 最大 ms | 移动 P05 | 负载／帧预算 |',
          '|---|---|---:|---:|---:|---:|---:|---|']
for row in sorted(clients, key=lambda r: (r['clients'], r['run'], r['role'])):
    f = row['frame_ms']
    lines.append(f"| {row['run']} | {row['role']} | {f['mean']:.3f} | {f['p95']:.3f} | {f['p99']:.3f} | {f['max']:.3f} | {row['moving_p05']:.0f} | {row['load_pass']} / {row['frame_budget_pass']} |")
lines += ['', '## 服务端与先前失败原因', '',
          '先前 `NavigationBuild` 耗时的具体归因由据点／Detour 修复任务定位为 CrowdManager proximity 查询；不能把该顶层计时名称直接解释为 NavMesh 重建。', '',
          '| 轮次 | 模拟步数（完整50秒） | 丢步 | CrowdManager 平均 ms | P95 ms | 峰值 ms |',
          '|---|---:|---:|---:|---:|---:|']
for row in sorted(servers, key=lambda r: (r['run'][-1], r['run'])):
    nav = row['navigation_full_capture']['NavigationBuildDetailed/GameThread/Navigation_CrowdManager']
    lines.append(f"| {row['run']} | {row['simulation_steps']} | {row['dropped_steps']} | {nav['mean']:.3f} | {nav['p95']:.3f} | {nav['max']:.3f} |")
lines += ['', '## CPU 路径与调用次数', '',
          '同一共同窗口的 GameThread trace；已剔除导出器重复带入的 GPU 行。以下耗时是 inclusive ms/帧，父子范围不得相加。零次数表示窗口内无该计时事件。', '',
          '| Scope | 九份窗口次数范围（每30秒） | 单客 | 双客C1 | 双客C2 |',
          '|---|---:|---:|---:|---:|']
for name, row in scopes.items():
    low, high = row['count_range_per_30s']
    values = [row['groups'][key]['median_inclusive_ms_per_frame'] for key in ('c1-client1', 'c2-client1', 'c2-client2')]
    lines.append(f'| `{name}` | {low}–{high} | ' + ' | '.join(f'{v:.4f}' for v in values) + ' |')
lines += ['', '实例池的 `RebuildLocalInstances` 是现有逐帧表现函数名，不等于完整池校准或渲染代理重建；完整池校准单独看 `PoolReconcile`。全场景 `Recreate` 的残余次数不能归为移动士兵，不能宣称场景更新已全部消失。', '',
          '## 缓存与进程资源', '',
          '| 明确计数的容器 | 跨轮高水位 bytes |', '|---|---:|']
for key, value in cache.items():
    lines.append(f'| `{key}` | {value:.0f} |')
lines += ['', f"各容器跨轮高水位相加 {sum(cache.values()) / 1024:.2f} KiB；这不是同一瞬时峰值，也不是净新增内存。不含全部引擎、Slate、资源及 GPU 缓存。", '',
          '进程资源取共同窗口约1Hz采样；每格为三轮平均值的中位数／三轮P95的中位数／三轮最大峰值。CPU单位为占用的逻辑核心等效数。', '',
          '| 场景 | CPU核心（均／P95／峰） | 工作集MiB（均／P95／峰） | PrivateMiB（均／P95／峰） |',
          '|---|---:|---:|---:|']
for (count, role), rows in sorted(groups.items()):
    values = []
    for key in ('CpuCoreEquivalents', 'WorkingSetMiB', 'PrivateMiB'):
        values.append(' / '.join(f'{v:.2f}' for v in (
            median(r['process'][key]['mean'] for r in rows),
            median(r['process'][key]['p95'] for r in rows),
            max(r['process'][key]['max'] for r in rows))))
    lines.append(f'| {count}客 {role} | ' + ' | '.join(values) + ' |')
lines += ['', '## 采集条件与验证边界', '',
          '- 同机源码引擎 `D:/UnrealEngine-5.7`，地图 `/Game/Maps/LVL_CommanderMassPrototype`；Editor运行时 `-game`，非Cook／Shipping。',
          '- 1200单位，join-first分批生成，8秒折返；服务端50秒、客户端40秒，每轮使用各角色同时覆盖的共同QPC 30秒窗口。',
          '- 1920×1080、Epic、屏幕百分比100%、VSync关闭、动态分辨率关闭、原固定镜头；D3D12离屏渲染、客户端不限帧、服务端60FPS上限。',
          '- 延续原正式CPU测试的临时 `MaxAgents=2048` 参数；项目配置与原始压测脚本未改。开启同样的CSV、CPU/GPU trace与网络采集。',
          '- 本次六轮采集期间编辑器关闭，没有并发构建、其他回归或trace导出。旧CPU基线有后台编辑器且实际移动负载不足；本次结果不能用来计算五项CPU改动的独立收益百分比。',
          '- 九份trace在全部采集结束后顺序导出。只读Insights分析器来自Launcher安装；构建、自动化与六轮运行均使用源码引擎。',
          '- 本轮仅复测，未修改生产源码。完整条件见 [conditions.json](conditions.json)，前后源码与模块核对见 [final-validation.json](final-validation.json)。',
          '- 本轮重新运行原有42项自动化；此前鼠标／血条／小地图人工回归证据沿用[原CPU实施报告](../CommanderClientCpu-20260915/REPORT.md)，本轮未重新执行人工交互。旧鼠标脚本仍要求已关闭的预测事件，该判据的适用性限制保留。',
          '- 结论限定为上述固定镜头短窗口场景。双客P95满足33.33ms门槛，但未达到16.67ms；未扩展到全军同屏、全选血条、长时间操作、打包或公网场景。', '',
          '## 构建与回归', '',
          '- `GuLiStrikeEditor Win64 Development` 和 `GuLiStrike Win64 Development`：源码Build.bat退出码均为0，本次均为Up to date。',
          '- 引擎、项目和五个涉及插件的七份模块清单BuildId一致：`11b8900e-cd30-41cb-ad6b-96933e5d3584`。',
          '- 原有网络、表现、血条、小地图回归：42成功，0失败，0未运行。',
          '- 30份相关文件的采集前后SHA-256一致；15份运行日志未检出Fatal、Assertion、Ensure或bunch超限标记。', '',
          '## 收尾', '',
          '- 编辑器已恢复至原 `/Game/Maps/LVL_CommanderMassPrototype` 地图，没有运行PIE，脏关卡／资产包为空，见 [恢复状态](editor-after.json)。',
          '- CPU开发记录已更新为done/passed；更广的客户端性能需求保留partial。新增复测归档，不覆盖原未通过记录。',
          '- Progress索引构建成功，全量检查0错误、12条既有文档尺寸／任务数提示，见 [构建日志](progress-build.log) 与 [检查日志](progress-check.log)。关联需求已超过20KB，后续可按CPU、同步、显示和资源归因边界拆分，本次未拆分。', '',
          '## 证据', '',
          '- [每轮共同窗口负载与服务端结果](movement-results.json)',
          '- [九份客户端CPU、内存与trace统计](comparison.json)',
          '- [汇总与机制门槛](results-summary.json) · [整帧CSV](frames-comparison.csv)',
          '- [Editor构建](editor-build.log) · [Game构建](game-build.log) · [自动化报告](Automation/index.json)',
          '- [模块清单](buildids.json) · [源码清单](source-manifest.json) · [运行日志标记检查](runtime-error-markers.json)',
          '- [既有场景采集脚本副本](run_capture.ps1) · [负载分析](analyze_movement.py) · [CPU分析](analyze_cpu.py) · [汇总脚本](summarize_results.py)',
          '- [Detour与坡道修复记录](../../Progress/Archive/20260916-据点矿厂落点与Detour避让卡顿修复.md)',
          '- [CPU开发记录](../../Progress/DevelopmentDocumentation/20260915-客户端CPU增量维护与10Hz刷新.md)',
          '']
(ROOT / 'REPORT.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
print(json.dumps({'groups': grouped, 'gates': gates}, ensure_ascii=False, indent=2))
