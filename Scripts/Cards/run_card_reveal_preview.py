import json, traceback, unreal
from pathlib import Path
try:
    source=Path('D:/UE5.7/test1/Scripts/Cards/preview_card_reveal_worker.py')
    exec(compile(source.read_text(encoding='utf-8'),str(source),'exec'),globals())
except Exception:
    Path('D:/UE5.7/test1/ArtSource/UI/CardRevealDemo/runtime-preview.json').write_text(json.dumps({'success':False,'errors':[traceback.format_exc()]},indent=2),encoding='utf-8')
    unreal.SystemLibrary.quit_editor()
