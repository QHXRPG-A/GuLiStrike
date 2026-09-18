"""Standalone source-editor visual worker; MCP plugins disabled to avoid sharing live port."""
import json,os,runpy,time,traceback
from pathlib import Path
import unreal
OUT=Path(unreal.Paths.project_dir())/'TestResults/Scale020/DemoReview'
OUT.mkdir(parents=True,exist_ok=True)
started=time.time()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
def finish(error=None):
    if globals().get('demo_watch'): unreal.unregister_slate_post_tick_callback(demo_watch)
    (OUT/'worker.json').write_text(json.dumps({'success':error is None,'error':error,'pid':os.getpid(),
        'started':started,'finished':time.time()},ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.SystemLibrary.quit_editor()
try:
    demo=runpy.run_path(str(Path(unreal.Paths.project_dir())/'Scripts/Scale020/demo_review.py'))
    if not any(demo['owned'](a) for a in demo['ACTORS'].get_all_level_actors()): demo['build']()
    else: assert demo['audit']()['success']
    demo['repair_review_visibility']()
    demo['gallery']()
    def watch(dt):
        try:
            p=OUT/'gallery.json'
            if p.exists() and p.stat().st_mtime>=started:
                data=json.loads(p.read_text(encoding='utf-8'))
                finish(None if data['success'] else data)
            elif time.time()-started>420: finish('Visual worker timeout')
        except Exception: finish(traceback.format_exc())
    demo_watch=unreal.register_slate_post_tick_callback(watch)
except Exception: finish(traceback.format_exc())
