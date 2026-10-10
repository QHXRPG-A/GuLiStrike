"""Client1 start/peak/stop/follow GIF from the saved opt-in comparison area.

This is visual evidence, outside performance sampling. Raw PNGs and timestamps
are retained; the GIF uses actual capture intervals, not a claimed stable FPS.
"""
import json
import sys
import time
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts'))
from Performance import run_four_stage_review as review

OUT = ROOT / 'outputs/performance/20261010-muzzle-batch/visual'
OUT.mkdir(exist_ok=True)
helpers = (ROOT / 'Scripts/Performance/muzzle_player_review.py').read_text(encoding='utf-8')
review.result(helpers)
review.result("guli_muzzle_review_start()")
deadline = time.monotonic()+90
while True:
    ready = review.run("ws=list(unreal.EditorLevelLibrary.get_pie_worlds(True))\n"
                       "unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'ready':len(ws)==3 "
                       "and all(unreal.GameplayStatics.get_player_controller(w,0) for w in ws)}))")
    if ready['ready']:
        break
    assert time.monotonic() < deadline
    time.sleep(.5)
review.run("w=unreal.EditorLevelLibrary.get_pie_worlds(True)[0]\n"
           "unreal.MCPythonHelper.submit_result(unreal.GuLiComponentSkillQALibrary.prepare_performance_population(w,2,True))")
review.result("w=unreal.EditorLevelLibrary.get_pie_worlds(True)[1]\n"
              "unreal.SystemLibrary.execute_console_command(w,'t.IdleWhenNotForeground 0')\n"
              "unreal.SystemLibrary.execute_console_command(w,'Slate.bAllowThrottling 0')\n"
              "guli_muzzle_review_view('near')")
time.sleep(1.5)
review.result("guli_muzzle_review_stop()")
time.sleep(1.25)
review.result("guli_muzzle_review_view('near')")
start = time.monotonic()
frames = []
stopped = False
try:
    for index in range(40):
        elapsed = time.monotonic()-start
        if elapsed >= 1.7 and not stopped:
            review.result("guli_muzzle_review_stop()")
            stopped = True
        path = OUT / f'frame-{index:03d}.png'
        code = "w=next(w for w in unreal.EditorLevelLibrary.get_pie_worlds(True) if '/UEDPIE_1_' in w.get_path_name())\n"
        code += "pc=unreal.GameplayStatics.get_player_controller(w,0)\n"
        code += f"assert unreal.GuLiComponentSkillQALibrary.capture_performance_viewport(pc,{path.as_posix()!r})\n"
        code += "fx=next(s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if s.get_outer()==w)\n"
        code += "unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'protocol':json.loads(fx.get_muzzle_protocol_snapshot()),'counters':str(fx.get_counters())}))"
        result = review.run(code)
        frames.append({'index': index, 'png': path.name, 'host_elapsed_seconds': time.monotonic()-start,
                       'phase': 'tail' if stopped else 'emission', 'protocol': result['protocol']})
        time.sleep(.08)
    review.result("guli_muzzle_review_view('offscreen')")
    time.sleep(.3)
    review.result("guli_muzzle_review_view('near')")
    time.sleep(.2)
    review.result("guli_muzzle_review_view('game')")
finally:
    review.stop()
    original = json.loads((OUT.parent / 'capture-root-original-controls.json').read_text(encoding='utf-8-sig'))
    original['gs.Muzzles.BatchMode'] = 2
    review.result("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
                  + '\n'.join(f"unreal.SystemLibrary.execute_console_command(w,{(k+' '+format(v,'g'))!r})"
                              for k, v in original.items()))
images = [Image.open(OUT / f['png']).convert('RGB') for f in frames]
durations = [max(40, int((b['host_elapsed_seconds']-a['host_elapsed_seconds'])*1000))
             for a, b in zip(frames, frames[1:])]+[500]
images[0].save(OUT / 'old-new-moving-follow.gif', save_all=True, append_images=images[1:],
               duration=durations, loop=0, disposal=2)
peak = max(frames, key=lambda f: len(f['protocol']['slots']))
for name, frame in [('start', frames[0]), ('peak', peak), ('stop', frames[-1])]:
    Image.open(OUT / frame['png']).save(OUT / f'{name}.png')
proof = {'success': True, 'world': 'Client1 / UEDPIE_1', 'map': '/Game/Maps/LVL_CommanderMassPrototype',
         'source': 'Saved MuzzleReview_ actors, old mode0 and batch mode2; normal and WM01 x2.',
         'gif': 'old-new-moving-follow.gif', 'frames': frames,
         'start': 0, 'peak': peak['index'], 'stop': frames[-1]['index'],
         'scope': 'Cosmetic synthetic comparison plus native moving-combat performance evidence separately. '
                  'Actual capture timestamps retained. Player visual approval pending.'}
(OUT / 'visual-evidence.json').write_text(json.dumps(proof, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps({'visual_frames': len(frames), 'peak_slots': len(peak['protocol']['slots']),
                  'stop_slots': len(frames[-1]['protocol']['slots']), 'gif': proof['gif']}), flush=True)
