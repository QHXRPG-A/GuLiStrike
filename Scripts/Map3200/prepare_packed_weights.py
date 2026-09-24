"""Pack the resampled weight layers for a single native import without repaint balancing."""
import json
from pathlib import Path
import numpy as np
from PIL import Image


def main():
    out=Path(__file__).resolve().parents[2]/'Artifacts/Map3200/20260923/terrain'
    target=json.loads((out/'layout-3200.json').read_text(encoding='utf-8'))
    weights=[np.asarray(Image.open(out/('weight-3200-'+layer['layer_name']+'.png')),dtype=np.uint8) for layer in target['source_layers']]
    assert all(w.shape==(4081,4081) for w in weights)
    packed=np.stack(weights,axis=-1)
    (out/'weights-interleaved.raw').write_bytes(packed.tobytes())
    print(json.dumps({'success':True,'shape':list(packed.shape),'bytes':packed.size}))


if __name__=='__main__': main()
