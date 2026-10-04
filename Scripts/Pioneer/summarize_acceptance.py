"""Aggregate saved Pioneer acceptance evidence; does not start UE or run gameplay."""
import hashlib
import json
import math
import statistics
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ART = ROOT / 'ArtSource/Mechs/RSGMechStyle_20261003'
DELIVERY = ART / 'Delivery_UE_v1'
REPORTS = DELIVERY / 'Reports'


def read(name):
    value = json.loads((REPORTS / name).read_text(encoding='utf-8-sig'))
    return value.get('result', value) if isinstance(value, dict) else value


def unit(snapshot, identity):
    return next(row for row in snapshot['units'] if row['id'] == identity)


def sweepers(snapshot):
    return {row['id'] for row in snapshot['units'] if row['type'] == 5 and row['alive']}


def per_unit_reply(snapshot, identity):
    return next(row for row in snapshot['last_reply']['units'] if row['id'] == identity)


def speeds(names, identity):
    result = []
    for name in names:
        for snapshot in read(name):
            row = unit(snapshot, identity)
            speed = math.hypot(*row['velocity'][:2])
            if row['moving'] and speed > 1:
                result.append(speed)
    return result


def main():
    cases = []

    def record(name, passed, detail, evidence):
        cases.append({'name': name, 'status': 'passed' if passed else 'failed',
                      'detail': detail, 'evidence': evidence})

    before = read('qa_v2_before_q.json')
    single = read('qa_v2_single_readback.json')
    cooldown = read('qa_v2_cooldown_readback.json')
    mixed = read('qa_v2_multi_readback.json')
    independent = read('qa_v2_independent_readback.json')
    accumulation_before = read('qa_v2_before_accumulation.json')
    accumulation = read('qa_v2_accumulation_readback.json')
    overview = read('qa_v2_overview_readback.json')
    after_death = read('qa_v2_after_death.json')
    final_v2 = read('qa_v2_final.json')
    blocked = read('qa_v4_blocked_readback.json')
    unblocked = read('qa_v4_unblocked_readback.json')
    multi_before = read('qa_v4_before_multi.json')
    multi = read('qa_v4_multi_readback.json')
    death = read('qa_v4_death_isolation.json')
    duplicate_before = read('qa_v5_before_exact_duplicate.json')
    duplicate = read('qa_v5_exact_duplicate.json')

    pioneer = unit(single, 1)
    width = pioneer['bounds_max'][1] - pioneer['bounds_min'][1]
    record('身份、体型、属性', pioneer['type'] == 1 and pioneer['name'] == '先驱号'
           and width == 625 and pioneer['radius'] == 312.5
           and pioneer['max_health'] == 100,
           {'width_cm': width, 'avoidance_radius_cm': pioneer['radius'], 'health': pioneer['max_health'],
            'defense': read('ue_runtime_readback.json')['pioneer']['Defense']},
           ['qa_v2_single_readback.json', 'ue_runtime_readback.json'])
    record('单台真实 Q 与初生待机', len(sweepers(single) - sweepers(before)) == 5
           and all(not unit(single, identity)['moving'] for identity in sweepers(single))
           and single['population'] == before['population'] and single['reserved'] == 0,
           {'new_sweepers': sorted(sweepers(single) - sweepers(before)), 'population': single['population'],
            'reserved': single['reserved']}, ['qa_v2_before_q.json', 'qa_v2_single_readback.json'])
    record('冷却拒绝与超过 30 秒后累积', 'COOLDOWN' in per_unit_reply(cooldown, 1)['code']
           and sweepers(cooldown) == sweepers(single)
           and len(sweepers(accumulation) - sweepers(accumulation_before)) == 5
           and accumulation['world_time'] - single['world_time'] > 30,
           {'first_cast_ready_at': per_unit_reply(single, 1)['ready_at'],
            'first_cast_snapshot_time': single['world_time'],
            'recast_snapshot_time': accumulation['world_time'], 'recast_added': 5},
           ['qa_v2_cooldown_readback.json', 'qa_v2_before_accumulation.json', 'qa_v2_accumulation_readback.json'])
    record('混选两台各五个与独立冷却', len(sweepers(mixed)) == 15
           and all(per_unit_reply(mixed, identity)['success'] for identity in [2, 3])
           and 'NO_SKILL' in per_unit_reply(mixed, 8)['code']
           and per_unit_reply(independent, 2)['success']
           and abs(unit(independent, 3)['ready_at'] - independent['world_time']) < .001,
           {'mixed_total': 15, 'mixed_ineligible_unit': 8,
            'independent_caster': 2, 'other_caster_ready': True},
           ['qa_v2_multi_readback.json', 'qa_v2_independent_readback.json'])
    record('相同内容请求重发返回缓存且无重复召唤', sweepers(duplicate) == sweepers(duplicate_before)
           and 'SUCCEEDED' in duplicate['last_reply']['code']
           and duplicate['last_reply']['units'] == duplicate_before['last_reply']['units']
           and unit(duplicate, 1)['ready_at'] == unit(duplicate_before, 1)['ready_at'],
           {'summoned_before': len(sweepers(duplicate_before)), 'summoned_after': len(sweepers(duplicate)),
            'reply_units_unchanged': duplicate['last_reply']['units'] == duplicate_before['last_reply']['units']},
           ['qa_v5_before_exact_duplicate.json', 'qa_v5_exact_duplicate.json'])
    record('保存墙体阻挡整批失败且不扣冷却', len(sweepers(blocked)) == 0
           and 'EXECUTION_FAILED' in per_unit_reply(blocked, 5)['code']
           and unit(blocked, 5)['ready_at'] == blocked['world_time']
           and len(sweepers(unblocked)) == 5 and per_unit_reply(unblocked, 5)['success'],
           {'blocked_count': 0, 'blocked_cooldown_seconds': 0, 'unblocked_count': 5,
            'error': per_unit_reply(blocked, 5)['error']},
           ['qa_v4_blocked_readback.json', 'qa_v4_unblocked_readback.json'])
    record('最终碰撞修复后的双台 Q 回归', len(sweepers(multi) - sweepers(multi_before)) == 10
           and all(not unit(multi, identity)['moving'] for identity in sweepers(multi))
           and multi['population'] == multi_before['population'] and multi['reserved'] == 0,
           {'added': 10, 'total_sweepers': 15, 'initial_idle': True, 'population': multi['population']},
           ['qa_v4_before_multi.json', 'qa_v4_multi_readback.json'])
    non_summon_population = sum(row['alive'] and row['team'] == 1 and not row['summon_only']
                                for row in overview['units'])
    record('人口排除、永久累积、施法者死亡后继续控制', len(sweepers(overview)) == 35
           and overview['population'] == non_summon_population and overview['reserved'] == 0
           and not unit(after_death, 1)['alive'] and sweepers(after_death) == sweepers(overview)
           and unit(final_v2, 9)['alive'] and unit(final_v2, 9)['moving']
           and final_v2['selected'] == [9],
           {'summons': 35, 'production_population': overview['population'],
            'alive_non_summon_population': non_summon_population, 'controlled_after_parent_death': 9},
           ['qa_v2_overview_readback.json', 'qa_v2_after_death.json', 'qa_v2_final.json'])
    speed_p = speeds(['qa_v2_pioneer_speed_projected_samples.json', 'qa_v2_wait_cooldown_samples.json'], 2)
    speed_w = speeds(['qa_v2_move_speed_samples.json'], 4)
    speed_s = speeds(['qa_v2_sweeper_control_samples.json'], 8)
    speed_metrics = {'pioneer_max_cm_s': max(speed_p), 'pioneer_median_cm_s': statistics.median(speed_p),
                     'pioneer_moving_samples': len(speed_p), 'heavy_max_cm_s': max(speed_w),
                     'sweeper_max_cm_s': max(speed_s)}
    record('实际移动速度 1440 / 720 / 720', abs(max(speed_p) - 1440) < .01
           and abs(max(speed_w) - 720) < .01 and abs(max(speed_s) - 720) < .01, speed_metrics,
           ['qa_v2_pioneer_speed_projected_samples.json', 'qa_v2_wait_cooldown_samples.json',
            'qa_v2_move_speed_samples.json', 'qa_v2_sweeper_control_samples.json'])
    firing = read('qa_v2_shooting_early.json')
    shots = [shot for shot in firing['shots'] if shot['source'] == 1]
    launches = {event['effect_id']: event for event in firing['projectiles'] if event['phase'] == 1}
    rounds = {}
    angles, launch_errors = [], []
    for shot in shots:
        rounds.setdefault(shot['time'], []).append(shot)
        projectile = launches[shot['shot_id']]
        a, b = shot['direction'], projectile['direction']
        dot = sum(x * y for x, y in zip(a, b)) / (math.sqrt(sum(x*x for x in a)) * math.sqrt(sum(x*x for x in b)))
        angles.append(math.degrees(math.acos(max(-1., min(1., dot)))))
        launch_errors.append(math.dist(shot['start'], projectile['launch']))
    times = sorted(rounds)
    intervals = [b - a for a, b in zip(times, times[1:])]
    weapon_metrics = {'sample_rounds': len(rounds), 'unique_shot_ids': len({shot['shot_id'] for shot in shots}),
                      'max_spread_deviation_degrees': max(angles), 'round_intervals_seconds': intervals,
                      'muzzle_projectile_start_max_error_cm': max(launch_errors),
                      'target_health_before': unit(before, 6)['health'], 'target_health_after': unit(firing, 6)['health']}
    record('双枪同步、独立弹丸、伤害、1 Hz、2° 总散布与枪口起点', len(shots) == 10
           and len({shot['shot_id'] for shot in shots}) == 10
           and all({shot['muzzle'] for shot in pair} == {0, 1} and len(pair) == 2 for pair in rounds.values())
           and max(angles) <= 1 and max(launch_errors) < .001
           and all(.95 < interval < 1.05 for interval in intervals)
           and pioneer['damage'] == 10 and unit(before, 6)['health'] - unit(firing, 6)['health'] == 60,
           weapon_metrics, ['qa_v2_before_q.json', 'qa_v2_shooting_early.json'])
    waiting = read('qa_v2_wait_readback.json')
    target_distance = math.dist(unit(before, 1)['position'][:2], unit(before, 7)['position'][:2])
    record('60 米射程与扫荡者继承 20 米单枪', pioneer['range'] == 6000
           and target_distance > 6000 and unit(waiting, 7)['health'] == unit(before, 7)['health']
           and not any(shot['source'] == 1 and shot['target'] == 7 for shot in waiting['shots'])
           and any(shot['source'] == 8 and shot['target'] == 7 for shot in overview['shots'])
           and unit(overview, 8)['range'] == 2000 and unit(overview, 8)['projectile_count'] == 1,
           {'in_range_target_cm': 5600, 'out_of_range_target_cm': target_distance,
            'out_target_health_before_sweeper_approach': unit(waiting, 7)['health'],
            'sweeper_range_cm': 2000, 'sweeper_projectile_count': 1},
           ['qa_v2_before_q.json', 'qa_v2_wait_readback.json', 'qa_v2_overview_readback.json'])
    observed_frames = []
    sample_names = ['qa_v2_single_readback.json', 'qa_v2_move_speed_samples.json',
                    'qa_v2_pioneer_speed_projected_samples.json', 'qa_v2_wait_cooldown_samples.json',
                    'qa_v2_sweeper_control_samples.json', 'qa_v2_final.json', 'qa_v4_death_isolation.json']
    skeletal_max = 0
    for name in sample_names:
        payload = read(name)
        for snapshot in payload if isinstance(payload, list) else [payload]:
            skeletal_max = max(skeletal_max, snapshot.get('skeletal_components', 0))
            for component in snapshot['presentation']:
                if 'Pioneer' not in (component['mesh'] or ''):
                    continue
                assert component['custom_slots'] == 59
                observed_frames.extend(instance['vat'][0] for instance in component['instance_data']
                                       if min(instance['scale']) > .5 and instance['vat'])
    clip_ranges = {'Idle': [0, 50], 'Forward': [50, 81], 'Backward': [81, 112],
                   'Left': [112, 143], 'Right': [143, 174], 'Death': [174, 235]}
    coverage = {name: sum(start <= frame < end for frame in observed_frames)
                for name, (start, end) in clip_ranges.items()}
    wreck_frames = [instance['vat'][0] for component in death['presentation']
                    if component['name'].startswith('WreckUnits') and 'Pioneer' in (component['mesh'] or '')
                    for instance in component['instance_data'] if min(instance['scale']) > .5]
    record('Mass 静态 ISM VAT、待机/四向/死亡', skeletal_max == 0 and all(coverage.values())
           and any(174 <= frame < 235 for frame in wreck_frames),
           {'skeletal_components': skeletal_max, 'custom_slots': 59,
            'observed_frame_samples_by_clip': coverage, 'wreck_vat_frames': wreck_frames}, sample_names)
    editor = read('qa_editor_final_state.json')
    scene = read('qa_final_saved_scene.json')
    record('正式资源、最新编译、场景保存与启动烘焙', read('ue_runtime_readback.json')['success']
           and editor['PIE_stopped'] and editor['bake_valid'] and editor['unsaved_packages'] == 0
           and len(scene['deployments']) == 7 and len(scene['blockers']) == 4
           and 'Result: Succeeded' in (REPORTS / 'qa_exact_dedup_build.log').read_text(encoding='utf-8-sig'),
           {'editor': editor, 'initial_count': 7, 'saved_blockers': 4, 'build_exit_code': 0},
           ['ue_runtime_readback.json', 'qa_exact_dedup_build.log', 'qa_editor_final_state.json',
            'qa_final_saved_scene.json', 'qa_bake_result.json'])
    baseline_hash = hashlib.file_digest((ART / 'Production_B_v1/RSGMech_Production_B_v1.blend').open('rb'), 'sha256').hexdigest()
    assert baseline_hash == '3b330e2d2e84bb464b904aa2bb77e2043e79489c226c8d39815c8fe11f19a887'
    report = {'date': '2026-10-03', 'authority': '用户明确要求“这次你帮我验收”',
              'scope': '本地权威 PIE 核心功能验收；不代替万人容量、多客户端或性能验收',
              'status': 'passed' if all(case['status'] == 'passed' for case in cases) else 'failed',
              'art_baseline_sha256': baseline_hash, 'cases': cases,
              'not_run': ['10000 实体容量满载边界', '提交中途故障注入与回滚运行测试',
                          '独立服务端与多客户端协议 23 联机', '帧率/GPU 性能基准与打包'],
              'historical_failure_evidence': ['qa_launch_failure.log', 'qa_first_run_failure_evidence.json',
                                              'qa_v3_collision_failure_evidence.json', 'qa_v2_duplicate_q.json'],
              'duplicate_test_correction': '旧重发工具遗漏原请求的地面命中上下文，实际测试了同 ID 不同内容拒绝；最新工具复制只读服务器回执完整重发。',
              'build_ids': read('qa_final_build_ids.json')}
    (REPORTS / 'qa_acceptance_summary.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    (REPORTS / 'qa_acceptance_authorization.json').write_text(json.dumps({
        'date': '2026-10-03', 'user_request': '这次你帮我验收', 'pie_authorized': True,
        'editor_compile_reload_authorized': True, 'art_baseline': 'B-v1',
        'art_approval_replaced': False, 'performance_benchmark_run': False,
        'historical_approval_file_preserved': '../approval_B_v1_20261003.json'
    }, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'status': report['status'], 'passed': sum(case['status'] == 'passed' for case in cases),
                      'total': len(cases), 'speed': speed_metrics, 'weapon': weapon_metrics,
                      'failed': [case['name'] for case in cases if case['status'] != 'passed']}, ensure_ascii=False, indent=2))
    return 0 if report['status'] == 'passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
