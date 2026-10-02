"""Correct only budget records; preserve validated height and mask inputs."""
from pathlib import Path
import ast,json
from prepare_island_sources import BLUE,PROJECT,DELIVERY

layout_path=PROJECT/'layout.json'
layout=json.loads(layout_path.read_text(encoding='utf-8'))
layout['blue_budgets']=BLUE
layout['budget_method']='9x9 area overlap resampling; rotational largest-remainder allocation; explicit paired land-capacity transfers including protected coast corners and full cluster footprints, preserving 200/40.'
layout['terrain_grading_routes']=layout.get('terrain_grading_routes',layout['routes'])
layout['terrain_grading_pads']=layout.get('terrain_grading_pads',layout['pads'])
layout['routes']=[r for r in layout['routes'] if r['name']!='Outpost_R4C4_Link']
next(p for p in layout['outposts'] if p['name']=='Outpost_R4C4')['center_m']=[100,110]
next(p for p in layout['pads'] if p['name']=='Outpost_R4C4')['center_m']=[100,110]
for key in ('outposts','pads'):
    next(p for p in layout[key] if p['name']=='Outpost_R7C4')['center_m']=[-72.4286,-733.8571]
next(r for r in layout['routes'] if r['name']=='Outpost_R7C4_Link')['points']=[[-72.4286,-733.8571,9],[0,-740,9]]
for p in layout['outposts']:
    p['blue_cluster_budget']=BLUE[p['row']-1][p['column']-1]
for p in layout['pads']:
    if p['name'].startswith('Outpost_'):p['blue_cluster_budget']=BLUE[p['row']-1][p['column']-1]
for path in (layout_path,DELIVERY/'layout.json',DELIVERY/'UEImport/GaeaExports/layout.json'):
    path.write_text(json.dumps(layout,ensure_ascii=False,indent=2),encoding='utf-8')
p=PROJECT/'scripts/prepare_sources.py';text=p.read_text(encoding='utf-8');lines=text.splitlines(keepends=True)
changes=[]
for n in ast.parse(text).body:
    if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name) and n.targets[0].id in ('AUTHORING','PADS'):
        name=n.targets[0].id
        if name=='AUTHORING':
            value={k:layout[k] for k in ('outposts','pads','routes','terrain_grading_routes','terrain_grading_pads','board_dimension','factory_anchors_m','assembly_anchors_m','budget_method','blue_budgets','red_budgets','source_revision')}
        else:value=layout['terrain_grading_pads']
        changes.append((n.lineno-1,n.end_lineno,name+' = '+repr(value)+'\n'))
for start,end,replacement in sorted(changes,reverse=True):lines[start:end]=[replacement]
p.write_text(''.join(lines),encoding='utf-8')
print(json.dumps({'success':True,'blue_total':sum(map(sum,BLUE)),'terrain_inputs_changed':False}))
