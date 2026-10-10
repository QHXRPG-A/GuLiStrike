"""Production-bandwidth native network capture after snapshot candidate rollback.

The four experimental candidates were removed. This entry keeps baseline/manual
capture only; historical comparison code is preserved under Rollback/Before.

Uses the existing real movement/combat fixtures. No Python enumeration, screenshots,
asset writes or additional network RPCs occur in the measured window.
"""
from __future__ import annotations

import argparse
from datetime import datetime
import hashlib
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts'))
from Performance import run_muzzle_batch_review as fixture
from Performance import run_four_stage_review as review
from Performance.capture_live_pie import SNAPSHOT

OUT = ROOT / 'Artifacts/PerformanceOptimization20261010/Rollback'
VERIFY = ['gs.Avoidance.VerifyPrefix']
QUALITY = ['sg.' + s + 'Quality' for s in ['ViewDistance', 'AntiAliasing', 'Shadow',
           'GlobalIllumination', 'Reflection', 'PostProcess', 'Texture', 'Effects', 'Foliage', 'Shading']]
KEYS = list(dict.fromkeys(fixture.KEYS + VERIFY + QUALITY +
                         ['r.ScreenPercentage', 'r.DynamicRes.OperationMode']))
PROFILES = """
ws=list(unreal.EditorLevelLibrary.get_pie_worlds(True))
for s in unreal.ObjectIterator(unreal.GuLiPerformanceSubsystem):
 if s.get_outer() in ws:s.ACTION_capture()
"""


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def configure_view(setup, view, yaw_offset=0):
    if view == 'full':
        # Moving columns spread ~80,000 cm across this 30 s fixture. Frame the
        # entire region from above; do not widen FOV and assume it covers them.
        setup['camera'] = [setup['target'][0], setup['target'][1]-1000, setup['target'][2]+70000]
    offsets = [0, 0] if view in ['onscreen','full'] else [180, 180] if view == 'offscreen' else \
              [180, 0] if view == 'asymmetric' else [yaw_offset, yaw_offset]
    return review.run(f"""
rows=[]
for i,w in enumerate(unreal.EditorLevelLibrary.get_pie_worlds(True)[1:]):
 pc=unreal.GameplayStatics.get_player_controller(w,0)
 cam=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.CameraActor) if a.get_actor_label()=='PerfReview_Camera')
 cam.set_actor_location(unreal.Vector(*{setup['camera']!r}),False,True)
 rot=unreal.MathLibrary.find_look_at_rotation(cam.get_actor_location(),unreal.Vector(*{setup['target']!r}))
 rot.yaw+={offsets!r}[i]
 if {view!r}=='offscreen' or ({view!r}=='asymmetric' and i==0):rot.pitch=45.0
 cam.set_actor_rotation(rot,True)
 cc=cam.get_component_by_class(unreal.CameraComponent)
 if {view!r}=='full':cc.set_field_of_view(90.0)
 pc.set_editor_property('bAutoManageActiveCameraTarget',False)
 pc.set_view_target_with_blend(cam,0)
 rows.append({{'world':w.get_path_name(),'yaw':rot.yaw,'pitch':rot.pitch,'offset':{offsets!r}[i], 'field_of_view':cc.get_editor_property('field_of_view')}})
unreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'views':rows}}))
""")


def profiles():
    return review.run("""
ws=list(unreal.EditorLevelLibrary.get_pie_worlds(True))
rows=[json.loads(s.get_capture_json()) for s in unreal.ObjectIterator(unreal.GuLiPerformanceSubsystem) if s.get_outer() in ws]
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'profiles':rows}))
""")['profiles']


def visible_fractions(rows, count):
    values = []
    for row in rows:
        counters = row.get('counters', {})
        visible = [v['mean_per_engine_frame'] for k, v in counters.items() if k.endswith('Units.Visible')]
        if visible:
            values.append(sum(visible) / count)
    return values


def calibrate_mixed(setup):
    # Outside formal captures; measure actual bounds visibility, not a guessed camera label.
    low, high, trials = 0.0, 180.0, []
    for _ in range(8):
        yaw = (low + high) / 2
        configure_view(setup, 'mixed', yaw)
        time.sleep(.5)
        review.result(PROFILES.replace('ACTION', 'begin'))
        time.sleep(1.5)
        review.result(PROFILES.replace('ACTION', 'end'))
        fractions = visible_fractions(profiles(), setup['population']['requested'])
        if not fractions:
            raise RuntimeError('Missing Units.Visible counters for camera calibration')
        ratio = sum(fractions) / len(fractions)
        trials.append({'yaw': yaw, 'fractions': fractions})
        if all(.4 <= x <= .6 for x in fractions):
            return {'yaw_offset': yaw, 'trials': trials}
        if ratio > .5:
            low = yaw
        else:
            high = yaw
    raise RuntimeError('Unable to establish mixed visibility: ' + str(trials))


def prepare_mixed_view(scene):
    """Calibrate in a separate PIE, then freeze one angle for every paired run."""
    path = OUT / 'Views' / (scene + '-mixed.json')
    if path.exists():
        return
    try:
        setup = fixture.launch(scene, 2, fixed_viewports=True, install_review_assets=False,
                               extra_previews=False, load_seconds=120)
        setup['target'] = [15000, 65000, 5401.779] if scene == 'stress' else [-10800, 65000, 1000]
        time.sleep(10)
        write(path, calibrate_mixed(setup))
    finally:
        review.stop()


def capture(scene, candidate, round_number, variant, view='onscreen', seconds=30, verify=False, phase='baseline',
            *, reuse_setup=None, warmup_seconds=10, stop_after=True):
    if variant != 'baseline':
        raise ValueError('Snapshot optimization candidates were rolled back; only baseline capture is available')
    name = f'{phase}-{scene}-{view}-{candidate}-r{round_number}-{variant}'
    directory = OUT / 'Paired' / name
    if (directory / 'result.json').exists():
        previous = json.loads((directory / 'result.json').read_text(encoding='utf-8'))
        if previous.get('view_eligible', True):
            return previous
    for completed in sorted((OUT/'Paired').glob(name+'-retry-*/result.json'), reverse=True):
        previous = json.loads(completed.read_text(encoding='utf-8'))
        if previous.get('view_eligible', True):
            return previous
    if directory.exists():
        directory = directory.with_name(name + '-retry-' + datetime.now().strftime('%H%M%S'))
    directory.mkdir(parents=True)
    print(json.dumps({'stage': 'setup', 'run': directory.name}), flush=True)
    log_path = ROOT / 'Saved/Logs/GuLiStrike.log'
    log_offset = log_path.stat().st_size
    fixture.commands([f'{k} {int(verify)}' for k in VERIFY], True)
    setup = dict(reuse_setup) if reuse_setup is not None else fixture.launch(
        scene, 2, fixed_viewports=True, install_review_assets=False,
        extra_previews=False, load_seconds=120)
    # The shared fixture disables expensive verification for its historical runs.
    fixture.commands([f'{k} {int(verify)}' for k in VERIFY])
    setup['target'] = [15000, 65000, 5401.779] if scene == 'stress' else [-10800, 65000, 1000]
    setup['view'] = view
    fixture.commands(['r.ScreenPercentage 100', 'r.DynamicRes.OperationMode 0'])
    # Same production value for both workloads; transient PIE connections only.
    setup['bandwidth'] = review.run("""
rows=[]
for w in unreal.EditorLevelLibrary.get_pie_worlds(True):
 r=json.loads(unreal.GuLiComponentSkillQALibrary.set_performance_bandwidth(w,250000))
 assert r['success'];rows.append(r)
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'worlds':rows}))
""")
    if view == 'mixed':
        setup['calibration'] = json.loads((OUT / 'Views' / (scene + '-mixed.json')).read_text(encoding='utf-8'))
    setup['actual_views'] = configure_view(setup, view, setup.get('calibration', {}).get('yaw_offset', 0))
    time.sleep(warmup_seconds)
    before = review.run(fixture.COUNTERS)
    context = review.run(SNAPSHOT)
    write(directory / 'counters-before.json', before)
    write(directory / 'context-before.json', context)
    views = [c for w in context['worlds'] for c in w['controllers'] if c['local']]
    assert len(views) == 2 and all(c['viewport'] == [1280,720] and c['view_target'] == 'PerfReview_Camera' for c in views), views
    assert all(not w['paused'] for w in before['worlds'])
    csv_path, trace_path = directory / 'frames.csv', directory / 'session.utrace'
    fixture.commands(['r.GPUCsvStatsEnabled 1'])
    review.result(PROFILES.replace('ACTION', 'begin'))
    capture_log_offset = log_path.stat().st_size
    try:
        fixture.commands([f'Trace.File {trace_path.as_posix()} cpu,gpu,frame,bookmark',
                          f'CsvProfile STARTFILE=../../../{csv_path.relative_to(ROOT).as_posix()}',
                          'CsvProfile START', 'Trace.Bookmark SnapshotPairStart'])
        started = time.monotonic()
        print(json.dumps({'stage': 'capture', 'run': directory.name, 'seconds': seconds}), flush=True)
        time.sleep(seconds)
        ended = time.monotonic()
    finally:
        fixture.commands(['Trace.Bookmark SnapshotPairEnd', 'CsvProfile STOP', 'Trace.Stop'])
        review.result(PROFILES.replace('ACTION', 'end'))
    elapsed = time.monotonic() - started
    write(directory / 'clock.json', {'window_monotonic_seconds': [started, ended],
                                    'clock_basis': 'Windows QPC seconds',
                                    'native_wall_seconds_offset': 16777216,
                                    'native_clock_basis': 'FWindowsPlatformTime::Seconds = QPC seconds + 2^24; unrelated to server clock error'})
    captured = profiles()
    write(directory / 'native-capture.json', captured)
    after = review.run(fixture.COUNTERS)
    write(directory / 'counters-after.json', after)
    write(directory / 'context-after.json', review.run(SNAPSHOT))
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        if csv_path.exists() and '[hasheaderrowatend]' in csv_path.read_text(encoding='utf-8', errors='replace')[-65536:].lower():
            break
        time.sleep(.25)
    stats = review.summarize_csv(csv_path)
    with log_path.open('rb') as stream:
        stream.seek(log_offset)
        run_log = stream.read()
    (directory / 'runtime.log').write_bytes(run_log)
    error_lines = [line for line in run_log.decode('utf-8', errors='replace').splitlines()
                   if ': Error:' in line or 'Ensure condition' in line]
    with log_path.open('rb') as stream:
        stream.seek(capture_log_offset)
        capture_log = stream.read().decode('utf-8', errors='replace')
    capture_errors = [line for line in capture_log.splitlines() if ': Error:' in line or 'Ensure condition' in line]
    for row in captured:
        samples = row['network_samples']
        assert len(samples) >= max(2, int(seconds) - 2), (row['world'], len(samples))
        for sample in samples:
            for c in sample['connections']:
                assert c['budget_bytes_per_second'] == 250000, c
    pop_before = next(w['native_population'] for w in before['worlds'] if 'native_population' in w)
    pop_after = next(w['native_population'] for w in after['worlds'] if 'native_population' in w)
    positions = {p['id']: p['position'] for p in pop_before.get('positions', [])}
    moved = sum(sum((v - positions[p['id']][i]) ** 2 for i, v in enumerate(p['position'])) > 100
                for p in pop_after.get('positions', []) if p['id'] in positions)
    feedback = [{k: a['effects'][k] - b['effects'][k] for k in
                 ['received_shots', 'muzzle_accepted', 'muzzle_born', 'muzzle_expired']}
                for b, a in zip(before['worlds'][1:], after['worlds'][1:])]
    receipt = {'scene': scene, 'candidate': candidate, 'round': round_number, 'variant': variant,
               'view': view, 'verification_enabled': verify, 'baseline': 'snapshot_candidates_removed',
               'setup': setup, 'warmup_seconds': warmup_seconds, 'capture_seconds': elapsed, 'csv': stats,
               'reused_pie': reuse_setup is not None,
               'moving_units_displaced': moved, 'alive_before': pop_before['alive'],
               'alive_after': pop_after['alive'], 'feedback': feedback,
               'visible_fractions': visible_fractions(captured, pop_before['requested']),
               'presentation_eligible': all(r['muzzle_born'] > 0 for r in feedback),
               'runtime_errors': error_lines,
               'capture_errors': capture_errors,
               'case': scene, 'pair': round_number,
               'build_manifest_sha256': hashlib.sha256((OUT / 'Build/verification.json').read_bytes()).hexdigest(),
               'trace': str(trace_path), 'native_capture': str(directory / 'native-capture.json')}
    write(directory / 'result.json', receipt)
    if stop_after:
        review.stop()
    print(json.dumps({'stage': 'done', 'run': directory.name, 'csv': stats,
                      'moved': moved, 'feedback': feedback}), flush=True)
    return receipt


def inspect_scene(scene):
    """Player entry using the same saved map and existing native population fixture."""
    setup = fixture.launch(scene, 2, fixed_viewports=True, install_review_assets=False,
                           extra_previews=False, load_seconds=120)
    directory = OUT / 'Manual' / datetime.now().strftime('%Y%m%d-%H%M%S')
    write(directory/'setup.json',setup)
    time.sleep(10)
    review.result(PROFILES.replace('ACTION','begin'))
    try:
        segment=0
        print('PIE ready. Commands: front / back / split / mixed; stop / reverse / forward for units. Enter quits and restores controls. Extra stress replenishment lasts at most 120 seconds.',flush=True)
        while True:
            command=input('Review command: ').strip().lower()
            review.result(PROFILES.replace('ACTION','end'))
            write(directory/f'segment-{segment:02d}.json',profiles())
            segment+=1
            if not command:break
            if command in ['front','back','split','mixed']:
                view={'front':'onscreen','back':'offscreen','split':'asymmetric','mixed':'mixed'}[command]
                angle=0
                if view=='mixed':
                    path=OUT/'Views'/(scene+'-mixed.json')
                    if path.exists():angle=json.loads(path.read_text(encoding='utf-8'))['yaw_offset']
                    else:print('No saved mixed-view calibration; using front view.',flush=True);view='onscreen'
                configure_view(setup,view,angle)
            elif command in ['stop','reverse','forward']:
                direction={'stop':0,'reverse':-1,'forward':1}[command]
                review.run(f"w=unreal.EditorLevelLibrary.get_pie_worlds(True)[0]\nunreal.MCPythonHelper.submit_result(json.dumps({{'success':unreal.GuLiComponentSkillQALibrary.order_performance_population(w,{direction})}}))")
            else:print('Unknown command; view unchanged.',flush=True)
            review.result(PROFILES.replace('ACTION','begin'))
    finally:
        # A player may already have ended PIE in the editor.
        worlds = review.run("unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'count':len(unreal.EditorLevelLibrary.get_pie_worlds(True))}))")['count']
        if worlds:
            review.result(PROFILES.replace('ACTION','end'))
            write(directory/'native-capture.json',profiles())
        review.stop()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--phase', choices=['smoke', 'baseline', 'inspect'], default='smoke')
    parser.add_argument('--scenes', nargs='+', default=['dense200'], choices=['dense200', 'stress'])
    parser.add_argument('--rounds', type=int, default=1)
    parser.add_argument('--seconds', type=int, default=30)
    parser.add_argument('--view', choices=['onscreen', 'full', 'mixed', 'offscreen', 'asymmetric'], default='onscreen')
    args = parser.parse_args()
    if args.rounds < 1 or args.seconds < 3:
        parser.error('rounds must be positive and seconds must be at least 3')
    session = OUT / 'Sessions' / (datetime.now().strftime('%Y%m%d-%H%M%S-') + args.phase)
    original = review.run(f"""
assert not unreal.EditorLevelLibrary.get_pie_worlds(True), 'An unrelated PIE session is active'
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert 'LVL_CommanderMassPrototype' in w.get_path_name(),w.get_path_name()
unreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'values':{{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in {KEYS!r}}}}}))
""")['values']
    write(session / 'original-controls.json', original)
    try:
        review.run("""
w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
n=unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(w,True)
r=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
unreal.MCPythonHelper.submit_result(json.dumps({'success':r.success and n.success,'navigation':n.message,'resource':r.message}))
""", timeout=180)
        for scene in args.scenes:
            if args.phase == 'inspect':
                inspect_scene(scene)
                continue
            if args.view == 'mixed':
                prepare_mixed_view(scene)
            for number in range(1, args.rounds + 1):
                capture(scene, 'rollback-native', number, 'baseline', args.view,
                        seconds=5 if args.phase == 'smoke' else args.seconds,
                        verify=False, phase=args.phase)
    finally:
        review.stop()
        fixture.commands([f'{k} {v:g}' for k, v in original.items()], True)
        restored = review.run(f"unreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'values':{{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in {KEYS!r}}}}}))")['values']
        write(session / 'restored-controls.json', restored)
        assert original == restored, 'Temporary controls did not restore exactly'


if __name__ == '__main__':
    main()
