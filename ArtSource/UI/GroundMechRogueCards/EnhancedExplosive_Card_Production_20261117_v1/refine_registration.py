"""Final rigid registration refinement of generated surfaces; master is measurement only."""
from pathlib import Path
import sys,json,hashlib,shutil
sys.path.insert(0,'C:/Users/a/.codex/tmp/enhanced-explosive-card-runtime')
import cv2,numpy as np
from PIL import Image
from scipy.optimize import minimize
from skimage.metrics import structural_similarity
B=Path(__file__).resolve().parent
g=json.loads((B/'geometry.json').read_text(encoding='utf-8'))
prev=json.loads((B/'assembly_r2.json').read_text(encoding='utf-8'))
W,H=g['canvas'];order=g['draw_order']
def load(p):return np.array(Image.open(B/p).convert('RGBA'))
def pm(arr):
    f=arr.astype(np.float32)/255;f[:,:,:3]*=f[:,:,3:4];return f
def rgba(p):
    rgb=np.divide(p[:,:,:3],p[:,:,3:4],out=np.zeros_like(p[:,:,:3]),where=p[:,:,3:4]>1e-7)
    return np.clip(np.concatenate([rgb,p[:,:,3:4]],2)*255,0,255).round().astype(np.uint8)
stable=B/'Assembled_r2';stable.mkdir(exist_ok=True)
for l in prev['layers']:
    dst=stable/Path(l['file']).name
    if not dst.exists():shutil.copy2(B/l['file'],dst)
arrs={l['index']:load('Assembled_r2/'+Path(l['file']).name) for l in prev['layers']}
# Both generated sky inputs are opaque. Preserve that alpha invariant after resampling.
assert load('Generated/L6_sky_r1.png')[:,:,3].min()==255
assert load('Generated/L6_sky_outpaint_r2.png')[:,:,3].min()==255
arrs[6][:,:,3]=255
master=load('Inputs/FinalReference.png')
new=load('Generated/L5_mainland_fidelity_r3.png')
sift=cv2.SIFT_create(nfeatures=18000,contrastThreshold=.01,edgeThreshold=18)
ka,da=sift.detectAndCompute(cv2.cvtColor(new[:,:,:3],cv2.COLOR_RGB2GRAY),(new[:,:,3]>225).astype(np.uint8)*255)
kb,db=sift.detectAndCompute(cv2.cvtColor(master[:,:,:3],cv2.COLOR_RGB2GRAY),None)
matches=[m for m,n in cv2.BFMatcher().knnMatch(da,db,k=2) if m.distance<.78*n.distance]
a=np.float32([ka[m.queryIdx].pt for m in matches]);b=np.float32([kb[m.trainIdx].pt for m in matches])
M,keep=cv2.estimateAffinePartial2D(a,b,method=cv2.RANSAC,ransacReprojThreshold=5,maxIters=10000,confidence=.999)
keep=keep.ravel().astype(bool)
for _ in range(3):
    ac=a[keep]-a[keep].mean(0);bc=b[keep]-b[keep].mean(0)
    s=float(np.sum(ac*bc)/np.sum(ac*ac));off=b[keep].mean(0)-s*a[keep].mean(0)
    residual=np.linalg.norm(a*s+off-b,axis=1);keep=residual<6
M=np.array([[s,0,float(off[0])+96],[0,s,float(off[1])+96]],np.float64)
new_warp=cv2.warpAffine(pm(new),M,(W,H),flags=cv2.INTER_LANCZOS4,borderMode=cv2.BORDER_CONSTANT)
yy,xx=np.mgrid[:H,:W].astype(np.float32)
ux=(xx-M[0,2])/s;uy=(yy-M[1,2])/s
dist=np.minimum.reduce([ux,uy,new.shape[1]-1-ux,new.shape[0]-1-uy])
weight=np.clip((dist-1)/7,0,1)[:,:,None]
arrs[5]=rgba(new_warp*weight+pm(arrs[5])*(1-weight))
replacement=dict(file='Generated/L5_mainland_fidelity_r3.png',matrix_to_common=M.tolist(),matches=len(matches),inliers=int(keep.sum()),median_error_px=float(np.median(residual[keep])))
print(json.dumps(dict(land_replacement=replacement)),flush=True)
bases={i:pm(ar) for i,ar in arrs.items()}
small={i:cv2.resize(ar,(W//2,H//2),interpolation=cv2.INTER_AREA) for i,ar in bases.items()}
target=cv2.resize(master[:,:,:3].astype(np.float32)/255,(656,592),interpolation=cv2.INTER_AREA)
mu=cv2.GaussianBlur(target,(11,11),1.5);var=cv2.GaussianBlur(target*target,(11,11),1.5)-mu*mu
c1=.01**2;c2=.03**2
params={i:np.array([1.,0.,0.]) for i in order}
def matrix(p,factor=1.):
    s,dx,dy=p
    return np.array([[s,0,(1-s)*W/2*factor+dx*factor],[0,s,(1-s)*H/2*factor+dy*factor]],np.float32)
def transformed(i,p):
    return cv2.warpAffine(small[i],matrix(p,.5),(W//2,H//2),flags=cv2.INTER_LINEAR,borderMode=cv2.BORDER_CONSTANT)
def compose(images):
    first=images[order[0]].copy()
    for i in order[1:]:
        ar=images[i];first=ar+first*(1-ar[:,:,3:4])
    return first
cache={i:small[i] for i in order}
def score(images):
    out=compose(images)[48:640,48:704,:3]
    mb=cv2.GaussianBlur(out,(11,11),1.5)
    vb=cv2.GaussianBlur(out*out,(11,11),1.5)-mb*mb
    cov=cv2.GaussianBlur(out*target,(11,11),1.5)-mb*mu
    ss=((2*mb*mu+c1)*(2*cov+c2))/((mb*mb+mu*mu+c1)*(vb+var+c2))
    return float(ss[5:-5,5:-5].mean())
initial=score(cache)
for i in [5,3,4,1,2]:
    start=score(cache)
    def objective(p):
        images=dict(cache);images[i]=transformed(i,p)
        return -score(images)
    fit=minimize(objective,[1,0,0],method='Powell',bounds=[(.994,1.006),(-4,4),(-4,4)],options={'maxfev':160,'xtol':.0004,'ftol':.000005})
    if -fit.fun>start:
        params[i]=fit.x;cache[i]=transformed(i,fit.x)
    print(json.dumps(dict(layer=i,delta=params[i].tolist(),half_resolution_score=score(cache),before=start)),flush=True)
final_arrays={}
outlayers=[]
for l in prev['layers']:
    idx=l['index'];p=params[idx]
    mat=matrix(p)
    final=rgba(cv2.warpAffine(bases[idx],mat,(W,H),flags=cv2.INTER_LANCZOS4,borderMode=cv2.BORDER_CONSTANT)) if idx!=6 else arrs[6]
    final_arrays[idx]=pm(final)
    dest=B/l['file'];Image.fromarray(final).save(dest)
    outlayers.append(dict(l,sha256=hashlib.sha256(dest.read_bytes()).hexdigest(),alpha_range=[int(final[:,:,3].min()),int(final[:,:,3].max())],static_refinement_matrix=mat.tolist(),source_revision='r3' if idx==5 else 'r2'))
render=rgba(compose(final_arrays))
Image.fromarray(render).save(B/'Checks/Composite_final.png')
core=render[96:1280,96:1408,:3].astype(np.float32)/255
kw=dict(data_range=1,channel_axis=2,gaussian_weights=True,sigma=1.5,win_size=11,use_sample_covariance=False)
ss=float(structural_similarity(master[:,:,:3].astype(np.float32)/255,core,**kw))
report=dict(version='r3',geometry=g,layers=outlayers,land_replacement=replacement,static_refinement={str(i):params[i].tolist() for i in params},core_rgb_ssim=ss,threshold=.8,passed=ss>=.8,metric_parameters=kw,reference='Inputs/FinalReference.png',reference_sha256=hashlib.sha256((B/'Inputs/FinalReference.png').read_bytes()).hexdigest(),source_pixels_copied=False,assistant_visual_validation=False,user_visual_acceptance='pending')
(B/'final_assembly.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(final_ssim=ss,passed=ss>=.8)),flush=True)

