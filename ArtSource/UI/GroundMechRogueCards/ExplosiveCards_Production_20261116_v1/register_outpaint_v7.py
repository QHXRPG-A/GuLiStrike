import json
import cv2,numpy as np
from PIL import Image
from pathlib import Path
root=Path(__file__).resolve().parent
jobs=json.loads((root/'generation_v7.json').read_text(encoding='utf8'))
sift=cv2.SIFT_create(nfeatures=18000,contrastThreshold=.008)
out=[]
for j in jobs:
 if not Path(j['input']).exists() or not Path(j['target']).exists():
  continue  # Superseded candidates were removed at user-requested closure.
 a=np.asarray(Image.open(j['input']).convert('RGBA')); b=np.asarray(Image.open(j['target']).convert('RGBA'))
 k0,d0=sift.detectAndCompute(cv2.cvtColor(a[:,:,:3],cv2.COLOR_RGB2GRAY),cv2.erode((a[:,:,3]>100).astype('uint8')*255,np.ones((3,3),'uint8')))
 k1,d1=sift.detectAndCompute(cv2.cvtColor(b[:,:,:3],cv2.COLOR_RGB2GRAY),cv2.erode((b[:,:,3]>100).astype('uint8')*255,np.ones((3,3),'uint8')))
 matches=cv2.BFMatcher().knnMatch(d1,d0,k=2); good=[m for m,n in matches if m.distance<.74*n.distance]
 src=np.float32([k1[m.queryIdx].pt for m in good]);dst=np.float32([k0[m.trainIdx].pt for m in good])
 regions={
  'all':np.ones(len(good),bool),
  'left':dst[:,0]<450,'right':dst[:,0]>702,
  'top':dst[:,1]<550,'bottom':dst[:,1]>1200,
  'bottom_left':(dst[:,0]<450)&(dst[:,1]>1000),
  'bottom_right':(dst[:,0]>702)&(dst[:,1]>1000),
 }
 fits={}
 for label,m in regions.items():
  if m.sum()<4:continue
  M,mask=cv2.estimateAffine2D(src[m],dst[m],method=cv2.RANSAC,ransacReprojThreshold=3,maxIters=20000)
  if M is None:continue
  count=int(mask.sum())
  if count<4:continue
  pred=np.c_[src[m],np.ones(m.sum())]@M.T
  error=np.linalg.norm(pred-dst[m],axis=1)[mask[:,0]>0]
  fits[label]={'matches':int(m.sum()),'inliers':count,'M':M.tolist(),'error_median':float(np.median(error)),'source_span':np.ptp(src[m][mask[:,0]>0],axis=0).tolist()}
 print(j['card'],j['index'],'size',b.shape[:2],json.dumps(fits))
 out.append({'card':j['card'],'index':j['index'],'fits':fits,'source':src.tolist(),'target':dst.tolist()})
(root/'outpaint_registration_v7.json').write_text(json.dumps(out,indent=2),encoding='utf8')

