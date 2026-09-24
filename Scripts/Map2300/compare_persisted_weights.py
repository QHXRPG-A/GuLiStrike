"""Compare reloaded merged weights with the resampled originals, in uint8 levels."""
import json
from pathlib import Path
import numpy as np
from PIL import Image


def main():
    out=Path(__file__).resolve().parents[2]/'Artifacts/Map2300/20260923'
    terrain=out/'terrain'
    records=[]
    for path in sorted(terrain.glob('weight-2300-*.png')):
        source=np.asarray(Image.open(path))
        persisted=np.asarray(Image.open(terrain/path.name.replace('weight-2300-','persisted-weight-')))
        assert source.shape==persisted.shape==(4081,4081)
        difference=np.abs(source.astype(np.int16)-persisted.astype(np.int16))
        records.append({'name':path.name,'input_nonzero':int(np.count_nonzero(source)),
                        'persisted_nonzero':int(np.count_nonzero(persisted)),
                        'max_difference_u8':int(difference.max()),'mean_difference_u8':float(difference.mean()),
                        'changed_pixels':int(np.count_nonzero(difference))})
    # Original blended sums span 253..257; the engine normalizes the merged result
    # to 255. Up to two uint8 levels may change, but no layer may disappear.
    success=len(records)==3 and all(r['persisted_nonzero']>0 and r['max_difference_u8']<=2 for r in records)
    result={'success':success,'maximum_allowed_difference_u8':2,'layers':records}
    (out/'weight-persistence-refinement/comparison-final.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print(json.dumps(result))
    assert success,result


if __name__=='__main__': main()
