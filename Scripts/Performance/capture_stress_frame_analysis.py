"""Capture the loaded production build's 600-unit / 500-flight pressure frame.

ReadPixels, component inventory and ProfileGPU run OUTSIDE the measured window.
Only the acceptance producer duration is extended to 90 s for post-window work;
population, flight count, four producers, camera, quality and formal assets stay
identical to the whole-build fixture. No runtime optimization is disabled.
"""
from __future__ import annotations

import json
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts'))
from Performance import run_four_stage_review as review
from Performance import run_whole_optimization_review as whole

OUT = ROOT / 'outputs/performance/20261009-stress-frame-analysis'
KEYS = ['guli.stronghold.CaptureSeconds', 'guli.stronghold.TeamUnitCap',
        't.IdleWhenNotForeground', 'Slate.bAllowThrottling', 'r.GPUCsvStatsEnabled',
        'gs.Flights.DataPool', 'gs.Avoidance.QueryOptimizations',
        'gs.SceneUI.ProjectionCache', 'gs.Effects.BoundsCull',
        'gs.Avoidance.VerifyPrefix', 'gs.Avoidance.SparseCells',
        'r.ProfileGPU.ShowUI', 'r.ProfileGPU.Sort', 'r.ProfileGPU.ThresholdPercent',
        'r.ProfileGPU.UnicodeOutput']

INVENTORY = r"""
def stress_inventory():
 import collections
 worlds=list(unreal.EditorLevelLibrary.get_pie_worlds(True))
 rows=[]
 for world in worlds:
  assets=collections.defaultdict(lambda:{'components':0,'active':0,'tick_enabled':0,'visible':0,'owners':collections.Counter()})
  for comp in unreal.ObjectIterator(unreal.NiagaraComponent):
   if comp.get_world()!=world:continue
   asset=comp.get_asset()
   key=asset.get_path_name() if asset else '<none>'
   stat=assets[key]
   stat['components']+=1
   stat['active']+=int(comp.is_active())
   stat['tick_enabled']+=int(comp.is_component_tick_enabled())
   stat['visible']+=int(comp.is_visible())
   owner=comp.get_owner()
   stat['owners'][owner.get_class().get_name() if owner else '<none>']+=1
  classes=collections.Counter(a.get_class().get_name() for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Actor))
  rows.append({'world':world.get_path_name(),'game_seconds':unreal.GameplayStatics.get_time_seconds(world),
               'actors':dict(classes.most_common()),'niagara':dict(assets)})
 refs=[{'id':i,'path':unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(i).resource_path.get_path_name()}
       for i in [4,5,36,38,45,52]]
 return {'success':True,'worlds':rows,'formal_effects':refs}
unreal.MCPythonHelper.submit_result(json.dumps(stress_inventory()))
"""


def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    initial = review.run(f"unreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'worlds':len(unreal.EditorLevelLibrary.get_pie_worlds(True)),'values':{{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in {KEYS!r}}}}}))")
    assert initial['worlds'] == 0, 'Preserve an existing user PIE session; this capture expects stopped PIE.'
    write(OUT / 'initial-runtime.json', initial)
    run_native = review.run
    stop_native = review.stop
    launch_native = review.launch

    def extended_run(code, timeout=45):
        code = code.replace('f.start_load(500,45)', 'f.start_load(500,90)')
        return run_native(code, timeout)

    def launch(case, version):
        setup = launch_native('runtime', 'optimized_controls')
        setup['whole_effects'] = review.run(whole.FIXTURE)
        setup['acceptance_producer_seconds'] = 90
        setup['duration_reason'] = 'Keep 500-pressure producer running for post-window frame/inventory capture; simulation rules unchanged.'
        return setup

    record = None
    try:
        review.OUT = OUT
        review.run = extended_run
        review.launch = launch
        # Keep the exact captured World alive for post-window evidence.
        review.stop = lambda: None
        record = review.capture('runtime', 1, 'frame-diagnosis')
        record['scope'] = 'Loaded final formal assets and defaults. Same process dedicated server + two 1280x720 clients; 600 moving units, four producers x125 flights, 32 tool beams +32 flash previews/client; 10s warmup/30s unperturbed timing. Producer90s for post-window captures.'
        directory = next(p.parent for p in (OUT / 'paired').glob('*/result.json'))
        record['source_directory'] = directory.name
        write(directory / 'result.json', record)
        write(OUT / 'capture-result.json', record)
        write(OUT / 'post-window-inventory.json', review.run(INVENTORY))
        frames = review.run("""
rows=[]
for index,world in enumerate(unreal.EditorLevelLibrary.get_pie_worlds(True)[1:],1):
 controller=unreal.GameplayStatics.get_player_controller(world,0)
 path=""" + repr(OUT.as_posix()) + """+'/client-'+str(index)+'.png'
 ok=unreal.GuLiComponentSkillQALibrary.capture_performance_viewport(controller,path)
 rows.append({'world':world.get_path_name(),'client':index,'path':path,'captured':ok,'game_seconds':unreal.GameplayStatics.get_time_seconds(world)})
unreal.MCPythonHelper.submit_result(json.dumps({'success':all(r['captured'] for r in rows),'frames':rows,'timing_scope':'ReadPixels outside timing window; screenshots show same running pressure scene, not a synchronized trace frame.'}))
""")
        write(OUT / 'viewport-frames.json', frames)
        log = ROOT / 'Saved/Logs/GuLiStrike.log'
        offset = log.stat().st_size
        cmds = ['r.ProfileGPU.ShowUI 0',
                'r.ProfileGPU.Sort 0', 'r.ProfileGPU.ThresholdPercent 0.2',
                'r.ProfileGPU.UnicodeOutput 0', 'ProfileGPU']
        review.result("capture_world=unreal.EditorLevelLibrary.get_pie_worlds(True)[1]\n" + '\n'.join(
            f'unreal.SystemLibrary.execute_console_command(capture_world,{cmd!r})' for cmd in cmds))
        time.sleep(3)
        with log.open('rb') as stream:
            stream.seek(offset)
            (OUT / 'gpu-profile-frame.log').write_bytes(stream.read())
        write(OUT / 'post-gpu-counters.json', review.run(review.COUNTERS))
        print(json.dumps({'stage':'frame-evidence-complete','source':str(directory),'csv':record['csv'],'viewports':frames['frames']}, ensure_ascii=False), flush=True)
    finally:
        review.stop = stop_native
        review.run = run_native
        review.launch = launch_native
        stop_native()
        review.result("restore_world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n" + '\n'.join(
            f'unreal.SystemLibrary.execute_console_command(restore_world,{f"{k} {v:g}"!r})' for k,v in initial['values'].items()))
        write(OUT / 'restored-runtime.json', review.run(f"unreal.MCPythonHelper.submit_result(json.dumps({{'success':True,'worlds':len(unreal.EditorLevelLibrary.get_pie_worlds(True)),'values':{{k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in {KEYS!r}}}}}))"))


if __name__ == '__main__':
    main()
