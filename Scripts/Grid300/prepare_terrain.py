"""Resample the live terrain export on matching endpoints; preserve 16-bit vertical units."""
import hashlib
import json
from collections import deque
from pathlib import Path
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'ArtSource/Navigation/Grid300/Baseline/Terrain'
DEST=ROOT/'ArtSource/Navigation/Grid300/Terrain'
VERTICES=568
CELL=300.0

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def resample(values):
    # PIL's pixel-centre resize shifts map boundaries. Landscape vertices must share endpoints.
    x=np.linspace(0,values.shape[1]-1,VERTICES)
    y=np.linspace(0,values.shape[0]-1,VERTICES)
    ix=np.floor(x).astype(np.int32); iy=np.floor(y).astype(np.int32)
    jx=np.minimum(ix+1,values.shape[1]-1); jy=np.minimum(iy+1,values.shape[0]-1)
    fx=x-ix;fy=(y-iy)[:,None]
    top=values[iy[:,None],ix]*(1-fx)+values[iy[:,None],jx]*fx
    bottom=values[jy[:,None],ix]*(1-fx)+values[jy[:,None],jx]*fx
    return top*(1-fy)+bottom*fy

def main():
    source=json.loads((SOURCE/'manifest.json').read_text(encoding='utf-8'))
    assert source['resolution']==[4081,4081]
    assert np.allclose(source['location'][:2],[-90000,-90000])
    assert np.allclose(np.array(source['scale'][:2])*4080,[180000,180000])
    assert sha(SOURCE/source['height'])==source['height_sha256']
    old=np.asarray(Image.open(SOURCE/source['height']),dtype=np.float64)
    source_height_raw_sha256=hashlib.sha256(old.astype('<u2').tobytes()).hexdigest()
    height=np.rint(resample(old)).astype(np.uint16)
    values=[];source_weight_raw_sha256={}
    for layer in source['weights']:
        assert sha(SOURCE/layer['file'])==layer['sha256']
        original=np.asarray(Image.open(SOURCE/layer['file']),dtype=np.uint8)
        source_weight_raw_sha256[layer['layer']]=hashlib.sha256(original.tobytes()).hexdigest()
        values.append(resample(original.astype(np.float64)))
    values=np.stack(values,axis=-1)
    total=values.sum(axis=-1,keepdims=True)
    assert total.min()>0
    values=values/total*255
    weights=np.rint(values).astype(np.int16)
    residual=255-weights.sum(axis=-1)
    yy,xx=np.indices((VERTICES,VERTICES))
    weights[yy,xx,np.argmax(values,axis=-1)]+=residual
    assert weights.min()>=0 and weights.max()<=255 and np.all(weights.sum(axis=-1)==255)
    DEST.mkdir(parents=True,exist_ok=True)
    Image.fromarray(height).save(DEST/'height_568.png')
    height.astype('<u2').tofile(DEST/'height_568.r16')
    for i,layer in enumerate(source['weights']):
        values_u8=weights[:,:,i].astype(np.uint8)
        Image.fromarray(values_u8).save(DEST/(layer['layer']+'_568.png'))
        values_u8.tofile(DEST/(layer['layer']+'_568.u8'))
    z=(height.astype(np.float64)-32768)/128*source['scale'][2]+source['location'][2]
    # Conservative maximum gradient of both Landscape triangles; no 3 m height quantization.
    dx0=(z[:-1,1:]-z[:-1,:-1])/CELL;dx1=(z[1:,1:]-z[1:,:-1])/CELL
    dy0=(z[1:,:-1]-z[:-1,:-1])/CELL;dy1=(z[1:,1:]-z[:-1,1:])/CELL
    slope=np.hypot(np.maximum(np.abs(dx0),np.abs(dx1)),np.maximum(np.abs(dy0),np.abs(dy1)))
    water=weights[:,:,next(i for i,l in enumerate(source['weights']) if l['layer']=='Water')]
    wet=np.maximum.reduce([water[:-1,:-1],water[:-1,1:],water[1:,:-1],water[1:,1:]])>=128
    minimum=np.minimum.reduce([z[:-1,:-1],z[:-1,1:],z[1:,:-1],z[1:,1:]])
    walkable=(slope<=np.tan(np.deg2rad(44))) & ~wet & (minimum>=0)
    # Persist the current level's authored forbidden areas and physical blockers.
    # Bounds are read through the editor, never by parsing .umap/.uasset bytes.
    base=SOURCE.parent
    geometry=json.loads((base/'geometry.json').read_text(encoding='utf-8'))
    editor=json.loads((base/'editor.json').read_text(encoding='utf-8'))
    actors={a['name']:a for a in editor['actors']}
    obstacles=[]
    for g in geometry['actors']:
        forbidden=g['class']=='NavModifierVolume' and g.get('area_class','').endswith('NavArea_Null')
        physical=g['class']=='StaticMeshActor' and any(
            c['values'].get('can_ever_affect_navigation')=='True' and 'NO_COLLISION' not in c['values'].get('collision_enabled','NO_COLLISION')
            for c in g['components'])
        if not forbidden and not physical:continue
        a=actors[g['name']];center,extent=np.asarray(a['bounds'],dtype=float)
        low=center-extent;high=center+extent;low[:2]*=.945;high[:2]*=.945
        x0,y0=np.maximum(0,np.floor((low[:2]+85050)/300).astype(int))
        x1,y1=np.minimum(567,np.ceil((high[:2]+85050)/300).astype(int))
        if x0>=x1 or y0>=y1:continue
        region=minimum[y0:y1,x0:x1]
        # Flat helpers entirely below the Landscape do not form a second navigation floor.
        blocked=np.ones(region.shape,dtype=bool) if forbidden else (high[2]>region+45) & (low[2]<region+3500)
        walkable[y0:y1,x0:x1]&=~blocked
        obstacles.append({'actor':a['path'],'label':a['label'],'kind':'forbidden' if forbidden else 'collision',
                          'source_bounds':a['bounds'],'target_xy_bounds':[low[:2].tolist(),high[:2].tolist()],
                          'blocked_cells':int(blocked.sum())})
    (DEST/'static_geometry.json').write_text(json.dumps(obstacles,ensure_ascii=False,indent=2),encoding='utf-8')
    clearance=np.zeros((568,568),dtype=np.int32)
    for y in range(566,-1,-1):
        for x in range(566,-1,-1):
            if walkable[y,x]:clearance[y,x]=1+min(clearance[y,x+1],clearance[y+1,x],clearance[y+1,x+1])
    z.astype('<f4').tofile(DEST/'heights_cm.f32')
    walkable.astype('u1').tofile(DEST/'walkable.u8')
    clearance[:-1,:-1].astype('<i4').tofile(DEST/'clearance.i32')
    hierarchies=[]
    for size in (1,2,4,10):
        fit=clearance[:-1,:-1]>=size
        labels=np.full((567,567),-1,dtype=np.int32)
        region=0
        for by in range(0,567,32):
            for bx in range(0,567,32):
                y1=min(by+32,567);x1=min(bx+32,567)
                for y in range(by,y1):
                    for x in range(bx,x1):
                        if not fit[y,x] or labels[y,x]>=0:continue
                        queue=deque([(x,y)]);labels[y,x]=region
                        while queue:
                            qx,qy=queue.popleft()
                            for nx,ny in ((qx-1,qy),(qx+1,qy),(qx,qy-1),(qx,qy+1)):
                                if bx<=nx<x1 and by<=ny<y1 and fit[ny,nx] and labels[ny,nx]<0:
                                    labels[ny,nx]=region;queue.append((nx,ny))
                        region+=1
        adjacent=[set() for _ in range(region)]
        for a,b in ((labels[:,:-1],labels[:,1:]),(labels[:-1,:],labels[1:,:])):
            mask=(a>=0)&(b>=0)&(a!=b)
            for u,v in zip(a[mask].tolist(),b[mask].tolist()):adjacent[u].add(v);adjacent[v].add(u)
        offsets=[0];neighbours=[]
        for links in adjacent:neighbours.extend(sorted(links));offsets.append(len(neighbours))
        prefix='hierarchy_'+str(size)
        labels.astype('<i4').tofile(DEST/(prefix+'_regions.i32'))
        np.asarray(offsets,dtype='<i4').tofile(DEST/(prefix+'_offsets.i32'))
        np.asarray(neighbours,dtype='<i4').tofile(DEST/(prefix+'_neighbours.i32'))
        hierarchies.append({'footprint':size,'regions':region,'edges':len(neighbours),'prefix':prefix})
    files={p.name:sha(p) for p in sorted(DEST.iterdir()) if p.is_file() and p.name!='manifest.json'}
    manifest={'schema':'guli-grid300/terrain-v1','source_manifest_sha256':sha(SOURCE/'manifest.json'),
              'source_height_raw_sha256':source_height_raw_sha256,'source_weight_raw_sha256':source_weight_raw_sha256,
              'source_hash':hashlib.sha256(json.dumps(files,sort_keys=True).encode()).hexdigest(),
              'map':source['map'],'resolution':[568,568],'cells':[567,567],'cell_cm':CELL,
              'origin':[-85050,-85050],'location':[-85050,-85050,source['location'][2]],
              'scale':[300,300,source['scale'][2]],'components':[9,9],'quads_per_component':63,
              'layout_scale_xy':0.945,'height_quantum_cm':source['scale'][2]/128,
              'maximum_slope_degrees':44,'walkable_cells':int(walkable.sum()),
              'hierarchies':hierarchies,
              'clearance_counts':{str(n):int((clearance[:-1,:-1]>=n).sum()) for n in (1,2,3,4,5,10,12)},
              'files':files,'material':source['material'],
              'height_range_cm':[float(z.min()),float(z.max())]}
    (DEST/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:v for k,v in manifest.items() if k!='files'},ensure_ascii=False,indent=2))

if __name__=='__main__':main()
