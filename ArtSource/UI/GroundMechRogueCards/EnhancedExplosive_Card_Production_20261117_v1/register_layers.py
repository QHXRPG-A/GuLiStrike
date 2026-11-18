"""Register generated complete layers using rigid uniform scale/translation only.
Reference pixels are used only for numerical correspondences and SSIM, never copied.
"""
from pathlib import Path
import sys, json, hashlib
sys.path.insert(0, 'C:/Users/a/.codex/tmp/enhanced-explosive-card-runtime')
import numpy as np
import cv2
from PIL import Image
from skimage.metrics import structural_similarity
from scipy.optimize import differential_evolution

BASE=Path(__file__).resolve().parent
G=json.loads((BASE/'geometry.json').read_text(encoding='utf-8'))
OUT=BASE/'Registered'
OUT.mkdir(exist_ok=True)
ref=np.asarray(Image.open(BASE/'Inputs/FinalReference.png').convert('RGBA'))
sift=cv2.SIFT_create(nfeatures=16000,contrastThreshold=0.015,edgeThreshold=15)
gray=cv2.cvtColor(ref[:,:,:3],cv2.COLOR_RGB2GRAY)
rk,rd=sift.detectAndCompute(gray,None)
matcher=cv2.BFMatcher(cv2.NORM_L2)
records=json.loads((BASE/'generation_results_r1.json').read_text(encoding='utf-8'))
reports=[]
registered={}

def warp_rgba(arr,M,size):
    a=arr[:,:,3:4].astype(np.float32)/255
    premul=np.concatenate([arr[:,:,:3].astype(np.float32)*a,a*255],axis=2)
    warped=cv2.warpAffine(premul,M,size,flags=cv2.INTER_LANCZOS4,borderMode=cv2.BORDER_CONSTANT,borderValue=(0,0,0,0))
    wa=np.clip(warped[:,:,3:4],0,255)
    rgb=np.divide(warped[:,:,:3]*255,wa,out=np.zeros_like(warped[:,:,:3]),where=wa>0.001)
    return np.clip(np.concatenate([rgb,wa],axis=2),0,255).round().astype(np.uint8)

for rec in records:
    idx=rec['index']
    arr=np.asarray(Image.open(BASE/rec['raw_file']).convert('RGBA'))
    mask=(arr[:,:,3]>240).astype(np.uint8)*255
    k,d=sift.detectAndCompute(cv2.cvtColor(arr[:,:,:3],cv2.COLOR_RGB2GRAY),mask)
    pairs=matcher.knnMatch(d,rd,k=2)
    good=[m for m,n in pairs if m.distance<0.74*n.distance]
    a=np.float32([k[m.queryIdx].pt for m in good])
    b=np.float32([rk[m.trainIdx].pt for m in good])
    plausible=np.max(np.abs(a-b),axis=1)<150
    a=a[plausible];b=b[plausible]
    rng=np.random.default_rng(23+idx)
    best=None
    for trial in range(2500):
        if len(a)<2: break
        i,j=rng.choice(len(a),2,replace=False)
        da=a[j]-a[i]; db=b[j]-b[i]
        if np.dot(da,da)<400: continue
        s=float(np.dot(da,db)/np.dot(da,da))
        if not .85<s<1.2: continue
        off=b[i]-s*a[i]
        distances=np.linalg.norm(s*a+off-b,axis=1)
        good_mask=distances<6
        score=(int(good_mask.sum()),-float(np.median(distances[good_mask])))
        if best is None or score>best[0]: best=(score,s,off,good_mask)
    feature_fit=best is not None and best[0][0]>=3
    if feature_fit:
        keep=best[3]
        aa=a[keep]; bb=b[keep]
        ac=aa-aa.mean(axis=0); bc=bb-bb.mean(axis=0)
        scale=float(np.sum(ac*bc)/np.sum(ac*ac))
        offset=bb.mean(axis=0)-scale*aa.mean(axis=0)
    else:
        yy,xx=np.where(arr[:,:,3]>245)
        choose=rng.choice(len(xx),min(7000,len(xx)),replace=False)
        xx=xx[choose].astype(np.float32);yy=yy[choose].astype(np.float32)
        colors=arr[yy.astype(int),xx.astype(int),:3].astype(np.float32)/255
        reference_rgb=ref[:,:,:3].astype(np.float32)/255
        def loss(params):
            s,tx,ty=params
            px=(xx*s+tx).astype(np.float32);py=(yy*s+ty).astype(np.float32)
            sample=cv2.remap(reference_rgb,px.reshape(-1,1),py.reshape(-1,1),cv2.INTER_LINEAR,borderMode=cv2.BORDER_CONSTANT)[:,0,:]
            diff=np.mean((sample-colors)**2,axis=1)
            invalid=(px<0)|(px>1311)|(py<0)|(py>1183)
            diff[invalid]=.12
            return float(np.mean(np.minimum(diff,.12)))
        fit=differential_evolution(loss,[(.95,1.05),(-40,40),(-45,45)],seed=29,popsize=12,maxiter=90,tol=.00005,polish=True)
        scale=float(fit.x[0]);offset=np.array(fit.x[1:])
        keep=np.zeros(len(a),dtype=bool)
    # Refit only physically rigid coordinates. Never shear or distort a layer.
    for _ in range(3):
        if not feature_fit: break
        residual=np.linalg.norm(a*scale+offset-b,axis=1)
        next_keep=residual<6
        if next_keep.sum()<3: break
        keep=next_keep
        aa=a[keep];bb=b[keep]
        ac=aa-aa.mean(axis=0);bc=bb-bb.mean(axis=0)
        scale=float(np.sum(ac*bc)/np.sum(ac*ac))
        offset=bb.mean(axis=0)-scale*aa.mean(axis=0)
    M=np.array([[scale,0,float(offset[0])+96],[0,scale,float(offset[1])+96]],dtype=np.float64)
    result=warp_rgba(arr,M,tuple(G['canvas']))
    registered[idx]=result
    file=f'Registered/L{idx}_{rec["slug"]}.png'
    Image.fromarray(result).save(BASE/file)
    residual=np.linalg.norm(a[keep]*scale+offset-b[keep],axis=1)
    report=dict(index=idx,file=file,raw_file=rec['raw_file'],method='rigid_feature_fit' if feature_fit else 'rigid_alpha_weighted_photometric_fit',scale=scale,offset_to_reference=offset.tolist(),matrix_raw_to_common=M.tolist(),feature_matches=len(good),inliers=int(keep.sum()),median_error_px=float(np.median(residual)) if len(residual) else None,p95_error_px=float(np.quantile(residual,.95)) if len(residual) else None,source_bounds_in_common=[float(M[0,2]),float(M[1,2]),float(M[0,2]+arr.shape[1]*scale),float(M[1,2]+arr.shape[0]*scale)],sha256=hashlib.sha256((BASE/file).read_bytes()).hexdigest())
    reports.append(report)
    print(json.dumps(report),flush=True)
# Only generated RGBA content is composited.
canvas=np.zeros((G['canvas'][1],G['canvas'][0],4),np.float32)
for idx in G['draw_order']:
    arr=registered[idx].astype(np.float32)/255
    a=arr[:,:,3:4]
    ca=canvas[:,:,3:4]
    outa=a+ca*(1-a)
    rgb=np.divide(arr[:,:,:3]*a+canvas[:,:,:3]*ca*(1-a),outa,out=np.zeros_like(canvas[:,:,:3]),where=outa>0)
    canvas=np.concatenate([rgb,outa],axis=2)
im=(np.clip(canvas,0,1)*255).round().astype(np.uint8)
Image.fromarray(im).save(BASE/'Checks/Composite_registered_r1.png')
core=im[96:1280,96:1408,:3]
metric=float(structural_similarity(ref[:,:,:3].astype(np.float32)/255,core.astype(np.float32)/255,data_range=1,channel_axis=2,gaussian_weights=True,sigma=1.5,win_size=11,use_sample_covariance=False))
report=dict(registrations=reports,core_rgb_ssim=metric,threshold=.8,metric_window=[96,96,1408,1280],reference_sha256=hashlib.sha256((BASE/'Inputs/FinalReference.png').read_bytes()).hexdigest(),source_pixels_copied=False,alpha_modified=False,status='registered_core_before_required_outpaint')
(BASE/'registration_r1.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(core_ssim=metric,threshold=.8)),flush=True)

