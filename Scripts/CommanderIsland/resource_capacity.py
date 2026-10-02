"""Inspect geometric capacity before changing source anchor/road records."""
import json,math
from pathlib import Path
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]/'ArtSource/Environment/GuLiStrike_CommanderIsland_1800m_v1'
layout=json.loads((ROOT/'layout.json').read_text(encoding='utf-8'))
army=json.loads((ROOT/'EditorEvidence/army_current.json').read_text(encoding='utf-8'))
h=np.asarray(Image.open(ROOT/'UEImport/LandscapeInputs/height_4081_ue.png'),dtype=np.float32)/65535*140-20
gy,gx=np.gradient(h,1800/4080);slope=np.rad2deg(np.arctan(np.hypot(gx,gy)))

def candidates(row,column,mirror_home=False):
    cx=(column-4)*1800/7;cy=(4-row)*1800/7;half=900/7-12.5
    axis=np.array([v+f*25 for v in np.arange(-900,900,25) for f in (.25,.5,.75)])
    xs=axis[np.abs(axis-cx)<=half];ys=axis[np.abs(axis-cy)<=half]
    x,y=np.meshgrid(xs,ys);p=np.stack([x.ravel(),y.ravel()],axis=1)
    clear=np.ones(len(p),bool)
    for sign in (1,-1):
        q=p*sign;px,py=q.T
        ix=np.rint((px+900)/1800*4080).astype(int);iy=np.rint((py+900)/1800*4080).astype(int)
        clear &= (h[iy,ix]>.3)&(slope[iy,ix]<=15)
        for outpost in layout['outposts']:
            ax,ay=outpost['center_m']
            if mirror_home and outpost['name']=='Outpost_R7C4':ax,ay=-72.4286,-733.8571
            clear &= np.hypot(px-ax,py-ay)>=65
        for r in army['reservations']:
            ax,ay=r['x']/100,r['y']/100
            if mirror_home and r['label']=='Outpost_R7C4':ax,ay=-72.4286,-733.8571
            clear &= np.hypot(px-ax,py-ay)>=r['radius_cm']/100+6.7
        for route in layout['routes']:
            points=list(route['points'])
            if mirror_home and route['name']=='Outpost_R7C4_Link':points=[[-72.4286,-733.8571,9],[0,-740,9]]
            d=np.full(len(p),np.inf)
            for a,b in zip(points[:-1],points[1:]):
                vx,vy=b[0]-a[0],b[1]-a[1];t=np.clip(((px-a[0])*vx+(py-a[1])*vy)/(vx*vx+vy*vy),0,1)
                d=np.minimum(d,np.hypot(px-a[0]-t*vx,py-a[1]-t*vy))
            clear &= d>route['minimum_width_m']/2+5.2
    return p[clear]

def pack(p,iterations=1000,needed=100):
    rng=np.random.default_rng(20261001)
    dist=np.sum((p[:,None,:]-p[None,:,:])**2,axis=2)
    best=[]
    for iteration in range(iterations):
        order=rng.permutation(len(p));selected=[]
        for i in order:
            if not selected or np.all(dist[i,selected]>=55**2):selected.append(i)
        if len(selected)>len(best):best=selected
        if len(best)>=needed:break
    return p[best].tolist()

if __name__=='__main__':
    result=[]
    for mirror in (False,True):
        p=candidates(1,4,mirror);points=pack(p,needed=6)
        result.append({'mirror_home':mirror,'candidate_count':len(p),'packing_count':len(points),'packing':points})
    print(json.dumps(result,indent=2))
