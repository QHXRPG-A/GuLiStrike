"""Read the accepted static-only editable model. Never modify or save its geometry."""
import bpy, json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/WarMachineHover_20260929'
OUT.mkdir(parents=True,exist_ok=True)
SOURCE=ROOT/'ArtSource/MechanicalAnimation_20260929/WarMachine/WarMachine_RigidEditable.blend'
bpy.ops.wm.open_mainfile(filepath=str(SOURCE))
assert not any(o.type=='ARMATURE' for o in bpy.data.objects)
body=next(o for o in bpy.data.objects if o.type=='MESH' and 'Body' in o.name)
groups={g.index:g.name for g in body.vertex_groups}
result={'source':str(SOURCE),'coordinate_space':'UE authored centimeters; scale once at runtime','nozzles':[]}
for part,label in zip(range(6,10),['FL','FR','RL','RR']):
    points=[body.matrix_world @ v.co for v in body.data.vertices if any(groups[g.group]=='RigidPart_%02d'%part and g.weight>.5 for g in v.groups)]
    assert points
    lo=[min(p[i] for p in points)*100 for i in range(3)]
    hi=[max(p[i] for p in points)*100 for i in range(3)]
    result['nozzles'].append({'name':'FX_Hover_'+label,'part':part,'location_cm':[(lo[0]+hi[0])*.5,(lo[1]+hi[1])*.5,lo[2]],
        'disc_pivot_cm':[(lo[0]+hi[0])*.5,(lo[1]+hi[1])*.5,hi[2]],'diameter_cm':min(hi[0]-lo[0],hi[1]-lo[1])})
(OUT/'nozzles.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print(json.dumps(result))
