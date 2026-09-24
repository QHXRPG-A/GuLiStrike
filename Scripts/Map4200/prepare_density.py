"""Map the captured density field into the 2.8 km battlefield, preserving schema v1."""
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Artifacts/Map4200/20260922'
BLUE = [[3,3,4,5,4,3,3], [3,4,4,5,4,4,3], [4,4,5,5,5,4,4], [4,5,5,6,5,5,4]]
RED = [[0,0,0,2,0,0,0], [0,0,1,2,1,0,0], [0,1,2,2,2,1,0], [1,1,2,4,2,1,1]]
BLUE += BLUE[-2::-1]
RED += RED[-2::-1]


def main():
    source = json.loads((OUT / 'density-before.json').read_text(encoding='utf-8'))
    assert source['schema_version'] == 1 and source['cell_size_cm'] == 2500
    patches, report = [], []
    for layer in source['layers']:
        key = layer['layer_key']
        cells = {(c['cell_x'], c['cell_y']): c['density_u8'] for c in layer['cells']}
        remapped = {}
        counts = [[0]*7 for _ in range(7)]
        for iy in range(-56,56):
            for ix in range(-56,56):
                x,y = (ix+0.5)*2500, (iy+0.5)*2500
                col,row = math.floor((x+140000)/40000), math.floor((140000-y)/40000)
                cx,cy = (col-3)*40000, (3-row)*40000
                if abs(x-cx)>17000 or abs(y-cy)>17000: continue
                if math.hypot(x-cx,y-cy)<13000: continue
                if abs(x-round(x/40000)*40000)<2500 or abs(y-round(y/40000)*40000)<2500: continue
                if any(math.hypot(x,y-s*a)<12000 for s in (-1,1) for a in (110000,130000)): continue
                # Each new 25 m cell covers exactly four old cells under the 1/2 battlefield transform.
                weight = round(sum(cells.get((2*ix+dx,2*iy+dy),0) for dx in (0,1) for dy in (0,1))/4)
                if key == 'RedOre' and not RED[row][col]: continue
                # Old outpost/boundary reserves no longer match the new board. Fill only
                # those zero-weight holes from the closest painted ring, then apply the
                # new reserves above. Existing positive paint is unchanged.
                if not weight:
                    for radius in range(1,8):
                        ring=[cells.get((2*ix+dx,2*iy+dy),0)
                              for dy in range(-radius,radius+2) for dx in range(-radius,radius+2)
                              if dx in (-radius,radius+1) or dy in (-radius,radius+1)]
                        positive=[v for v in ring if v]
                        if positive:
                            weight=round(sum(positive)/len(positive))
                            break
                if weight: remapped[(ix,iy)] = weight
        # Integer cell centers reflect as (-i-1); averaging preserves the painted weighting without side bias.
        for cell in list(remapped):
            opposite = (-cell[0]-1,-cell[1]-1)
            weight = round((remapped.get(cell,0)+remapped.get(opposite,0))/2)
            remapped[cell] = remapped[opposite] = weight
        for (ix,iy),weight in remapped.items():
            row,col = math.floor((140000-(iy+.5)*2500)/40000), math.floor(((ix+.5)*2500+140000)/40000)
            counts[row][col] += weight>0
        budget = BLUE if key=='BlueOre' else RED
        assert all(counts[r][c]>=budget[r][c] for r in range(7) for c in range(7)), (key,counts)
        # A single patch per layer clears old cells and writes replacements; no intermediate empty field.
        merged = dict.fromkeys(cells,0)
        merged.update(remapped)
        patches.append({'schema_version':1,'layer_key':key,'cells':[
            {'cell_x':x,'cell_y':y,'density_u8':v} for (x,y),v in sorted(merged.items(),key=lambda p:(p[0][1],p[0][0]))]})
        report.append({'layer':key,'old_cells':len(cells),'new_cells':sum(v>0 for v in remapped.values()),'candidates_by_territory':counts})
    (OUT/'density-patches-4200.json').write_text(json.dumps(patches,separators=(',',':')),encoding='utf-8')
    (OUT/'density-remap-report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__': main()
