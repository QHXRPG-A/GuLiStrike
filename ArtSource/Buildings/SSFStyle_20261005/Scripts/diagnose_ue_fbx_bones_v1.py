import json
from pathlib import Path
from io_scene_fbx import parse_fbx
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');D=R/'UE_Delivery_v1'
source={a['key']:a for a in json.loads((R/'Source/source_manifest.json').read_text(encoding='utf8'))['meshes']}
out={}
for key in ('AirBase','CloningCenter','CommandCenter','MilitaryFactory','Reactor','StrategyCenter','Drone'):
 root,_=parse_fbx.parse(str(D/'UE_Readback'/(key+'.fbx')));objects=next(e for e in root.elems if e.id==b'Objects');edges=next(e for e in root.elems if e.id==b'Connections')
 models=[e for e in objects.elems if e.id==b'Model' and e.props[-1] in (b'Root',b'LimbNode')];indices={e.props[0]:i for i,e in enumerate(models)}
 parents={e.props[1]:e.props[2] for e in edges.elems if e.props[0]==b'OO' and e.props[1] in indices and e.props[2] in indices}
 rows=[]
 for i,model in enumerate(models):
  rows.append({'index':i,'FBX_name':model.props[1].split(b'\x00\x01')[0].decode(),'FBX_parent_index':indices.get(parents.get(model.props[0]),-1),'logical_name':source[key]['bones'][i]['name'] if i<len(source[key]['bones']) else None,'logical_parent_index':source[key]['bones'][i]['parent_index'] if i<len(source[key]['bones']) else None})
 out[key]={'model_count':len(models),'bone_count':len(source[key]['bones']),'hierarchy_indices_match':all(r['FBX_parent_index']==r['logical_parent_index'] for r in rows),'names':rows}
(D/'UE_FBX_bone_export_names.json').write_text(json.dumps(out,ensure_ascii=False,indent=2),encoding='utf8');print(json.dumps({k:{n:v for n,v in a.items() if n!='names'} for k,a in out.items()}));print(json.dumps([r for r in out['AirBase']['names'] if 'Anten' in r['FBX_name'] or 'Anten' in r['logical_name']]))
