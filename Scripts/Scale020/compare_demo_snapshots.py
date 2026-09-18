"""Review deterministic snapshot differences. Never modifies a UE asset."""
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]/'TestResults/Scale020/DemoReview'
a=json.loads((ROOT/'environment_before.json').read_text(encoding='utf-8'))
b=json.loads((ROOT/'environment_current_diagnostic.json').read_text(encoding='utf-8'))
diff=[]
def compare(x,y,path=''):
    if isinstance(x,dict) and isinstance(y,dict):
        for k in sorted(x.keys()|y.keys()): compare(x.get(k),y.get(k),path+'/'+k)
    elif x!=y: diff.append({'path':path,'before':x,'after':y})
compare(a,b)
(ROOT/'snapshot-differences.json').write_text(json.dumps(diff,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'differences':len(diff),'sample':diff[:12]},ensure_ascii=False,indent=2))
