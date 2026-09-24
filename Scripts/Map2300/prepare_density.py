"""Resample existing painted weights into the 1.8 km battlefield, schema v1."""
from pathlib import Path
import json
import math

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT/'Artifacts/Map2300/20260923'
BLUE = [[1,1,2,2,3,2,2,1,1],[1,2,2,3,3,3,2,2,1],[2,2,3,3,4,3,3,2,2],[2,2,3,4,4,4,3,2,2],[2,3,4,4,6,4,4,3,2]]
RED = [[0,0,0,0,1,0,0,0,0],[0,0,0,0,1,0,0,0,0],[0,0,0,1,2,1,0,0,0],[0,0,1,1,2,1,1,0,0],[1,1,2,2,4,2,2,1,1]]
BLUE += BLUE[-2::-1]
RED += RED[-2::-1]


def main():
    source=json.loads((OUT/'density-before.json').read_text(encoding='utf-8'))
    army=json.loads((OUT/'initial-army-layout.json').read_text(encoding='utf-8'))
    assert source['schema_version']==1 and source['cell_size_cm']==2500
    assert sum(map(sum,BLUE))==200 and sum(map(sum,RED))==40
    patches,report=[],[]
    for layer in source['layers']:
        cells={(c['cell_x'],c['cell_y']):c['density_u8'] for c in layer['cells']}
        key=layer['layer_key'];budget=BLUE if key=='BlueOre' else RED
        mapped={};counts=[[0]*9 for _ in range(9)]
        for iy in range(-36,36):
            for ix in range(-36,36):
                x,y=(ix+.5)*2500,(iy+.5)*2500
                row,col=math.floor((90000-y)/20000),math.floor((x+90000)/20000)
                if not budget[row][col]:continue
                dx,dy=x-(col-4)*20000,y-(4-row)*20000
                # Density remains 25 m. Keep a cell if any of its nine new sample
                # positions can fit; the native bake rechecks every exact point.
                positions=[(x+ox,y+oy) for ox in (-625,0,625) for oy in (-625,0,625)]
                def valid(px,py):
                    lx,ly=px-(col-4)*20000,py-(4-row)*20000
                    return (abs(lx)<=8750 and abs(ly)<=8750 and math.hypot(lx,ly)>=6500
                            and abs(lx)>=3020 and abs(ly)>=3020
                            and all(math.hypot(px-r['x'],py-r['y'])>=r['radius_cm']+520+150 for r in army['reservations']))
                if not any(valid(px,py) for px,py in positions):continue
                sx,sy=x*1.5/2500-.5,y*1.5/2500-.5
                bx,by=math.floor(sx),math.floor(sy);fx,fy=sx-bx,sy-by
                weight=round(sum(cells.get((bx+xx,by+yy),0)*(fx if xx else 1-fx)*(fy if yy else 1-fy)
                                 for xx in (0,1) for yy in (0,1)))
                if not weight:
                    for radius in range(1,17):
                        values=[cells.get((bx+xx,by+yy),0) for yy in range(-radius,radius+2) for xx in range(-radius,radius+2)
                                if xx in (-radius,radius+1) or yy in (-radius,radius+1)]
                        values=[v for v in values if v]
                        if values:weight=round(sum(values)/len(values));break
                if weight:mapped[(ix,iy)]=weight
        for cell in list(mapped):
            opposite=(-cell[0]-1,-cell[1]-1)
            weight=round((mapped.get(cell,0)+mapped.get(opposite,0))/2)
            mapped[cell]=mapped[opposite]=weight
        for (ix,iy),weight in mapped.items():
            row,col=math.floor((90000-(iy+.5)*2500)/20000),math.floor(((ix+.5)*2500+90000)/20000)
            counts[row][col]+=weight>0
        assert all(counts[r][c]>=budget[r][c] for r in range(9) for c in range(9)),(key,counts)
        merged=dict.fromkeys(cells,0);merged.update(mapped)
        patches.append({'schema_version':1,'layer_key':key,'cells':[
            {'cell_x':x,'cell_y':y,'density_u8':v} for (x,y),v in sorted(merged.items(),key=lambda p:(p[0][1],p[0][0]))]})
        report.append({'layer':key,'old_cells':len(cells),'new_cells':sum(v>0 for v in mapped.values()),'candidates_by_territory':counts})
    (OUT/'density-patches-2300.json').write_text(json.dumps(patches,separators=(',',':')),encoding='utf-8')
    (OUT/'density-remap-report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({'success':True,'layers':[{'layer':r['layer'],'new_cells':r['new_cells']} for r in report]}))


if __name__=='__main__':main()
