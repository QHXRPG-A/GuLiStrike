"""Prepare collision-free editor review anchors from scalar grid data, without running UE."""
import json
from prepare_layout import Terrain,read,save,sha,OUT

def main():
    grid=Terrain();resources=read(OUT/'layout_resource.json')
    occupied=[(c['center'],3) for c in resources['clusters']]
    occupied += [(t['outpost_ground_location'],2) for t in resources['territories']]
    occupied += [(p,4) for k,p in resources['spawn_anchors'].items() if 'factory' in k]
    requested=[('Assembly',[0,55000]),('CrossWest',[-9000,24300]),('CrossEast',[9000,24300]),
               ('CrossNorth',[0,33300]),('CrossSouth',[0,15300]),('Slope',[-8500,35900]),
               ('NarrowRoad',[-36500,-33000]),('Diagonal',[19800,22500])]
    targets=[]
    for name,desired in requested:
        p=next((p for p in grid.candidates(desired,10,40) if grid.fits(p,10)
                and all(max(abs(p[i]-q[i]) for i in (0,1))>=(10+n)*150+300 for q,n in occupied)),None)
        if p is None:raise RuntimeError('No complete 10-cell review anchor near '+name)
        targets.append({'id':name,'location':[*p,grid.z(p)],'checked_footprint':10})
    report={'schema':'guli-grid300/review-v1','terrain_hash':read(OUT/'Terrain/manifest.json')['source_hash'],
            'targets':targets,'default_inactive':True,'automatic_gameplay':False,
            'selected_units':{'1':8,'2':8,'5':8,'6':1},'launch_population':200,
            'map':'/Game/Maps/LVL_CommanderMassPrototype'}
    save(OUT/'review.json',report)
    manifest=read(OUT/'layout_manifest.json');manifest['output_hashes']['review.json']=sha(OUT/'review.json')
    save(OUT/'layout_manifest.json',manifest)
    print(json.dumps({'anchors':len(targets),'positions':targets},ensure_ascii=False))

if __name__=='__main__':main()
