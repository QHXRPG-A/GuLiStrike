"""Assemble generated complete layers and their generated outpaint.
No master pixels are copied; only uniform scale, translation, and RGBA assembly.
"""
from pathlib import Path
import sys,json,hashlib,shutil
sys.path.insert(0,'C:/Users/a/.codex/tmp/enhanced-explosive-card-runtime')
import cv2
import numpy as np
from PIL import Image
from skimage.metrics import structural_similarity
BASE=Path(__file__).resolve().parent
g=json.loads((BASE/'geometry.json').read_text(encoding='utf-8'))
r1=json.loads((BASE/'generation_results_r1.json').read_text(encoding='utf-8'))
reg=json.loads((BASE/'registration_r1.json').read_text(encoding='utf-8'))
r2=json.loads((BASE/'outpaint_results_r2.json').read_text(encoding='utf-8'))
W,H=g['canvas']
yy,xx=np.mgrid[:H,:W].astype(np.float32)
sift=cv2.SIFT_create(nfeatures=16000,contrastThreshold=.01,edgeThreshold=18)
bf=cv2.BFMatcher(cv2.NORM_L2)
reports=[]

def read(p): return np.asarray(Image.open(BASE/p).convert('RGBA'))
def premul(a):
    f=a.astype(np.float32)/255
    f[:,:,:3]*=f[:,:,3:4]
    return f
def rgba(a):
    rgb=np.divide(a[:,:,:3],a[:,:,3:4],out=np.zeros_like(a[:,:,:3]),where=a[:,:,3:4]>1e-5)
    return (np.clip(np.concatenate([rgb,a[:,:,3:4]],axis=2),0,1)*255).round().astype(np.uint8)
def fit(out,original,idx):
    ko,do=sift.detectAndCompute(cv2.cvtColor(out[:,:,:3],cv2.COLOR_RGB2GRAY),(out[:,:,3]>225).astype(np.uint8)*255)
    kt,dt=sift.detectAndCompute(cv2.cvtColor(original[:,:,:3],cv2.COLOR_RGB2GRAY),(original[:,:,3]>225).astype(np.uint8)*255)
    matches=[m for m,n in bf.knnMatch(do,dt,k=2) if m.distance<.8*n.distance]
    a=np.float32([ko[m.queryIdx].pt for m in matches]);b=np.float32([kt[m.trainIdx].pt for m in matches])
    rng=np.random.default_rng(100+idx);best=None
    for trial in range(3500):
        if len(a)<2: break
        i,j=rng.choice(len(a),2,replace=False)
        da=a[j]-a[i];db=b[j]-b[i]
        if np.dot(da,da)<900:continue
        s=float(np.dot(da,db)/np.dot(da,da))
        if not 1.1<s<2.2:continue
        off=b[i]-s*a[i]
        residual=np.linalg.norm(s*a+off-b,axis=1)
        keep=residual<7
        score=(int(keep.sum()),-float(np.median(residual[keep])))
        if best is None or score>best[0]:best=(score,s,off,keep)
    if best is None or best[0][0]<3:
        if idx!=2:raise RuntimeError(f'Outpaint registration failed: {idx}; matches={len(matches)}')
        def components(arr):
            n,labels,stats,cents=cv2.connectedComponentsWithStats((arr[:,:,3]>180).astype(np.uint8))
            points=[cents[i] for i in range(1,n) if stats[i,4]>60]
            return np.array(sorted(points,key=lambda p:p[1]))
        aa=components(out)[-5:];bb=components(original)[-5:]
        ac=aa-aa.mean(axis=0);bc=bb-bb.mean(axis=0)
        s=float(np.sum(ac*bc)/np.sum(ac*ac));off=bb.mean(axis=0)-s*aa.mean(axis=0)
        error=np.linalg.norm(s*aa+off-bb,axis=1)
        return np.array([[s,0,float(off[0])],[0,s,float(off[1])],[0,0,1]],np.float64),dict(method='rigid fit to five internal stroke alpha centroids; extended edge stroke excluded',samples=5,median_error_px=float(np.median(error)))
    keep=best[3]
    for _ in range(3):
        ac=a[keep]-a[keep].mean(axis=0);bc=b[keep]-b[keep].mean(axis=0)
        s=float(np.sum(ac*bc)/np.sum(ac*ac))
        off=b[keep].mean(axis=0)-s*a[keep].mean(axis=0)
        residual=np.linalg.norm(s*a+off-b,axis=1)
        nextkeep=residual<7
        if nextkeep.sum()<3:break
        keep=nextkeep
    return np.array([[s,0,float(off[0])],[0,s,float(off[1])],[0,0,1]],np.float64),dict(matches=len(matches),inliers=int(keep.sum()),median_error_px=float(np.median(residual[keep])))

for item in r1:
    idx=item['index']
    orig=read(item['raw_file'])
    rr=next(r for r in reg['registrations'] if r['index']==idx)
    M1=np.vstack([np.array(rr['matrix_raw_to_common']),[0,0,1]])
    core=read(rr['file'])
    ext=next((r for r in r2 if r['index']==idx),None)
    if ext:
        expanded=read(ext['file'])
        M2,quality=fit(expanded,orig,idx)
        M=M1@M2
        fill=cv2.warpAffine(premul(expanded),M[:2],(W,H),flags=cv2.INTER_LANCZOS4,borderMode=cv2.BORDER_CONSTANT)
        inv=np.linalg.inv(M1)
        ux=xx*inv[0,0]+inv[0,2];uy=yy*inv[1,1]+inv[1,2]
        # Feather the joint between two generated versions near the old rectangle.
        # All material and transparency come from those generated versions.
        dist=np.minimum.reduce([ux,uy,orig.shape[1]-1-ux,orig.shape[0]-1-uy])
        weight=np.clip((dist-1)/7,0,1)[:,:,None]
        assembled=premul(core)*weight+fill*(1-weight)
        result=rgba(assembled)
        source_info=dict(core=rr['file'],outpaint=ext['file'],outpaint_to_original=M2.tolist(),outpaint_to_common=M.tolist(),registration=quality,core_rectangle_feather_px=7,protected_original_master_pixels=0)
    else:
        result=core
        source_info=dict(core=rr['file'],outpaint=None,reason='Complete generated object already extends beyond the maximum visible sample region',protected_original_master_pixels=0)
    dest=BASE/f'Layers/L{idx}_{item["slug"]}.png'
    Image.fromarray(result).save(dest)
    ar=result[:,:,3]
    report=dict(index=idx,name=item['name'],file=dest.relative_to(BASE).as_posix(),sha256=hashlib.sha256(dest.read_bytes()).hexdigest(),canvas=[W,H],depth=item['depth'],motion_owner=item['motion_owner'],alpha_range=[int(ar.min()),int(ar.max())],transparent_fraction=float(np.mean(ar==0)),source=source_info)
    reports.append(report)
    print(json.dumps(dict(index=idx,alpha_range=report['alpha_range'],source=source_info)),flush=True)
# Draw all six actual saved files, with their unchanged alpha.
can=np.zeros((H,W,4),np.float32)
for idx in g['draw_order']:
    ar=premul(read(next(r['file'] for r in reports if r['index']==idx)))
    can=ar+can*(1-ar[:,:,3:4])
final=rgba(can)
Image.fromarray(final).save(BASE/'Checks/Composite_r2.png')
master=read('Inputs/FinalReference.png')[:,:,:3].astype(np.float32)/255
core=final[96:1280,96:1408,:3].astype(np.float32)/255
params=dict(data_range=1,channel_axis=2,gaussian_weights=True,sigma=1.5,win_size=11,use_sample_covariance=False)
metric=float(structural_similarity(master,core,**params))
cropped=float(structural_similarity(master[:,12:-12],core[:,12:-12],**params))
report=dict(version='r2',geometry=g,layers=reports,core_rgb_ssim=metric,visible_inner_pixel_ssim=cropped,threshold=.8,source_pixels_copied=False,metric_parameters=dict(data_range=1,channel_axis=2,gaussian_weights=True,sigma=1.5,win_size=11,use_sample_covariance=False),assistant_visual_validation=False,user_visual_acceptance='pending')
(BASE/'assembly_r2.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(ssim=metric,cropped_inner_ssim=cropped,threshold=.8)),flush=True)

