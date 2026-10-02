"""Copy the independent relative-path source into the project delivery folder."""
from pathlib import Path
import json,shutil,hashlib

REPO=Path(__file__).resolve().parents[2]
NAME='GuLiStrike_CommanderIsland_1800m_v1'
MASTER=Path.home()/'Documents/Gaea/MCP/Projects'/NAME
DELIVERY=REPO/'ArtSource/Environment'/NAME
DEST=DELIVERY/'Source/Projects'/NAME
DEST.mkdir(parents=True,exist_ok=True)
for name in (NAME+'.terrain','layout.json','graph-manifest.json','REFERENCES.md','README.md'):
    shutil.copy2(MASTER/name,DEST/name)
for directory in ('inputs','scripts'):
    (DEST/directory).mkdir(exist_ok=True)
    for p in (MASTER/directory).iterdir():
        if p.is_file():shutil.copy2(p,DEST/directory/p.name)
terrain=json.loads((DEST/(NAME+'.terrain')).read_text(encoding='utf-8-sig'))
nodes=terrain['Assets']['$values'][0]['Terrain']['Nodes']
for n in nodes.values():
    if isinstance(n,dict) and n.get('$type','').startswith('QuadSpinner.Gaea.Nodes.File,'):
        assert n['RelativePath'] and (DEST/n['FileName']).is_file()
files=[p for p in DEST.rglob('*') if p.is_file()]
manifest={'success':True,'source_project':str(DEST/(NAME+'.terrain')),'master_project':str(MASTER/(NAME+'.terrain')),
    'relative_inputs_verified':True,'source_png_count':len(list((DEST/'inputs').glob('*.png'))),
    'files':{str(p.relative_to(DEST)):{'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in files}}
(DELIVERY/'source_package.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
print(json.dumps({k:manifest[k] for k in ('success','source_project','relative_inputs_verified','source_png_count')}))
