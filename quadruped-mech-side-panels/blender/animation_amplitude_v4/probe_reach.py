import bpy,math,json
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parent
p=ROOT.parent/'create_vertex_animations.py'
A={'__file__':str(p),'__name__':'amplitude_probe'}
exec(compile(p.read_text(encoding='utf-8').split('\nbase_scene = bpy.context.scene',1)[0],str(p),'exec'),A)
results=[]
for recenter in (0.,.1,.2,.3):
    required=0.;impossible=0
    for i in range(28*16+1):
        frame=1+i/16;phase=((frame-1)%28)/28
        matrix=A['body_matrix']((.078*math.sin(math.tau*phase),0,.126*math.cos(2*math.tau*phase)),math.radians(3.3)*math.sin(math.tau*phase),math.radians(2.25)*math.sin(math.tau*phase))
        for leg,d in A['LEGS'].items():
            hip=matrix@d['hip'];base=d['ankle'].copy();base.y+=(d['hip'].y-d['ankle'].y)*recenter
            old,alignment=A['walk_foot'](leg,phase);delta=old-d['ankle'];delta.z*=3;target=base+delta
            dx=target.x-hip.x;dy=target.y-hip.y
            reach=d['upper_length']+d['lower_length']-.015
            sq=reach*reach-dx*dx-dy*dy
            if sq<0:impossible+=1;continue
            allowed=target.z+math.sqrt(sq)-hip.z
            required=min(required,allowed)
    results.append({'recenter_fraction':recenter,'body_z_offset_needed':required,'impossible_horizontal_samples':impossible})
mining=[]
for adjustment in (0.,.10,.15,.18,.20):
    maximum=0.
    for i in range(72*8+1):
        frame=1+i/8
        old,offset,phase,laser=A['mining_pose'](frame)
        pitch=old.to_euler('XYZ').x
        mat=A['body_matrix'](tuple(2*x for x in offset),pitch*2,0)
        for leg,d in A['LEGS'].items():
            hip=mat@d['hip'];ankle=d['ankle'].copy()
            if 'Front' in leg:ankle.y+=adjustment
            distance=(ankle-hip).length
            maximum=max(maximum,distance-(d['upper_length']+d['lower_length']-.015))
    mining.append({'front_foot_recenter_m':adjustment,'reach_excess_m':maximum})
record={'walk':results,'mining_unreachable':mining}
(ROOT/'reach-probe.json').write_text(json.dumps(record,indent=2),encoding='utf-8')
print(json.dumps(record),flush=True)
