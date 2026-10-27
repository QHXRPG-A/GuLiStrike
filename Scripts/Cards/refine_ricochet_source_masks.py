"""Refine original-pixel ownership with a segmentation-only model, not generation."""
from pathlib import Path
import json
import sys
import numpy as np
from PIL import Image
from scipy import ndimage

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/UI/GroundMechRogueCards/GroundMech_ChainRicochet/CardProduction_20261026_v1'
TOOLS=Path('D:/gen-image/tools/ricochet-source-extraction')
sys.path.insert(0,str(TOOLS))
import torch
from segment_anything import sam_model_registry,SamPredictor

device='cuda' if torch.cuda.is_available() and torch.cuda.mem_get_info()[0]>4*1024**3 else 'cpu'
print('Segmentation device:',device,flush=True)
model=sam_model_registry['vit_b'](checkpoint=str(TOOLS/'sam_vit_b_01ec64.pth')).to(device)
predictor=SamPredictor(model)
rgb=np.asarray(Image.open(OUT/'Master.png').convert('RGB')).copy()
predictor.set_image(rgb)
owner=np.asarray(Image.open(OUT/'Masks/Ownership.png')).copy()


def segment(box,positive,negative=()):
    pts=np.asarray([*positive,*negative],dtype=np.float32)
    labels=np.asarray([1]*len(positive)+[0]*len(negative),dtype=np.int32)
    with torch.inference_mode():
        masks,scores,_=predictor.predict(point_coords=pts,point_labels=labels,box=np.asarray(box),multimask_output=False)
    print(json.dumps({'box':box,'mask_pixels':int(masks[0].sum()),'confidence':round(float(scores[0]),5)}),flush=True)
    return masks[0]


mech=segment([0,190,665,888],[(163,298),(322,339),(296,500),(435,631),(522,766),(584,839),(166,492)],
             [(525,533),(630,635),(245,667),(39,737)])
enemies=np.zeros((1024,1024),dtype=bool)
for box,pts in [([477,205,669,410],[(532,249),(589,334),(612,319)]),
                ([608,328,838,491],[(690,375),(760,412),(716,452)]),
                ([812,391,1023,605],[(911,497),(975,543),(855,511)])]:
    enemies |= segment(box,pts)
wall=segment([645,570,1023,1023],[(941,760),(906,858),(828,943),(1000,990)],[(724,716),(759,796)])
rocks=segment([0,807,465,1023],[(66,937),(172,979),(306,1004)],[(232,848)])

# Keep the colour-selected attached flames, trail, blast and dirt extraction;
# use the new whole-object masks to prevent cream armour entering the dirt layer.
old_mech=owner==3
old_enemies=owner==5
foreground=owner==1
foreground &= ~mech & ~enemies
foreground |= (wall|rocks) & ~mech & ~enemies
base=owner.copy()
base[(base==3)|(base==5)|(base==1)]=4
base[old_enemies|enemies]=5
base[old_mech|mech]=3
base[foreground]=1
base[owner==2]=2
owner=base
Image.fromarray(owner).save(OUT/'Masks/Ownership.png')
names=['01_Foreground','02_SpeedLines','03_PlayerMech','04_Scene','05_Ricochet_Enemies','06_FarBackground']
counts={}
for index,name in enumerate(names,1):
    mask=owner==index
    counts[str(index)]=int(mask.sum())
    Image.fromarray(mask.astype(np.uint8)*255).save(OUT/'Masks'/f'{name}.png')
    core=np.zeros((1024,1024,4),dtype=np.uint8)
    core[mask,:3]=rgb[mask]
    core[:,:,3]=mask.astype(np.uint8)*255
    Image.fromarray(core).save(OUT/'CoreLayers'/f'{name}.png')
record=json.loads((OUT/'source-extraction.json').read_text(encoding='utf-8'))
record.update(pixel_ownership_counts=counts,mask_method='SAM vit_b segmentation + source-coordinate constrained extraction; original RGB copied unchanged',
              segmentation_model={'name':'Meta Segment Anything vit_b','checkpoint':str(TOOLS/'sam_vit_b_01ec64.pth'),'device':device,'used_for':'masks only; no generated RGB'},
              assistant_visual_validation=False)
(OUT/'source-extraction.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'ownership_counts':counts,'partition_pixels':sum(counts.values()),'finished':True}),flush=True)
