"""Produce the existing footprint/placement data. This never enables grid pathfinding."""
from collections import deque
import numpy as np


def build(heights, water, origin, geometry):
    ny,nx=heights.shape[0]-1,heights.shape[1]-1
    a,b,c,d=heights[:-1,:-1],heights[:-1,1:],heights[1:,:-1],heights[1:,1:]
    slope=np.maximum(np.hypot(b-a,d-b),np.hypot(d-c,c-a))/300
    minimum=np.minimum.reduce([a,b,c,d]);walkable=(slope<=np.tan(np.deg2rad(44)))&(minimum>=0)
    if water is not None:walkable &= np.maximum.reduce([water[:-1,:-1],water[:-1,1:],water[1:,:-1],water[1:,1:]])<128
    for obstacle in geometry:
        center,extent=np.asarray(obstacle['bounds'],dtype=float);low,high=center-extent,center+extent
        x0,y0=np.maximum(0,np.floor((low[:2]-origin)/300).astype(int));x1,y1=np.minimum([nx,ny],np.ceil((high[:2]-origin)/300).astype(int))
        if x0>=x1 or y0>=y1:continue
        region=minimum[y0:y1,x0:x1]
        blocked=np.ones(region.shape,dtype=bool) if obstacle['kind']=='forbidden' else (high[2]>region+45)&(low[2]<region+3500)
        walkable[y0:y1,x0:x1]&=~blocked
    clearance=np.zeros((ny+1,nx+1),dtype=np.int32)
    for y in range(ny-1,-1,-1):
        for x in range(nx-1,-1,-1):
            if walkable[y,x]:clearance[y,x]=1+min(clearance[y,x+1],clearance[y+1,x],clearance[y+1,x+1])
    files={'walkable.u8':walkable.astype('u1').tobytes(),'clearance.i32':clearance[:-1,:-1].astype('<i4').tobytes()};hierarchies=[]
    for size in (1,2,4,10):
        fit=clearance[:-1,:-1]>=size;labels=np.full((ny,nx),-1,dtype=np.int32);count=0
        for by in range(0,ny,32):
            for bx in range(0,nx,32):
                y1,x1=min(by+32,ny),min(bx+32,nx)
                for y in range(by,y1):
                    for x in range(bx,x1):
                        if not fit[y,x] or labels[y,x]>=0:continue
                        queue=deque([(x,y)]);labels[y,x]=count
                        while queue:
                            qx,qy=queue.popleft()
                            for xx,yy in ((qx-1,qy),(qx+1,qy),(qx,qy-1),(qx,qy+1)):
                                if bx<=xx<x1 and by<=yy<y1 and fit[yy,xx] and labels[yy,xx]<0:labels[yy,xx]=count;queue.append((xx,yy))
                        count+=1
        links=[set() for _ in range(count)]
        for a,b in ((labels[:,:-1],labels[:,1:]),(labels[:-1,:],labels[1:,:])):
            keep=(a>=0)&(b>=0)&(a!=b)
            for u,v in zip(a[keep].tolist(),b[keep].tolist()):links[u].add(v);links[v].add(u)
        offsets=[0];neighbors=[]
        for group in links:neighbors.extend(sorted(group));offsets.append(len(neighbors))
        prefix='hierarchy_'+str(size)
        files[prefix+'_regions.i32']=labels.astype('<i4').tobytes();files[prefix+'_offsets.i32']=np.asarray(offsets,dtype='<i4').tobytes();files[prefix+'_neighbours.i32']=np.asarray(neighbors,dtype='<i4').tobytes()
        hierarchies.append({'footprint':size,'prefix':prefix,'regions':count,'edges':len(neighbors)})
    return files,hierarchies
