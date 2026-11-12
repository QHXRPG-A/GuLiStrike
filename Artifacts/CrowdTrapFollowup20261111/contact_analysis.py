import json,math,sys
from pathlib import Path
p=Path(__file__).resolve().parent
d=json.loads((p/sys.argv[1]).read_text())
u={s['id']:s for s in d['soldiers']}
def sub(a,b):return [a[i]-b[i] for i in range(2)]
def mul(a,b):return [v*b for v in a]
def add(a,b):return [a[i]+b[i] for i in range(2)]
def dot(a,b):return sum(x*y for x,y in zip(a,b))
def norm(a):return mul(a,1/math.hypot(*a))
def hits(id,pos,delta):
    rr=u[id]['radius']+2;out=[]
    for s in u.values():
        if s['id']==id or not s['present'] or s['movement_layer']!=u[id]['movement_layer']:continue
        m=sub(pos,s['position']);A=dot(delta,delta);B=dot(m,delta);C=dot(m,m)-(rr+s['radius'])**2
        if C<=0:
            if B>=0 and A>1e-8:continue
            t=0
        else:
            dis=B*B-A*C
            if A<=1e-8 or B>=0 or dis<=0:continue
            t=(-B-math.sqrt(dis))/A
        if 0<=t<1:out.append((t,s['id']))
    return sorted(out)
b=u[245];f=norm(sub(b['waypoint'],b['position']));preferred=b['avoidance_side'] or -1
y=[f[1]*preferred,-f[0]*preferred]
first=hits(245,b['position'],mul(f,288))
print('mover',b['position'],'forward',f,'yield',y,'first',first[:3])
id=first[0][1];seen=set()
while id not in seen:
    seen.add(id);n=u[id];dist=n['normal_speed']*.1;hi=hits(id,n['position'],mul(y,dist))
    if not hi:
        print(id,'clear');break
    t,h=hi[0];contact=dist*t;skin=.025/8.5*n['radius'];travel=max(0,contact-skin)
    pos=add(n['position'],mul(y,travel));normal=norm(sub(add(n['position'],mul(y,contact)),u[h]['position']));inward=dot(y,normal)
    tangent=sub(y,mul(normal,inward));slide=mul(tangent,dist-contact)
    shi=hits(id,pos,slide) if inward<0 else []
    print(id,'direct',hi[:2],'travel',travel,'slide',slide,'slidehits',shi[:2],'yield',n['yield_remaining'])
    id=shi[0][1] if shi else h
print('end',id)
