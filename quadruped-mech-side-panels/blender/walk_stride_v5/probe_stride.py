import math,json
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parent
p=ROOT.parent/'create_vertex_animations.py';A={'__file__':str(p),'__name__':'stride_probe'}
exec(compile(p.read_text(encoding='utf-8').split('\nbase_scene = bpy.context.scene',1)[0],str(p),'exec'),A)
results=[]
for stride in (1.2,1.44):
    for recenter in (.20,.25,.30,.35,.40):
        required=0.;impossible=0
        for i in range(28*16+1):
            frame=1+i/16;phase=((frame-1)%28)/28
            matrix=A['body_matrix']((.078*math.sin(math.tau*phase),0,.126*math.cos(2*math.tau*phase)),math.radians(1.5+3.3*math.sin(math.tau*phase)),math.radians(2.25)*math.sin(math.tau*phase))
            for leg,d in A['LEGS'].items():
                hip=matrix@d['hip'];base=d['ankle'].copy();base.y+=(d['hip'].y-d['ankle'].y)*recenter
                old,alignment=A['walk_foot'](leg,phase);delta=old-d['ankle'];delta.y*=stride/.72;delta.z*=3;target=base+delta
                dx=target.x-hip.x;dy=target.y-hip.y;reach=d['upper_length']+d['lower_length']-.02
                sq=reach*reach-dx*dx-dy*dy
                if sq<0:impossible+=1;continue
                required=min(required,target.z+math.sqrt(sq)-hip.z)
        results.append({'stride_m':stride,'recenter_fraction':recenter,'body_z_offset_needed':required,'impossible_horizontal_samples':impossible})
(ROOT/'reach-probe.json').write_text(json.dumps(results,indent=2),encoding='utf-8');print(json.dumps(results),flush=True)
