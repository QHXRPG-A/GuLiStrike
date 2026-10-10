"""Observe the final normal dual-client startup; inject no population or flights."""
import json
import time
from datetime import datetime
from pathlib import Path
from run_four_stage_review import ROOT, run, result, snapshot, stop, summarize_csv, COUNTERS

OUT = ROOT / 'outputs/performance/20261009-all-optimizations/normal-current'
OUT.mkdir(parents=True, exist_ok=True)
original = json.loads((ROOT / 'outputs/performance/20261009-implementation/benchmark-original-cvars.json').read_text())
records = []
try:
    result("assert not unreal.EditorLevelLibrary.get_pie_worlds(True)\nw=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
           "unreal.SystemLibrary.execute_console_command(w,'guli.stronghold.CaptureSeconds 20')\n"
           "unreal.SystemLibrary.execute_console_command(w,'guli.stronghold.TeamUnitCap 300')\n"
           "unreal.SystemLibrary.execute_console_command(w,'t.IdleWhenNotForeground 0')\n"
           "unreal.SystemLibrary.execute_console_command(w,'Slate.bAllowThrottling 0')")
    run("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
        "n=unreal.GuLiNavigationBakeLibrary.prepare_world_navigation(w,True)\n"
        "r=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()\n"
        "unreal.MCPythonHelper.submit_result(json.dumps({'success':n.success and r.success}))", timeout=180)
    run("unreal.MCPythonHelper.submit_result(json.dumps({'success':unreal.GuLiComponentSkillQALibrary.start_performance_pie(False,1280,720)}))")
    deadline = time.monotonic() + 120
    while time.monotonic() < deadline:
        ready = snapshot()
        if len(ready['worlds']) == 3 and all(w['controllers'] for w in ready['worlds']):
            break
        time.sleep(.5)
    else:
        raise RuntimeError('Normal dual-client startup not ready')
    result("for cw in unreal.EditorLevelLibrary.get_pie_worlds(True)[1:]:\n"
           " cp=unreal.GameplayStatics.get_player_controller(cw,0)\n"
           " assert unreal.GuLiComponentSkillQALibrary.set_performance_viewport_size(cp,1280,720)")
    for sample in range(1, 4):
        directory = OUT / ('sample-' + str(sample))
        if directory.exists():
            directory = directory.with_name(directory.name + '-retry-' + datetime.now().strftime('%H%M%S'))
        directory.mkdir(exist_ok=False)
        print(json.dumps({'stage':'warmup','sample':sample,'seconds':10}), flush=True)
        time.sleep(10)
        before = snapshot()
        counters_before = run(COUNTERS)
        (directory/'context-before.json').write_text(json.dumps(before,indent=2),encoding='utf-8')
        (directory/'counters-before.json').write_text(json.dumps(counters_before,indent=2),encoding='utf-8')
        csv_path = directory / 'frames.csv'
        gpu_original = before['cvars']['r.GPUCsvStatsEnabled']
        def commands(values):
            result("w=unreal.EditorLevelLibrary.get_pie_worlds(True)[1]\n" +
                   '\n'.join(f'unreal.SystemLibrary.execute_console_command(w,{v!r})' for v in values))
        commands(['r.GPUCsvStatsEnabled 1', f'CsvProfile STARTFILE=../../../{csv_path.relative_to(ROOT).as_posix()}', 'CsvProfile START'])
        print(json.dumps({'stage':'capture','sample':sample,'seconds':30}), flush=True)
        time.sleep(30)
        commands(['CsvProfile STOP', f'r.GPUCsvStatsEnabled {gpu_original:g}'])
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            try:
                if csv_path.is_file() and b'[hasheaderrowatend]' in csv_path.read_bytes()[-65536:].lower():
                    break
            except PermissionError:
                pass
            time.sleep(.3)
        else:
            raise RuntimeError('Normal CSV did not finish')
        after = snapshot()
        counters_after = run(COUNTERS)
        record = {'sample':sample, 'source_directory':directory.name, 'csv':summarize_csv(csv_path),
                  'historical_user_fps':[15,20], 'scope':'Current normal startup, dedicated server + two clients, default roles/cameras and production. No injected 600-unit population, no flight load or preview VFX. CSV instrumentation only. Historical range is approximate, with unmatched camera/population/render resolution.'}
        for name, value in [('context-before',before),('context-after',after),('counters-before',counters_before),('counters-after',counters_after),('result',record)]:
            (directory / (name + '.json')).write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')
        records.append(record)
        print(json.dumps({'stage':'done','sample':sample,'csv':record['csv']}), flush=True)
finally:
    (OUT / 'results.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
    stop()
    result("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n" +
           '\n'.join(f'unreal.SystemLibrary.execute_console_command(w,{f"{k} {v:g}"!r})' for k,v in original.items()))
