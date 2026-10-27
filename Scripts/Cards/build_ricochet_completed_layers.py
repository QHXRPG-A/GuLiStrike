"""Assemble model-completed layers while retaining source coordinates and core colours.

Feature registration and colour-gated compositing are production operations,
not output visual acceptance or rendered-image tests.
"""
from pathlib import Path
from datetime import datetime
import hashlib
import json
import sys
import numpy as np
from PIL import Image,ImageDraw
from scipy import ndimage
from scipy.optimize import minimize

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/UI/GroundMechRogueCards/GroundMech_ChainRicochet/CardProduction_20261026_v1'
sys.path.insert(0,'D:/gen-image/tools/ricochet-source-extraction')
import cv2
cv2.setRNGSeed(42)
SIZE=1536
PAD=256
CORE=1024
NAMES=['01_Foreground','02_SpeedLines','03_PlayerMech','04_Scene','05_Ricochet_Enemies','06_FarBackground']
requests=json.loads((OUT/'completion-r3-requests.json').read_text(encoding='utf-8'))
owner=np.asarray(Image.open(OUT/'Masks/Ownership.png'))
source=np.asarray(Image.open(OUT/'Master.png').convert('RGB')).copy()
staging=OUT/'Layers_CompletedR3'
staging.mkdir(exist_ok=True)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def align_supplement(generated,edit):
    """Register only generated fill to unchanged input anchors; never transform source RGB."""
    sift=cv2.SIFT_create(nfeatures=3000)
    target_mask=(edit[:,:,3]>230).astype(np.uint8)*255
    target_mask=cv2.erode(target_mask,np.ones((5,5),np.uint8))
    generated_mask=(generated[:,:,3]>220).astype(np.uint8)*255
    target_keys,target_desc=sift.detectAndCompute(cv2.cvtColor(edit[:,:,:3],cv2.COLOR_RGB2GRAY),target_mask)
    generated_keys,generated_desc=sift.detectAndCompute(cv2.cvtColor(generated[:,:,:3],cv2.COLOR_RGB2GRAY),generated_mask)
    report={'operation':'generated-fill registration only','applied':False,'source_pixels_transformed':False}
    if target_desc is None or generated_desc is None:
        report['reason']='Insufficient isolated-layer features; shared canvas retained'
        return generated,report
    pairs=cv2.BFMatcher().knnMatch(generated_desc,target_desc,k=2)
    matches=[a for pair in pairs if len(pair)==2 for a,b in [pair] if a.distance<.72*b.distance]
    report['feature_matches']=len(matches)
    if len(matches)<8:
        if int((edit[:,:,3]>230).sum())<20000:
            # Sparse white strokes need geometry fitting rather than texture features.
            points=np.argwhere(edit[:,:,3]>230)[::6]
            distance=ndimage.distance_transform_edt(generated[:,:,3]<170)
            def objective(parameters):
                scale,dx,dy,angle=parameters
                cosine,sine=np.cos(angle),np.sin(angle)
                x=(points[:,1]-dx)/scale;y=(points[:,0]-dy)/scale
                gx=cosine*x+sine*y;gy=-sine*x+cosine*y
                values=ndimage.map_coordinates(distance,[gy,gx],order=1,mode='constant',cval=30.)
                return float(np.minimum(values,30.).mean()+.00008*((dx-PAD)**2+(dy-PAD)**2)+5*(scale-2/3)**2+3*abs(angle))
            fit=minimize(objective,[2/3,PAD,PAD,0.],method='Powell',bounds=[(.4,.95),(100,420),(100,420),(-.05,.05)],options={'maxiter':80,'xtol':.001,'ftol':.0005})
            scale,dx,dy,angle=fit.x
            matrix=np.array([[scale*np.cos(angle),-scale*np.sin(angle),dx],[scale*np.sin(angle),scale*np.cos(angle),dy]])
            output=cv2.warpAffine(generated,matrix,(SIZE,SIZE),flags=cv2.INTER_LINEAR,borderMode=cv2.BORDER_CONSTANT,borderValue=(0,0,0,0))
            report.update(applied=True,method='sparse stroke geometry fitting',matrix=matrix.tolist())
            return output,report
        report['reason']='Insufficient reliable anchors; shared canvas retained'
        return generated,report
    a=np.float32([generated_keys[m.queryIdx].pt for m in matches])
    b=np.float32([target_keys[m.trainIdx].pt for m in matches])
    matrix,inliers=cv2.estimateAffinePartial2D(a,b,method=cv2.RANSAC,ransacReprojThreshold=4,maxIters=3000,confidence=.995)
    if matrix is None:
        report['reason']='No stable similarity fit; shared canvas retained'
        return generated,report
    count=int(inliers.sum())
    scale=float(np.hypot(matrix[0,0],matrix[0,1]))
    angle=float(np.degrees(np.arctan2(matrix[1,0],matrix[0,0])))
    center=matrix@np.array([SIZE/2,SIZE/2,1.])
    displacement=float(np.linalg.norm(center-SIZE/2))
    report.update(inlier_anchors=count,estimated_scale=scale,estimated_angle_degrees=angle,estimated_center_displacement=displacement)
    report['reference_anchor_bounds']=[b[inliers.ravel()>0].min(axis=0).tolist(),b[inliers.ravel()>0].max(axis=0).tolist()]
    if count<8 or count/len(matches)<.10 or not .25<scale<2.0 or abs(angle)>7 or displacement>600:
        report['reason']='Fit exceeds permitted supplement correction; shared canvas retained'
        return generated,report
    output=cv2.warpAffine(generated,matrix,(SIZE,SIZE),flags=cv2.INTER_LINEAR,borderMode=cv2.BORDER_CONSTANT,borderValue=(0,0,0,0))
    report.update(applied=True,matrix=matrix.tolist())
    return output,report


def align_enemy_group(generated,edit):
    """Keep three independent painted enemy poses at the original unit locations."""
    colour=generated[:,:,:3].astype(float)
    red=(generated[:,:,3]>100)&(colour[:,:,0]>40)&(colour[:,:,0]<210)&(colour[:,:,1]<colour[:,:,0]*.62)&(colour[:,:,2]<colour[:,:,0]*.70)
    y,x=np.where(red)
    if len(x)<90:
        return align_supplement(generated,edit)
    points=np.float32(np.column_stack([x,y])[::4])
    _,_,centers=cv2.kmeans(points,3,None,(cv2.TERM_CRITERIA_EPS+cv2.TERM_CRITERIA_MAX_ITER,80,.2),5,cv2.KMEANS_PP_CENTERS)
    centers=centers[np.argsort(centers[:,0])]
    yy,xx=np.mgrid[:SIZE,:SIZE]
    distances=np.stack([(xx-cx)**2+(yy-cy)**2 for cx,cy in centers])
    regions=np.argmin(distances,axis=0)
    boxes=[(475,205,670,410),(605,328,838,491),(810,390,1024,605)]
    sift=cv2.SIFT_create(nfeatures=3000)
    canvas=Image.new('RGBA',(SIZE,SIZE),(0,0,0,0))
    reports=[]
    for unit,(box,center) in enumerate(zip(boxes,centers)):
        x0,y0,x1,y1=box
        target_mask=np.zeros((SIZE,SIZE),dtype=np.uint8)
        target_mask[PAD+y0:PAD+y1,PAD+x0:PAD+x1]=edit[PAD+y0:PAD+y1,PAD+x0:PAD+x1,3]
        generation_mask=((regions==unit)&(generated[:,:,3]>220)).astype(np.uint8)*255
        tk,td=sift.detectAndCompute(cv2.cvtColor(edit[:,:,:3],cv2.COLOR_RGB2GRAY),target_mask)
        gk,gd=sift.detectAndCompute(cv2.cvtColor(generated[:,:,:3],cv2.COLOR_RGB2GRAY),generation_mask)
        matches=[]
        if gd is not None and td is not None:
            pairs=cv2.BFMatcher().knnMatch(gd,td,k=2)
            matches=[a for pair in pairs if len(pair)==2 for a,b in [pair] if a.distance<.76*b.distance]
        matrix=None
        inlier_count=0
        if len(matches)>=5:
            a=np.float32([gk[m.queryIdx].pt for m in matches]);b=np.float32([tk[m.trainIdx].pt for m in matches])
            candidate,inliers=cv2.estimateAffinePartial2D(a,b,method=cv2.RANSAC,ransacReprojThreshold=5,maxIters=3000)
            if candidate is not None:
                scale=np.hypot(candidate[0,0],candidate[0,1]);angle=np.degrees(np.arctan2(candidate[1,0],candidate[0,0]))
                inlier_count=int(inliers.sum())
                if inlier_count>=5 and .15<scale<1.5 and abs(angle)<12:
                    matrix=candidate
        method='unit feature registration'
        if matrix is None:
            # Colour components supply a deterministic position/size fallback.
            gy,gx=np.where(red&(regions==unit))
            target_rgb=source[y0:y1,x0:x1].astype(float)
            tr=(target_rgb[:,:,0]>40)&(target_rgb[:,:,0]<210)&(target_rgb[:,:,1]<target_rgb[:,:,0]*.62)&(target_rgb[:,:,2]<target_rgb[:,:,0]*.7)
            ty,tx=np.where(tr)
            assert len(tx)>10 and len(gx)>10,'Insufficient enemy red-armour anchors'
            gp=np.column_stack([gx,gy]);tp=np.column_stack([tx+PAD+x0,ty+PAD+y0])
            gc=np.median(gp,axis=0);tc=np.median(tp,axis=0)
            gs=np.linalg.norm(np.percentile(gp,90,axis=0)-np.percentile(gp,10,axis=0))
            ts=np.linalg.norm(np.percentile(tp,90,axis=0)-np.percentile(tp,10,axis=0))
            scale=float(np.clip(ts/max(gs,1),.15,1.5))
            shift=tc-scale*gc
            matrix=np.array([[scale,0,shift[0]],[0,scale,shift[1]]])
            method='unit red-armour spatial anchors'
        piece=generated.copy()
        piece[regions!=unit]=0
        # Isolate the local impact; the original continuous path is restored below.
        gold=(colour[:,:,0]>205)&(colour[:,:,1]>120)&(((colour[:,:,1]-colour[:,:,2])>35)|(colour.sum(axis=2)>725))
        gold_distance=ndimage.distance_transform_edt(gold&(regions==unit))
        local=distances[unit]<(max(150.,np.sqrt(np.median(distances[unit][red&(regions==unit)])))*1.6)**2
        peak=np.argmax(gold_distance*local)
        by,bx=np.unravel_index(peak,gold_distance.shape)
        nonlocal_beam=gold&(((xx-bx)**2+(yy-by)**2)>140**2)
        piece[nonlocal_beam]=0
        registered=cv2.warpAffine(piece,matrix,(SIZE,SIZE),flags=cv2.INTER_LINEAR,borderMode=cv2.BORDER_CONSTANT,borderValue=(0,0,0,0))
        canvas.alpha_composite(Image.fromarray(registered))
        reports.append({'unit':unit+1,'source_box':list(box),'method':method,'feature_matches':len(matches),'inlier_anchors':inlier_count,'matrix':matrix.tolist()})
    ray=Image.new('L',(CORE,CORE),0)
    ImageDraw.Draw(ray).line([(451,352),(578,280),(742,385),(941,474)],fill=255,width=24)
    r,g,b=[source[:,:,i].astype(np.int16) for i in range(3)]
    selected=(np.asarray(ray)>0)&(r>212)&(g>150)&(r>=g-2)&(((g-b)>32)|((r+g+b)>710))
    trail=np.zeros((SIZE,SIZE,4),dtype=np.uint8)
    trail_core=trail[PAD:PAD+CORE,PAD:PAD+CORE]
    trail_core[selected,:3]=source[selected];trail_core[selected,3]=255
    canvas.alpha_composite(Image.fromarray(trail))
    return np.asarray(canvas).copy(),{'operation':'per-unit generated-fill registration and original trajectory extraction','applied':True,'source_pixels_transformed':False,'unit_regions':reports,'trajectory_source_pixels':int(selected.sum()),'object_count_visual_review':'pending'}


normalized=[]
metadata=[]
for request in requests:
    raw=Path(request['raw_output'])
    with Image.open(raw) as image:
        raw_size=list(image.size)
        raw_mode=image.mode
        if request['transparent_background']:
            assert 'A' in image.getbands(),f"Layer {request['layer']} needs real alpha"
            assert image.getchannel('A').getextrema()[0]==0,f"Layer {request['layer']} is not transparent"
        generated=np.asarray(image.convert('RGBA').resize((SIZE,SIZE),Image.Resampling.LANCZOS)).copy()
    edit=np.asarray(Image.open(request['referenced_image_paths'][0]).convert('RGBA'))
    generated,registration=align_enemy_group(generated,edit) if request['layer']==5 else align_supplement(generated,edit)
    if request['layer']==6:
        missing=generated[:,:,3]==0
        if missing.any():
            # Registration can expose only the remote gutter; continue the generated plate there.
            original=np.asarray(Image.open(raw).convert('RGB').resize((SIZE,SIZE),Image.Resampling.LANCZOS))
            generated[missing,:3]=original[missing]
        generated[:,:,3]=255
    normalized.append(generated)
    metadata.append({'index':request['layer'],'name':request['name'],'raw':str(raw),'raw_size':raw_size,
                     'raw_mode':raw_mode,'raw_sha256':sha(raw),'prompt_sha256':request['prompt_sha256'],
                     'registration':registration})

yy,xx=np.mgrid[:CORE,:CORE]
source_rim=np.minimum.reduce([xx,yy,CORE-1-xx,CORE-1-yy])
for index,(request,base,record) in enumerate(zip(requests,normalized,metadata),1):
    rgba=base.copy()
    core=rgba[PAD:PAD+CORE,PAD:PAD+CORE]
    intended=owner==index
    radius=0 if index==2 else 1 if index in (3,5,6) else 2
    protected=ndimage.binary_erosion(intended,iterations=radius) if radius else intended.copy()
    colour_difference=np.max(np.abs(core[:,:,:3].astype(np.int16)-source.astype(np.int16)),axis=2)
    protected &= (core[:,:,3]>145) & (colour_difference<68)
    if index in (1,3,4):
        white=(source[:,:,0]>200)&(source[:,:,1]>195)&(source[:,:,2]>182)&((source[:,:,0].astype(int)-source[:,:,2].astype(int))<46)
        generated_streak=normalized[1][PAD:PAD+CORE,PAD:PAD+CORE,3]>32
        protected &= ~(white&generated_streak)
    weight=np.clip(ndimage.distance_transform_edt(protected)/3.,0,1)
    if index==5:
        r,g,b=[source[:,:,i].astype(float) for i in range(3)]
        red=(r>g*1.15)&(r>b*1.15)&(r<205)
        ivory=(r>160)&(g>150)&(b>135)&(r>=g*1.03)&((r-b)<100)
        dark=(np.max(source,axis=2)<110)&(np.abs(b-g)<35)&(core[:,:,3]>12)
        identity=ndimage.binary_erosion(intended&(red|ivory|dark),iterations=1)
        weight=np.maximum(weight,np.clip(ndimage.distance_transform_edt(identity)/2.,0,1))
    if index==1:
        # Preserve the true foreground contact with the card edge. No edge RGB extrusion.
        rim_weight=np.clip((36.-source_rim)/12.,0,1)*intended
        weight=np.maximum(weight,rim_weight)
    core[:,:,:3]=np.rint(core[:,:,:3].astype(float)*(1-weight[:,:,None])+source*weight[:,:,None]).astype(np.uint8)
    core[:,:,3]=np.maximum(core[:,:,3],np.rint(weight*255).astype(np.uint8))
    if index==6:
        rgba[:,:,3]=255
    rgba[rgba[:,:,3]==0,:3]=0
    output=staging/f'{NAMES[index-1]}.png'
    Image.fromarray(rgba).save(output)
    record.update(file=f'Layers/{output.name}',staged_file=str(output),canvas=[SIZE,SIZE],
                  source_window=[PAD,PAD,PAD+CORE,PAD+CORE],protected_original_core_pixels=int((weight>=.999).sum()),
                  source_pixels_resampled=False,alpha_extrema=[int(rgba[:,:,3].min()),int(rgba[:,:,3].max())],
                  transparent_pixels=int((rgba[:,:,3]==0).sum()),sha256=sha(output))
    assert record['alpha_extrema']==([255,255] if index==6 else [0,255])

# The completed foreground is one artwork asset. A binary occlusion mask routes
# its hidden surface behind the player and its visible particles in front.
front=np.full((SIZE,SIZE),255,dtype=np.uint8)
body=ndimage.binary_closing(owner==3,iterations=3)
front[PAD:PAD+CORE,PAD:PAD+CORE][body]=0
mask=OUT/'Masks/ForegroundFront_R3.png'
front_rgba=np.stack([front,front,front,front],axis=2)
Image.fromarray(front_rgba).save(mask)
record={'state':'model_completed_layers_staged','created_at':datetime.now().astimezone().isoformat(),
        'generator':'built-in image_gen; one completion request for each of six layers',
        'comfyui_used':False,'gameplay_modified':False,'canvas':[SIZE,SIZE],'source_canvas':[CORE,CORE],
        'padding_each_side':PAD,'source_window':[PAD,PAD,PAD+CORE,PAD+CORE],
        'source_window_uv':[1/6,1/6,5/6,5/6],'master_sha256':sha(OUT/'Master.png'),
        'layers':metadata,'foreground_front_mask':{'file':'Masks/ForegroundFront_R3.png','sha256':sha(mask),'canvas':[SIZE,SIZE],'front_value':255,'behind_player_value':0},
        'foreground_rendering':{'fixed_window_rim':.02,'motion_ramp_end':.12,'same_artwork_two_occlusion_passes':True},
        'composition_groups_back_to_front':[6,4,5,'1 behind player',3,'1 in front',2],
        'scope':'Original core colours retained where consistent; masks, hidden surfaces and exterior are model completed. Registration affects generated fill only.',
        'assistant_output_visual_test':False,'user_art_review':'pending','ue_imported':False}
(OUT/'completion-r3-build.json').write_text(json.dumps(record,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'state':record['state'],'layers':len(metadata),'canvas':[SIZE,SIZE],
                  'layers_with_fill_registration':sum(r['registration']['applied'] for r in metadata),
                  'original_core_pixels_preserved':{r['index']:r['protected_original_core_pixels'] for r in metadata},
                  'foreground_occlusion_mask':str(mask),'assistant_visual_test':False},ensure_ascii=False))
