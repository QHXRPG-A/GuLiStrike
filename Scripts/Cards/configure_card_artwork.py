import sys,json,traceback,importlib
from pathlib import Path
sys.path.insert(0,'D:/UE5.7/test1/Scripts/Cards')
import card_artwork_config
importlib.reload(card_artwork_config)
from card_artwork_config import configure
out=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards/Inspection')
try:
    report=configure()
    report['success']=True
except Exception:
    report={'success':False,'error':traceback.format_exc()}
(out/'artwork-interface.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False))
