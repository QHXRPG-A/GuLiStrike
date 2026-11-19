"""Compose generated outpaint around the unchanged v6 generated layers.
No original Master pixels, segmentation, color-keying or repeated edge pixels.
Only geometric registration of generated extensions and rectangular canvas assembly.
"""
import json,hashlib
import cv2,numpy as np
from PIL import Image
from pathlib import Path
ROOT=Path(__file__).resolve().parent
PAD=(32,48); SIZE=(1088,1632)
data=json.loads((ROOT/'card-data_v6.js').read_text(encoding='utf8').split('=',1)[1].strip().rstrip(';'))
jobs={(j['card'],j['index']):j for j in json.loads((ROOT/'generation_v7.json').read_text(encoding='utf8'))}
fits={(j['card'],j['index']):j['fits'] for j in json.loads((ROOT/'outpaint_registration_v7.json').read_text(encoding='utf8'))}
patches={j['id']:j for j in json.loads((ROOT/'generation_v7_patches_all.json').read_text(encoding='utf8'))}
patch_fits=json.loads((ROOT/'outpaint_patch_registration_v7.json').read_text())
yy,xx=np.indices((SIZE[1],SIZE[0]),dtype=np.float32)
# Registration was done on a 1152x1728 working canvas; trim excess exterior.
gx=xx+64-PAD[0]; gy=yy+96-PAD[1]
inside=(xx>=PAD[0])&(xx<PAD[0]+1024)&(yy>=PAD[1])&(yy<PAD[1]+1536)
def rgba(p):return np.asarray(Image.open(p).convert('RGBA'))
def matrix(card,i,region='all'):
 return np.array(fits[(card,i)][region]['M'],np.float32)
def sample(path,M,target_x=None,target_y=None):
 inv=cv2.invertAffineTransform(np.asarray(M,np.float32))
 tx=gx if target_x is None else target_x
 ty=gy if target_y is None else target_y
 mx=inv[0,0]*tx+inv[0,1]*ty+inv[0,2];my=inv[1,0]*tx+inv[1,1]*ty+inv[1,2]
 a=rgba(path).astype(np.float32)/255
 a[:,:,:3]*=a[:,:,3,None]
 w=cv2.remap(a,mx,my,cv2.INTER_LINEAR,borderMode=cv2.BORDER_CONSTANT)
 rgb=np.divide(w[:,:,:3],w[:,:,3,None],out=np.zeros_like(w[:,:,:3]),where=w[:,:,3,None]>0)
 return np.rint(np.dstack((rgb,w[:,:,3]))*255).clip(0,255).astype('uint8')
def patch(name,fitname):
 p=patches[name];M=np.array(patch_fits[name]['fits'][fitname]['M'],np.float32)
 M[:,2]+=[64+p['box'][0]-p['pad'][0],96+p['box'][1]-p['pad'][1]]
 return sample(p['target'],M),M.tolist()
def align_plume_crossing(out,side,target_knots,source_knots):
 # Geometric registration of generated plume widths at the join.
 # RGB and alpha are resampled together; no alpha mask or copied edge strip.
 dst_y=yy-PAD[1]
 src_y=np.interp(dst_y,target_knots,source_knots).astype('float32')+PAD[1]
 a=out.astype(np.float32)/255
 a[:,:,:3]*=a[:,:,3,None]
 w=cv2.remap(a,xx,src_y,cv2.INTER_LINEAR,borderMode=cv2.BORDER_CONSTANT)
 rgb=np.divide(w[:,:,:3],w[:,:,3,None],out=np.zeros_like(w[:,:,:3]),where=w[:,:,3,None]>0)
 warped=np.rint(np.dstack((rgb,w[:,:,3]))*255).clip(0,255).astype('uint8')
 mask=xx<PAD[0] if side=='left' else xx>=PAD[0]+1024
 out[mask]=warped[mask]
 return {'side':side,'target_y':target_knots,'source_y':source_knots,'method':'static geometric crossing registration on generated extension only'}
def edge_error(base,out):
 p,q=PAD
 b=base
 pairs={'left':(b[:,0],out[q:q+1536,p-1]),
 'right':(b[:,-1],out[q:q+1536,p+1024]),
 'top':(b[0,:],out[q-1,p:p+1024]),
 'bottom':(b[-1,:],out[q+1536,p:p+1024])}
 r={}
 for name,(a,c) in pairs.items():
  solid=a[:,3]>200
  if solid.any():r[name]={'solid_boundary_samples':int(solid.sum()),'immediate_extension_coverage':float((c[solid,3]>128).mean()),'alpha_mean_abs_delta':float(np.abs(c[:,3].astype(float)-a[:,3]).mean())}
 return r
reports={}
for card,c in data.items():
 reports[card]=[]
 for l in c['layers']:
  i=l['index'];base=rgba(ROOT/l['file']);key=(card,i)
  out=np.zeros((SIZE[1],SIZE[0],4),'uint8');sources=[];maps=[]
  if key in jobs:
   job=jobs[key]
   # These three whole-canvas candidates were superseded by local generated
   # continuations. They are no longer retained after session cleanup.
   if not (card=='NoSurvivors' and i in (2,3,6)):
    M=matrix(card,i)
    out=sample(job['target'],M);sources.append(job['target']);maps.append(M.tolist())
   if card=='NoSurvivors' and i==2:
    left,lm=patch('left_corner','affine')
    right,rm=patch('right_corner','affine')
    out=np.where((gx<576)[:,:,None],left,right)
    sources=[patches['left_corner']['target'],patches['right_corner']['target']];maps=[lm,rm]
   elif card=='NoSurvivors' and i==3:
    left,lm=patch('left_trails','bounded_affine');right,rm=patch('right_trail','bounded_similarity')
    out[:]=0
    out[gx<64]=left[gx<64];out[gx>=1088]=right[gx>=1088]
    sources=[patches['left_trails']['target'],patches['right_trail']['target']];maps=[lm,rm]
    maps.append(align_plume_crossing(out,'left',[-48,0,489,590,897,1158,1536,1584],[-48,0,494,572,902,1144,1536,1584]))
    maps.append(align_plume_crossing(out,'right',[-48,0,438,505,1536,1584],[-48,0,440,489,1536,1584]))
   elif card=='ExplosivePackage' and i==2:
    # Local affine registrations for the top beam and lower floor.
    top,tm=patch('top_arch','affine')
    # The top continuation preserves five distinct structural crossings 26 px
    # above the global fit. Register these crossings without alpha masking.
    top_target_x=[-32,0,177,362,377,400,414,447,458,502,802,1024,1056]
    top_source_x=[-32,0,181,355,366,408,418,456,464,504,776,1024,1056]
    aligned_x=np.interp(xx-PAD[0],top_target_x,top_source_x).astype('float32')+64
    top=sample(patches['top_arch']['target'],tm,aligned_x,gy-26)
    left=sample(job['target'],matrix(card,i,'left'))
    right=sample(job['target'],matrix(card,i,'right'))
    bottom=sample(job['target'],matrix(card,i,'bottom'))
    out[gx<64]=left[gx<64];out[gx>=1088]=right[gx>=1088]
    top_region=(gy<96)&(gx>=64)&(gx<1088)
    out[top_region]=top[top_region]
    out[gy>=1632]=bottom[gy>=1632]
    maps={name:matrix(card,i,name).tolist() for name in ['all','left','right','bottom']}
    maps['top_patch']=tm;sources.append(patches['top_arch']['target'])
    maps['top_crossing_registration']={'target_x':top_target_x,'source_x':top_source_x,
     'source_y_offset':-26,'method':'static RGBA geometry registration of generated top extension'}
   elif card=='ExplosivePackage' and i==5:
    left=sample(job['target'],matrix(card,i,'left'))
    right,rm=patch('right_buildings','affine')
    out[gx<64]=left[gx<64];out[gx>=1088]=right[gx>=1088]
    maps={name:matrix(card,i,name).tolist() for name in ['all','left']}
    maps['right_patch']=rm;sources.append(patches['right_buildings']['target'])
   elif card=='NoSurvivors' and i==6:
    out,sm=patch('wide_sky','affine')
    maps=[sm];sources=[patches['wide_sky']['target']]
  out[PAD[1]:PAD[1]+1536,PAD[0]:PAD[0]+1024]=base
  target=ROOT/card/'Generation_v7'/'Layers'/Path(l['file']).name
  Image.fromarray(out).save(target)
  saved=rgba(target)
  assert np.array_equal(saved[PAD[1]:PAD[1]+1536,PAD[0]:PAD[0]+1024],base)
  report={'index':i,'file':target.relative_to(ROOT).as_posix(),'size':list(SIZE),
    'base':l['file'],'base_core_pixel_identical':True,
    'sha256':hashlib.sha256(target.read_bytes()).hexdigest(),
    'sources':sources,'registration':maps,
    'alpha_min':int(out[:,:,3].min()),'alpha_max':int(out[:,:,3].max()),
    'edge_continuity_numeric':edge_error(base,out),
    'transparent_pixels':int((out[:,:,3]==0).sum())}
  reports[card].append(report)
  print(card,i,json.dumps({k:report[k] for k in ['alpha_min','edge_continuity_numeric']},ensure_ascii=True))
(ROOT/'assembled_outpaint_v7.json').write_text(json.dumps({'canvas':list(SIZE),'window':[PAD[0],PAD[1],PAD[0]+1024,PAD[1]+1536],'cards':reports},ensure_ascii=False,indent=2),encoding='utf8')

