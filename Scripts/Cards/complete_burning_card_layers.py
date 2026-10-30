"""Assemble complete painted layers; never paste source pixels or cut ownership.

Feature/geometry registration positions generated art on the common canvas.
It is production assembly, not visual testing or rendered-image comparison.
"""
from pathlib import Path
from datetime import datetime, timezone, timedelta
import hashlib
import json
import sys
import numpy as np
from PIL import Image

sys.path.insert(0, 'D:/gen-image/tools/ricochet-source-extraction')
import cv2

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/UI/GroundMechRogueCards/BurningCards_HTML_20261029_v1'
REPAIR=OUT/'Repairs/CompleteLayers_v3'
CANVAS,PAD=1536,256
cv2.setRNGSeed(29)


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def rgba(path):return np.asarray(Image.open(path).convert('RGBA')).copy()


def bbox(raw):
    yy,xx=np.nonzero(raw[:,:,3]>32)
    assert len(xx)
    return np.array([xx.min(),yy.min(),xx.max()+1,yy.max()+1],float)


def fit_box(raw,target):
    box=bbox(raw);target=np.asarray(target,float)+PAD
    scale=min((target[2]-target[0])/(box[2]-box[0]),(target[3]-target[1])/(box[3]-box[1]))
    shift=(target[:2]+target[2:])/2-scale*(box[:2]+box[2:])/2
    return np.column_stack([np.eye(2)*scale,shift])


def warp(raw,matrix):
    # Interpolate premultiplied color, then restore straight alpha for PNG.
    # Transparent black must not darken a newly painted silhouette.
    source=raw.astype(np.float32)/255
    source[:,:,:3]*=source[:,:,3:4]
    painted=cv2.warpAffine(source,matrix,(CANVAS,CANVAS),flags=cv2.INTER_LINEAR,
        borderMode=cv2.BORDER_CONSTANT,borderValue=(0,0,0,0))
    alpha=painted[:,:,3:4]
    painted[:,:,:3]=np.divide(painted[:,:,:3],alpha,out=np.zeros_like(painted[:,:,:3]),where=alpha>1e-6)
    return np.clip(np.round(painted*255),0,255).astype('uint8')


def feature_fit(raw,guide,target):
    matrix=fit_box(raw,target);report={'method':'complete-object bounds registration','feature_pairs':0}
    detector=cv2.SIFT_create(nfeatures=6500,contrastThreshold=.025)
    rmask=cv2.erode((raw[:,:,3]>225).astype('uint8')*255,np.ones((3,3),'uint8'))
    gmask=cv2.erode((guide[:,:,3]>225).astype('uint8')*255,np.ones((3,3),'uint8'))
    rk,rd=detector.detectAndCompute(cv2.cvtColor(raw[:,:,:3],cv2.COLOR_RGB2GRAY),rmask)
    gk,gd=detector.detectAndCompute(cv2.cvtColor(guide[:,:,:3],cv2.COLOR_RGB2GRAY),gmask)
    if rd is not None and gd is not None:
        pairs=[a for pair in cv2.BFMatcher().knnMatch(rd,gd,k=2) if len(pair)==2 for a,b in [pair] if a.distance<.70*b.distance]
        report['feature_pairs']=len(pairs)
        if len(pairs)>=8:
            a=np.float32([rk[p.queryIdx].pt for p in pairs]);b=np.float32([gk[p.trainIdx].pt for p in pairs])
            candidate,inliers=cv2.estimateAffinePartial2D(a,b,method=cv2.RANSAC,ransacReprojThreshold=3.5,maxIters=5000,confidence=.995)
            if candidate is not None:
                keep=inliers[:,0]>0;count=int(keep.sum());scale=float(np.hypot(candidate[0,0],candidate[1,0]))
                angle=float(np.degrees(np.arctan2(candidate[1,0],candidate[0,0])))
                span=np.ptp(b[keep],axis=0) if count else np.zeros(2)
                width,height=target[2]-target[0],target[3]-target[1]
                report.update(inlier_anchors=count,estimated_scale=scale,rotation_degrees=angle,anchor_span=span.tolist())
                if count>=8 and .25<scale<1.8 and abs(angle)<14 and span[0]>.20*width and span[1]>.20*height:
                    matrix=candidate;report['method']='whole-object source-feature registration'
    report['matrix']=matrix.tolist()
    return warp(raw,matrix),report


def similarity(a,b,ta,tb):
    a,b,ta,tb=map(lambda x:np.asarray(x,float),(a,b,ta,tb))
    v=b-a;tv=tb-ta
    angle=np.arctan2(tv[1],tv[0])-np.arctan2(v[1],v[0])
    scale=np.linalg.norm(tv)/np.linalg.norm(v)
    linear=scale*np.array([[np.cos(angle),-np.sin(angle)],[np.sin(angle),np.cos(angle)]])
    return np.column_stack([linear,ta-linear@a])


def bullet_fit(raw):
    rgb=raw[:,:,:3].astype(float);r,g,b=rgb[:,:,0],rgb[:,:,1],rgb[:,:,2]
    metal=(raw[:,:,3]>210)&(r>90)&(g>60)&(g>.60*r)&(b<.62*g)
    yy,xx=np.nonzero(metal);assert len(xx)>500,'Insufficient painted bullet-body anchors'
    points=np.column_stack([xx,yy]).astype(float);center=points.mean(axis=0)
    _,vectors=np.linalg.eigh(np.cov(points.T));axis=vectors[:,-1]
    if axis.sum()<0:axis=-axis
    projection=(points-center)@axis
    a=center+axis*np.percentile(projection,.5);b=center+axis*np.percentile(projection,99.8)
    matrix=similarity(a,b,np.array([510,469])+PAD,np.array([946,875])+PAD)
    return warp(raw,matrix),{'method':'painted metal longitudinal-axis registration','matrix':matrix.tolist(),
        'source_window_rear':[510,469],'source_window_tip':[946,875],
        'attached_flames_transformed_with_bullet':True}


def impact_fit(raw):
    rgb=raw[:,:,:3];white=(raw[:,:,3]>200)&(rgb[:,:,0]>240)&(rgb[:,:,1]>215)&(rgb[:,:,2]>145)
    distance=cv2.distanceTransform(white.astype('uint8'),cv2.DIST_L2,5)
    y,x=np.unravel_index(np.argmax(distance),distance.shape)
    box=bbox(raw);scale=330/max(box[2]-box[0],box[3]-box[1])
    origin=np.array([x,y],float);matrix=np.column_stack([np.eye(2)*scale,np.array([510,469])+PAD-scale*origin])
    return warp(raw,matrix),{'method':'complete impact flame with shared ballistic origin','matrix':matrix.tolist(),
        'source_window_origin':[510,469]}


def shooter_fit(raw,guide):
    rgb=raw[:,:,:3].astype(float);r,g,b=rgb[:,:,0],rgb[:,:,1],rgb[:,:,2]
    valid=raw[:,:,3]>180
    blue=valid&(b>70)&(b>1.12*r)&(b>.80*g)
    yy,xx=np.nonzero(blue)
    if len(xx)<100:return feature_fit(raw,guide,[40,110,550,505])
    hot=(valid&(r>190)&(g<205)&(b<135)).astype('uint8')*255
    lines=cv2.HoughLinesP(hot,1,np.pi/720,threshold=90,minLineLength=raw.shape[1]*.28,maxLineGap=45)
    choices=[]
    for line in ([] if lines is None else lines):
        x0,y0,x1,y1=map(float,np.asarray(line).reshape(-1));dx=x1-x0;dy=y1-y0
        if dx and .35<dy/dx<1.9:
            a=np.array([x0,y0]);c=np.array([x1,y1])
            if c[0]<a[0]:a,c=c,a
            choices.append((np.linalg.norm(c-a),a,c))
    if not choices:return feature_fit(raw,guide,[40,110,550,505])
    _,a,c=max(choices,key=lambda x:x[0]);direction=(c-a)/np.linalg.norm(c-a)
    hot_y,hot_x=np.nonzero(hot);projected=np.column_stack([hot_x,hot_y])@direction
    white=(valid&(r>240)&(g>220)&(b>170)).astype('uint8')
    wy,wx=np.mgrid[:raw.shape[0],:raw.shape[1]]
    white[(wx*direction[0]+wy*direction[1])>np.percentile(projected,12)]=0
    near=cv2.dilate((hot>0).astype('uint8'),np.ones((41,41),'uint8'))
    white&=near;distance=cv2.distanceTransform(white,cv2.DIST_L2,5)
    oy,ox=np.unravel_index(np.argmax(distance),distance.shape)
    origin=np.array([ox,oy],float) if distance[oy,ox]>0 else a
    scale=min(180/max(1,xx.max()-xx.min()),170/max(1,yy.max()-yy.min()))
    target_direction=np.array([946-195,875-176],float);target_direction/=np.linalg.norm(target_direction)
    matrix=similarity(origin,origin+direction*100,np.array([195,176])+PAD,
        np.array([195,176])+PAD+target_direction*scale*100)
    return warp(raw,matrix),{'method':'whole shooter and painted trajectory ray registration','matrix':matrix.tolist(),
        'source_window_muzzle':[195,176],'source_window_impact':[510,469],
        'source_window_tip':[946,875],'connected_trajectory_retained':True}


def pair_fit(raw,guide):
    count,labels,stats,centers=cv2.connectedComponentsWithStats((raw[:,:,3]>110).astype('uint8'))
    units=sorted(range(1,count),key=lambda n:stats[n,cv2.CC_STAT_AREA],reverse=True)[:2]
    assert len(units)==2,'Expected two independently painted distant-unit anchors'
    units.sort(key=lambda n:centers[n,0]);yy,xx=np.mgrid[:raw.shape[0],:raw.shape[1]]
    partition=np.argmin(np.stack([(xx-centers[n,0])**2+(yy-centers[n,1])**2 for n in units]),axis=0)
    assembled=Image.new('RGBA',(CANVAS,CANVAS));reports=[]
    for index,target in enumerate([(0,290,255,525),(810,465,1040,700)]):
        part=raw.copy();part[partition!=index]=0
        anchor=guide.copy();keep=np.zeros((CANVAS,CANVAS),bool)
        x0,y0,x1,y1=target;keep[PAD+y0:PAD+y1,max(0,PAD+x0):min(CANVAS,PAD+x1)]=True;anchor[~keep]=0
        painted,report=feature_fit(part,anchor,target)
        assembled.alpha_composite(Image.fromarray(painted));reports.append(report)
    return np.asarray(assembled).copy(),{'method':'two complete distant units independently registered','units':reports,
        'split':'spatial separation between units; no source ownership silhouette'}


def main():
    from build_burning_card_rigid_layers import main as build_current
    build_current()


if __name__=="__main__":main()
