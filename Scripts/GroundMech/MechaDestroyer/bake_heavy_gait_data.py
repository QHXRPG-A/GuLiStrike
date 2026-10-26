"""Offline key authoring from preserved vendor tracks, using Excel-generated tuning.

Only six locomotion clips and two new moving-turn derivatives are emitted. No UE
runtime, scene rendering, or gameplay pose probe is used by this authoring step.
"""
import hashlib
import json
import math
from pathlib import Path
import numpy as np

ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export'
CLIPS={'WalkForward':'Anim_Mecha_Walk_FWD','WalkBackward':'Anim_Mecha_WalK_BWD',
       'RunForward':'Anim_Run_FWD','RunBackward':'Anim_Mecha_Run_BWD',
       'StrafeLeft':'Anim_Mecha_Strafe_L','StrafeRight':'Anim_Mecha_Strafe_R'}
STRUCTURAL_GAITS={'WalkBackward','RunBackward'}
STRUCTURAL_FIELDS=('MinKneeAngle','MaxKneeAngle','MaxLegSwingDegrees','MaxKneeTwistDegrees','MinFootSeparation')

def matrix(t):
    x,y,z,w=np.array(t['q'])/np.linalg.norm(t['q'])
    m=np.eye(4)
    m[:3,:3]=[[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
              [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
              [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]
    m[:3,3]=t['t']; return m

def row(m):
    a=m[:3,:3]; trace=np.trace(a)
    if trace>0:
        s=math.sqrt(trace+1)*2
        q=[(a[2,1]-a[1,2])/s,(a[0,2]-a[2,0])/s,(a[1,0]-a[0,1])/s,s/4]
    else:
        i=int(np.argmax(np.diag(a))); j=(i+1)%3; k=(i+2)%3
        s=math.sqrt(max(0,1+a[i,i]-a[j,j]-a[k,k]))*2
        q=[0.,0.,0.,0.]; q[i]=s/4; q[j]=(a[i,j]+a[j,i])/s
        q[k]=(a[i,k]+a[k,i])/s; q[3]=(a[k,j]-a[j,k])/s
    q=np.array(q); q/=np.linalg.norm(q)
    return {'t':m[:3,3].tolist(),'q':q.tolist(),'s':[1,1,1]}

def unit(v): return v/max(np.linalg.norm(v),1.e-12)

def between(a,b):
    a,b=unit(a),unit(b); c=np.cross(a,b); d=float(np.clip(a@b,-1,1))
    if d<-.999999:
        axis=unit(np.cross(a,np.eye(3)[int(np.argmin(abs(a)))]))
        return 2*np.outer(axis,axis)-np.eye(3)
    k=np.array([[0,-c[2],c[1]],[c[2],0,-c[0]],[-c[1],c[0],0]])
    return np.eye(3)+k+k@k/(1+d)

def make_signature(rows,ik):
    values={r['Name']:{k:r[k] for k in ('HorizontalMotionScale','SwingLiftScale','PelvisMotionScale','MaxStrideScale')}
            for r in rows if r['Name'] in CLIPS}
    h=hashlib.sha256(json.dumps({'version':3,'gait':values,'pelvis_down':ik['PelvisDown'],'pelvis_up':ik['PelvisUp'],
        'structure':{k:ik[k] for k in STRUCTURAL_FIELDS}},sort_keys=True).encode())
    for name in ('source_animation_data.json','ue_mesh_import.json','retargeted_animation_data.json'):
        h.update((OUT/name).read_bytes())
    return h.hexdigest()

def reach_at(l1,l2,degrees):
    return math.sqrt(l1*l1+l2*l2-2*l1*l2*math.cos(math.radians(degrees)))

def pelvis_for_structure(targets,hips,rest_axes,lengths,ik):
    """Find a shared pelvis height inside both rigid-leg shells and swing cones."""
    low,high=-ik['PelvisDown'],ik['PelvisUp']
    for side in ('L','R'):
        d=targets[side]-hips[side]; l1,l2=lengths[side]
        minimum=reach_at(l1,l2,ik['MinKneeAngle'])+.01
        maximum=reach_at(l1,l2,ik['MaxKneeAngle'])-.01
        horizontal=float(d[:2]@d[:2])
        if horizontal>=maximum*maximum:return None
        # Keep the ankle below its hip; the inner shell prevents knee overfolding.
        low=max(low,d[2]+math.sqrt(max(0,minimum*minimum-horizontal)))
        high=min(high,d[2]+math.sqrt(maximum*maximum-horizontal))
        if (1 if side=='L' else -1)*targets[side][0]<ik['MinFootSeparation']*.5:return None
    if low>high:return None
    cosine=math.cos(math.radians(ik['MaxLegSwingDegrees']))
    def feasible(offset):
        return all(unit(targets[s]-hips[s]-np.array([0.,0.,offset]))@rest_axes[s]>=cosine-1.e-8 for s in ('L','R'))
    nearest=float(np.clip(0,low,high))
    if feasible(nearest):return nearest
    candidates=[float(v) for v in np.linspace(low,high,129) if feasible(v)]
    if not candidates:return None
    good=min(candidates,key=abs); bad=nearest
    for _ in range(28):
        mid=(good+bad)*.5
        if feasible(mid):good=mid
        else:bad=mid
    return good

def bake():
    src=json.loads((OUT/'source_animation_data.json').read_text(encoding='utf8'))
    dst=json.loads((OUT/'ue_mesh_import.json').read_text(encoding='utf8'))
    old=json.loads((OUT/'retargeted_animation_data.json').read_text(encoding='utf8'))
    previous_path=OUT/'heavy_gait_animation_data.json'
    previous_data=json.loads(previous_path.read_text(encoding='utf8')) if previous_path.exists() else {}
    rows=json.loads((ROOT/'Data/Json/DT_GuLiStrikeMech_Gait.json').read_text(encoding='utf8'))
    cfg={r['Name']:r for r in rows}
    ik=json.loads((ROOT/'Data/Json/DT_GuLiStrikeMech_GroundIK.json').read_text(encoding='utf8'))[0]
    names=[b['name'] for b in src['bones']]; parents={b['name']:b['parent'] for b in src['bones']}
    def globals_(local):
        result={}
        for n in names: result[n]=result.get(parents[n],np.eye(4))@local[n]
        return result
    rest={b['name']:matrix(b['local']) for b in dst['bones']}; rg=globals_(rest)
    base={n:matrix(t) for n,t in zip(names,src['clips']['Anim_Mecha_Idle']['frames'][0])}; bg=globals_(base)
    pelvis_ratio=rg['MiddleJoint'][2,3]/bg['MiddleJoint'][2,3]
    clips={}; reports={}; safe_rows={}
    for key,cname in CLIPS.items():
        g=cfg[key]; clip=src['clips'][cname]; frames=[]; baked=[]; max_error=0.; pelvis_offsets=[]
        structural=key in STRUCTURAL_GAITS
        rest_axes={s:unit(rg['Foot_'+s][:3,3]-rg['UpperLeg_'+s][:3,3]) for s in ('L','R')}
        lengths={s:(np.linalg.norm(rg['Leg_'+s][:3,3]-rg['UpperLeg_'+s][:3,3]),
                    np.linalg.norm(rg['Foot_'+s][:3,3]-rg['Leg_'+s][:3,3])) for s in ('L','R')}
        for frame in clip['frames']:
            sl={n:matrix(t) for n,t in zip(names,frame)}; sg=globals_(sl); ls={}
            for n in names:
                delta=np.linalg.inv(base[n])@sl[n]
                delta[:3,3]*=pelvis_ratio*(g['PelvisMotionScale'] if n=='MiddleJoint' else 1.)
                ls[n]=rest[n]@delta
            pose=globals_(ls); root=pose['root']; rootrot=root[:3,:3]; targets={}; legs={}
            low,high=-ik['PelvisDown'],ik['PelvisUp']
            for side in ('L','R'):
                upper,lower,foot=('UpperLeg_'+side,'Leg_'+side,'Foot_'+side)
                h0,k0,a0=(rg[n][:3,3] for n in (upper,lower,foot)); l1=np.linalg.norm(k0-h0); l2=np.linalg.norm(a0-k0)
                sh,sk,sa=(bg[n][:3,3] for n in (upper,lower,foot)); ratio=(l1+l2)/(np.linalg.norm(sk-sh)+np.linalg.norm(sa-sk))
                source_rel=np.linalg.inv(sg['root'])@sg[foot]; baseline_rel=np.linalg.inv(bg['root'])@bg[foot]
                delta=(source_rel[:3,3]-baseline_rel[:3,3])*ratio
                delta*=np.array([g['HorizontalMotionScale'],g['HorizontalMotionScale'],g['SwingLiftScale']])
                desired=(root@np.r_[a0+delta,1])[:3]; targets[side]=desired
                h=pose[upper][:3,3]; horizontal=np.linalg.norm((desired-h)[:2])
                assert horizontal<l1+l2-.01,(cname,'horizontal stride outside rigid reach')
                height=math.sqrt(max(0,(l1+l2-.01)**2-horizontal**2))
                low=max(low,desired[2]-h[2]-height); high=min(high,desired[2]-h[2]+height)
                legs[side]=(upper,lower,foot,h0,k0,a0,l1,l2,source_rel,baseline_rel)
            assert low<=high,(cname,'requested gait cannot preserve leg lengths within pelvis limits',low,high)
            offset=pelvis_for_structure(targets,{s:pose['UpperLeg_'+s][:3,3] for s in ('L','R')},
                rest_axes,lengths,ik) if structural else float(np.clip(0,low,high))
            assert offset is not None,(cname,'requested stride exceeds the mechanical joint constraints')
            pelvis_offsets.append(offset)
            # A minimal authoring correction, bounded by the same table as runtime IK.
            parent=pose.get(parents['MiddleJoint'],np.eye(4))
            ls['MiddleJoint'][:3,3]+=parent[:3,:3].T@np.array([0.,0.,offset]); pose=globals_(ls)
            for side in ('L','R'):
                upper,lower,foot,h0,k0,a0,l1,l2,source_rel,baseline_rel=legs[side]
                h=pose[upper][:3,3].copy(); ankle=targets[side]; d=ankle-h; distance=np.linalg.norm(d)
                assert abs(l1-l2)+.001<distance<=l1+l2+.001,(cname,side,distance)
                axis=unit(d); along=(l1*l1-l2*l2+distance*distance)/(2*distance)
                initial=unit(a0-h0); bend=k0-h0-initial*((k0-h0)@initial)
                bend=rootrot@bend; bend=unit(bend-axis*(bend@axis))
                knee=h+axis*along+bend*math.sqrt(max(0,l1*l1-along*along))
                hinge=between(rootrot@unit(np.cross(k0-h0,a0-k0)),unit(np.cross(knee-h,ankle-knee))) if structural else np.eye(3)
                for n,oldvec,newvec,p in ((upper,k0-h0,knee-h,h),(lower,a0-k0,ankle-knee,knee)):
                    m=np.eye(4); m[:3,:3]=between(hinge@rootrot@oldvec,newvec)@hinge@rootrot@rg[n][:3,:3]; m[:3,3]=p; pose[n]=m
                m=np.eye(4); m[:3,:3]=rootrot@source_rel[:3,:3]@baseline_rel[:3,:3].T@rg[foot][:3,:3]; m[:3,3]=ankle
                pose[foot]=m; pose['FootEnd_'+side]=m@ls['FootEnd_'+side]
                pose['IK_Foot_'+side]=root@rg['IK_Foot_'+side]; pose['IK_Foot_'+side][:3,3]=ankle
                max_error=max(max_error,abs(np.linalg.norm(knee-h)-l1),abs(np.linalg.norm(ankle-knee)-l2))
            baked.append(pose)
            keys=[row(np.linalg.inv(pose.get(parents[n],np.eye(4)))@pose[n]) for n in names]
            if frames:
                for previous_key,current_key in zip(frames[-1],keys):
                    if np.dot(previous_key['q'],current_key['q'])<0:current_key['q']=[-v for v in current_key['q']]
            frames.append(keys)
        direction=np.array([1.,0,0]) if key=='StrafeLeft' else np.array([-1.,0,0]) if key=='StrafeRight' else np.array([0.,-1 if key.endswith('Backward') else 1,0])
        def reachable(scale):
            for pose in baked:
                if structural:
                    targets={s:pose['Foot_'+s][:3,3]+direction*((pose['Foot_'+s][:3,3]-rg['Foot_'+s][:3,3])@direction)*(scale-1) for s in ('L','R')}
                    if pelvis_for_structure(targets,{s:pose['UpperLeg_'+s][:3,3] for s in ('L','R')},rest_axes,lengths,ik) is None:return False
                    continue
                low,high=-ik['PelvisDown'],ik['PelvisUp']
                for side in ('L','R'):
                    h,k,a=(pose[n+'_'+side][:3,3] for n in ('UpperLeg','Leg','Foot'))
                    target=a+direction*((a-rg['Foot_'+side][:3,3])@direction)*(scale-1)
                    length=np.linalg.norm(k-h)+np.linalg.norm(a-k)-.01
                    sq=length*length-np.linalg.norm((target-h)[:2])**2
                    if sq<0:return False
                    z=math.sqrt(sq); low=max(low,target[2]-h[2]-z); high=min(high,target[2]-h[2]+z)
                if low>high:return False
            return True
        low,high=1.,g['MaxStrideScale']
        assert reachable(low),cname
        for _ in range(28):
            middle=(low+high)/2
            if reachable(middle):low=middle
            else:high=middle
        safe_rows[key]={'SafeMaxStrideScale':float(low)}
        clips[cname]={'source':clip['path'],'seconds':clip['seconds'],'frames':frames,'row':key}
        reports[key]={'horizontal_scale':g['HorizontalMotionScale'],'swing_lift_scale':g['SwingLiftScale'],
                      'pelvis_scale':g['PelvisMotionScale'],'safe_max_stride':low,'max_leg_length_error_cm':max_error,
                      'max_authoring_pelvis_correction_cm':max(abs(x) for x in pelvis_offsets),'frames':len(frames),
                      'structural_constraints_applied':structural}
    idle=old['clips']['Anim_Mecha_Idle']['frames'][0]
    for side in ('L','R'):
        source=old['clips']['Anim_Mecha_Turn_'+side]; frames=[]
        for f in source['frames']:
            frames.append([v if n=='MiddleJoint' or n.startswith(('UpperLeg_','Leg_','Foot_','FootEnd_','IK_Foot_')) else idle[i]
                           for i,(n,v) in enumerate(zip(names,f))])
        clips['Anim_Mecha_MoveTurn_'+side]={'source':source['source'],'seconds':source['seconds'],'frames':frames,'additive':True}
    signature=make_signature(rows,ik)
    changed=[]
    for name,data in clips.items():
        old_clip=previous_data.get('clips',{}).get(name)
        # Preserve signatures/keys of untouched directions during the migration.
        payload={k:v for k,v in data.items() if k!='signature'}
        old_payload={k:v for k,v in (old_clip or {}).items() if k!='signature'}
        if old_clip and payload==old_payload:
            data['signature']=old_clip.get('signature',previous_data['signature'])
        else:
            data['signature']=hashlib.sha256(json.dumps({'clip':payload,'structure':{k:ik[k] for k in STRUCTURAL_FIELDS}
                if data.get('row') in STRUCTURAL_GAITS else {}},sort_keys=True).encode()).hexdigest()
            changed.append(name)
    result={'success':True,'signature':signature,'previous_signature':previous_data.get('signature'),'changed_clips':changed,'bones':names,'clips':clips}
    (OUT/'heavy_gait_animation_data.json').write_text(json.dumps(result),encoding='utf8')
    report={'success':True,'signature':result['signature'],'gait':safe_rows,'sole':{},'clips':reports,
            'scope':'Offline source-track authoring; six locomotion clips and two new lower-body turn derivatives only'}
    (OUT/'heavy_gait_authoring.json').write_text(json.dumps(report,indent=2),encoding='utf8')
    print(json.dumps(report,indent=2))
    return result,report

if __name__=='__main__':bake()
