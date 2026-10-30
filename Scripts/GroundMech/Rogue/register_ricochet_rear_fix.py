"""Archive the generated edit and register its canvas to the frozen layer coordinates.

This is source-production placement, not a rendered/visual acceptance test.
"""
import hashlib
import json
import shutil
import sys
from pathlib import Path
import numpy as np
from PIL import Image
sys.path.insert(0,'D:/gen-image/tools/ricochet-source-extraction')
import cv2
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'ArtSource/UI/GroundMechRogueCards/GroundMech_ChainRicochet/RearFix_20261029_v1'
ORIGINAL=ROOT/'ArtSource/UI/GroundMechRogueCards/GroundMech_ChainRicochet/CardProduction_20261026_v1/Layers/03_PlayerMech.png'
GENERATED=Path(r'C:\Users\a\.codex\generated_images\01a18150-677b-76e3-b70a-94874fa7ed67\exec-b39188b4-b8ca-464c-a2fd-3fe83fd3cbb6.png')

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    OUT.mkdir(parents=True,exist_ok=True)
    raw=OUT/'Raw_PlayerMech_RearFix.png'
    shutil.copy2(GENERATED,raw)
    target=np.asarray(Image.open(ORIGINAL).convert('RGBA'))
    source=np.asarray(Image.open(raw).convert('RGBA'))
    sift=cv2.SIFT_create(nfeatures=2500)
    kp_a,ds_a=sift.detectAndCompute(cv2.cvtColor(source[:,:,:3],cv2.COLOR_RGB2GRAY),(source[:,:,3]>220).astype('uint8')*255)
    kp_b,ds_b=sift.detectAndCompute(cv2.cvtColor(target[:,:,:3],cv2.COLOR_RGB2GRAY),(target[:,:,3]>220).astype('uint8')*255)
    good=[] if ds_a is None or ds_b is None else [a for a,b in cv2.BFMatcher().knnMatch(ds_a,ds_b,k=2) if a.distance<.70*b.distance]
    matrix=None;inliers=0
    if len(good)>=8:
        a=np.float32([kp_a[m.queryIdx].pt for m in good]);b=np.float32([kp_b[m.trainIdx].pt for m in good])
        matrix,mask=cv2.estimateAffinePartial2D(a,b,method=cv2.RANSAC,ransacReprojThreshold=8,maxIters=5000)
        if matrix is not None:
            inliers=int(mask.sum());scale=float(np.hypot(matrix[0,0],matrix[1,0]));angle=float(np.degrees(np.arctan2(matrix[1,0],matrix[0,0])))
            if inliers<8 or not .3<scale<2 or abs(angle)>5 or np.linalg.det(matrix[:,:2])<=0:matrix=None
    method='unchanged-limb-and-weapon feature registration'
    if matrix is None:
        # A uniform transform keeps the newly painted object rigid; no local warp or pixel restoration.
        a=Image.fromarray(source).getbbox();b=Image.fromarray(target).getbbox()
        scale=min((b[2]-b[0])/(a[2]-a[0]),(b[3]-b[1])/(a[3]-a[1]))
        tx=(b[0]+b[2])/2-scale*(a[0]+a[2])/2;ty=(b[1]+b[3])/2-scale*(a[1]+a[3])/2
        matrix=np.array([[scale,0,tx],[0,scale,ty]],dtype=np.float64);method='uniform alpha-envelope placement'
    f=source.astype(np.float32)/255;f[:,:,:3]*=f[:,:,3:4]
    mapped=cv2.warpAffine(f,matrix,(target.shape[1],target.shape[0]),flags=cv2.INTER_LANCZOS4,borderMode=cv2.BORDER_CONSTANT)
    mapped=np.clip(mapped,0,1);mapped[:,:,:3]=np.divide(mapped[:,:,:3],mapped[:,:,3:4],out=np.zeros_like(mapped[:,:,:3]),where=mapped[:,:,3:4]>1.e-6)
    dest=OUT/'03_PlayerMech_RearFix.png';Image.fromarray(np.uint8(np.clip(mapped,0,1)*255+.5),'RGBA').save(dest)
    inputs=[]
    for p in [ORIGINAL,ROOT/'ArtSource/UI/GroundMechRogueCards/GroundMech_ChainRicochet/CurrentGroundMech_A4B2_20261026/Input03_Views.png',ROOT/'ArtSource/UI/GroundMechRogueCards/GroundMech_ChainRicochet/CurrentGroundMech_A4B2_20261026/Input04_Weapons.png']:
        frozen=OUT/('Input'+str(len(inputs)+1)+'_'+p.name);shutil.copy2(p,frozen);inputs.append({'original':str(p),'frozen':str(frozen),'sha256':sha(frozen)})
    report={'generator':'built-in image_gen','generated_source':str(GENERATED),'raw':str(raw),'raw_sha256':sha(raw),'output':str(dest),'sha256':sha(dest),'size':list(Image.open(dest).size),'prompt':'Prompt.txt','prompt_sha256':sha(OUT/'Prompt.txt'),'inputs':inputs,
        'model_id':4001,'editor_reference_readback':'Artifacts/BurningRogue_20261029/editor_input_inventory.json','placement':{'method':method,'matrix':matrix.tolist(),'feature_inliers':inliers,'object_warp':False},'assistant_visual_test':False,'user_visual_acceptance':'pending','foreground_mask':'Original front/behind partition retained in the same registered coordinate system; no scenery/enemy/routing layer changed.'}
    (OUT/'manifest.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    from author_burning_cards import upsert,read
    book='GuLiStrikeCardArtwork.xlsx';rows=read(book,'Parameters');h=rows[0]
    row=next(r for r in rows[3:] if r[h.index('ArtworkId')]=='6' and r[h.index('Key')]=='Layer 3 Map')
    upsert(book,'Parameters',[{'id':int(row[0]),'SourceFile':dest.relative_to(ROOT).as_posix(),'Note':'A4B2真实背面结构修正；源图配准保留原画布位置'}])
    print(json.dumps({'success':True,'registered':str(dest),'placement':method,'inliers':inliers}))
if __name__=='__main__':main()
