"""Register a complete imagegen ring repair, update layer 4 and measure the stack.

Only rigid canvas assembly is performed. This does not synthesize or retouch
pixels, copy the frozen reference, inspect a result image, or approve visuals.
"""
from pathlib import Path
import hashlib
import json
import sys

sys.path.insert(0, 'C:/Users/a/.codex/tmp/enhanced-explosive-card-runtime')
import cv2
import numpy as np
from PIL import Image
from skimage.metrics import structural_similarity

B=Path(__file__).resolve().parent


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def read(name):return json.loads((B/name).read_text(encoding='utf-8'))
def write(name,obj):(B/name).write_text(json.dumps(obj,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')


def main():
    previous=read('Revisions/r4/manifest.json')
    assembly=read('Revisions/r4/final_assembly.json')
    request=read('shockwave_r5_request.json')
    g=assembly['geometry'];W,H=g['canvas']
    old_file=B/'Inputs/L4_ground_explosion_before_r5.png'
    assert sha(old_file)==previous['layers'][3]['sha256']
    unchanged=[]
    for layer in previous['layers']:
        if layer['index']!=4:
            assert sha(B/layer['file'])==layer['sha256']
            unchanged.append(dict(index=layer['index'],file=layer['file'],sha256=layer['sha256']))
    source='Generated/L4_shockwave_continuity_r5.png'
    im=Image.open(B/source)
    assert im.mode=='RGBA' and im.getchannel('A').getextrema()==(0,255)
    new=np.array(im);old=np.array(Image.open(old_file).convert('RGBA'))
    rw,rh=im.size
    # The unchanged, opaque fire/smoke structure registers the whole generated
    # layer. The intentionally repaired ring is not a shape-fitting target.
    sift=cv2.SIFT_create(nfeatures=16000,contrastThreshold=.012,edgeThreshold=16)
    new_mask=(new[:,:,3]>245).astype(np.uint8)*255
    old_mask=(old[:,:,3]>245).astype(np.uint8)*255
    new_mask[:int(rh*.39),:]=0;new_mask[int(rh*.84):,:]=0;new_mask[:,:int(rw*.28)]=0
    old_mask[:int(H*.39),:]=0;old_mask[int(H*.84):,:]=0;old_mask[:,:int(W*.28)]=0
    ka,da=sift.detectAndCompute(cv2.cvtColor(new[:,:,:3],cv2.COLOR_RGB2GRAY),new_mask)
    kb,db=sift.detectAndCompute(cv2.cvtColor(old[:,:,:3],cv2.COLOR_RGB2GRAY),old_mask)
    assert da is not None and db is not None
    matches=[m for m,n in cv2.BFMatcher().knnMatch(da,db,k=2) if m.distance<.75*n.distance]
    assert len(matches)>=30, 'Too few preserved fire/smoke features for registration'
    a=np.float32([ka[m.queryIdx].pt for m in matches]);b=np.float32([kb[m.trainIdx].pt for m in matches])
    _,selected=cv2.estimateAffinePartial2D(a,b,method=cv2.RANSAC,ransacReprojThreshold=8,maxIters=10000,confidence=.999)
    keep=selected.ravel().astype(bool)
    for _ in range(4):
        ac=a[keep]-a[keep].mean(0);bc=b[keep]-b[keep].mean(0)
        scale=float(np.sum(ac*bc)/np.sum(ac*ac));offset=b[keep].mean(0)-scale*a[keep].mean(0)
        residual=np.linalg.norm(a*scale+offset-b,axis=1);keep=residual<8
    error=float(np.median(residual[keep]))
    assert int(keep.sum())>=25 and error<6
    matrix=np.array([[scale,0,float(offset[0])],[0,scale,float(offset[1])]],np.float64)
    registration=dict(method='uniform scale and translation from unchanged opaque explosion features; no rotation, shear, local warp or ring shape fitting',matches=len(matches),inliers=int(keep.sum()),median_error_px=error,matrix_to_common=matrix.tolist(),source_feature_span=[a[keep].min(0).tolist(),a[keep].max(0).tolist()])
    write('Checks/shockwave_r5_registration.json',registration)
    premult=new.astype(np.float32)/255;premult[:,:,:3]*=premult[:,:,3:4]
    mapped=cv2.warpAffine(premult,matrix,(W,H),flags=cv2.INTER_LANCZOS4,borderMode=cv2.BORDER_CONSTANT)
    alpha=np.clip(mapped[:,:,3:4],0,1)
    rgb=np.divide(mapped[:,:,:3],mapped[:,:,3:4],out=np.zeros_like(mapped[:,:,:3]),where=mapped[:,:,3:4]>1e-7)
    final=np.clip(np.concatenate([rgb,alpha],2)*255,0,255).round().astype(np.uint8)
    target='Layers/L4_ground_explosion.png'
    Image.fromarray(final).save(B/target)
    generation=dict(revision='20261117-r5',tool='image_gen.imagegen',mode='builtin_edit',file=source,sha256=sha(B/source),native_size=[rw,rh],native_alpha_range=[int(new[:,:,3].min()),int(new[:,:,3].max())],prompt_file=request['prompt_file'],prompt_sha256=sha(B/request['prompt_file']),inputs=[dict(file=p,role=role,sha256=sha(Path(p))) for p,role in zip(request['referenced_image_paths'],request['input_roles'])],registration=registration,original_reference_pixels_copied=False,prior_generated_pixels_pasted=False,assistant_visual_test=False,user_visual_acceptance='pending')
    if (B/'shockwave_r5_tool_output.json').exists():generation['tool_output']=read('shockwave_r5_tool_output.json')
    write('shockwave_r5_source.json',generation)
    layer=assembly['layers'][3]
    layer.update(sha256=sha(B/target),canvas=[W,H],alpha_range=[int(final[:,:,3].min()),int(final[:,:,3].max())],transparent_fraction=float(np.mean(final[:,:,3]==0)),source_revision='r5',source=dict(core=source,outpaint=None,reason='Complete imagegen repair of the ground ring; preserved blast features register the layer',protected_original_master_pixels=0),static_refinement_matrix=matrix.tolist(),replacement_source=generation)
    assembly['static_refinement'].pop('4',None)
    assembly.update(version='r5',revision='20261117-r5',shockwave_replacement=generation,requested_revision='User requested repair of the far-left/back and front-right ground shockwave discontinuities. Prior r4 projectile correction is preserved.',previous_revision_record='Revisions/r4/final_assembly.json')
    assembly['generation_records_extra']+=['shockwave_r5_request.json','shockwave_r5_source.json']
    assembly['production_records_extra']+=['Checks/shockwave_r5_registration.json','Checks/shockwave_r5_validation.json']
    composite=Image.new('RGBA',(W,H),(0,0,0,0))
    by_index={l['index']:l for l in assembly['layers']}
    for i in g['draw_order']:
        component=Image.open(B/by_index[i]['file']).convert('RGBA')
        assert component.size==(W,H)
        composite=Image.alpha_composite(composite,component)
    composite.save(B/'Checks/Composite_final.png')
    reference=np.array(Image.open(B/'Inputs/FinalReference.png').convert('RGB'),np.float32)/255
    core=np.array(composite.crop(tuple(g['source_core'])).convert('RGB'),np.float32)/255
    score=float(structural_similarity(reference,core,**assembly['metric_parameters']))
    assembly.update(core_rgb_ssim=score,passed=score>=assembly['threshold'])
    write('final_assembly.json',assembly)
    write('Checks/shockwave_r5_validation.json',dict(revision='20261117-r5',source=source,source_size=[rw,rh],final_file=target,final_sha256=sha(B/target),final_size=[W,H],final_alpha_range=layer['alpha_range'],transparent_fraction=layer['transparent_fraction'],registration=registration,unchanged_layer_hashes=unchanged,full_original_window_rgb_ssim=score,threshold=assembly['threshold'],ssim_passed=assembly['passed'],geometry_and_motion_parameters_unchanged=True,assistant_visual_test=False,user_visual_acceptance='pending'))
    print(json.dumps(dict(size=[W,H],native_size=[rw,rh],registration=registration,ssim=score,unchanged_layers=len(unchanged))),flush=True)


if __name__=='__main__':main()
