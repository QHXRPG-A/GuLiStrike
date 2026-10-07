"""Build technical reference-only seam masks from source UV color-mask boundaries."""
from pathlib import Path
import json
import hashlib
import numpy as np
from PIL import Image, ImageFilter

ROOT = Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
OUT = ROOT / 'ReferenceMasks'
OUT.mkdir(exist_ok=True)
source = ROOT / 'Source/Textures'
records = []
for key in ['AirBase','CloningCenter','CommandCenter','MilitaryFactory','Reactor','StrategyCenter','Drone','Lamp']:
    files = list(source.glob('T_TB1_' + key + '*ColorMask.png'))
    if not files:
        files = list(source.glob('T_TB1_' + key + '*BaseColor.png'))
    if not files: continue
    im = Image.open(files[0]).convert('L').resize((2048,2048)).filter(ImageFilter.MedianFilter(3))
    # Thin outlines of existing recessed/panel regions; discard solid black fills.
    binary = im.point(lambda x: 255 if x < 90 else 0)
    thick = np.array(binary.filter(ImageFilter.MaxFilter(3)), dtype=np.int16)
    thin = np.array(binary.filter(ImageFilter.MinFilter(3)), dtype=np.int16)
    edges = Image.fromarray(np.clip(thick-thin, 0, 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(.35))
    target = OUT / (key + '_LineMask.png')
    edges.save(target)
    records.append({'asset': key, 'source': str(files[0].relative_to(ROOT)),
                    'mask': str(target.relative_to(ROOT)), 'size':[2048,2048],
                    'purpose': 'Reference-only source-UV structure boundaries; production atlas is made after A',
                    'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
(OUT/'mask_manifest.json').write_text(json.dumps(records, ensure_ascii=False, indent=2), encoding='utf-8')
print('Reference masks:', len(records))
