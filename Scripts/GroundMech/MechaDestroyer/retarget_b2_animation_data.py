"""Bake source motions onto B2 proportions; keep rigid links and barrel axes."""
import json,math
from pathlib import Path
from mathutils import Vector,Quaternion,Matrix
OUT=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export')
src=json.loads((OUT/'source_animation_data.json').read_text());dst=json.loads((OUT/'ue_mesh_import.json').read_text())
names=[b['name'] for b in src['bones']];parents={b['name']:b['parent'] for b in src['bones']}
assert names==[b['name'] for b in dst['bones']]
def mat(t):return Matrix.LocRotScale(Vector(t['t']),Quaternion((t['q'][3],*t['q'][:3])).normalized(),Vector(t['s']))
def tr(m):
    t,q,s=m.decompose();q.normalize()
    return {'t':list(t),'q':[q.x,q.y,q.z,q.w],'s':[1,1,1]}
def globals_(ls):
    g={}
    for n in names:g[n]=g.get(parents[n],Matrix.Identity(4))@ls[n]
    return g
rest={b['name']:mat(b['local']) for b in dst['bones']};rg=globals_(rest)
base={n:mat(t) for n,t in zip(names,src['clips']['Anim_Mecha_Idle']['frames'][0])};bg=globals_(base)
rootbase=bg['root'];rootinv=rootbase.inverted()
pelvis_ratio=rg['MiddleJoint'].translation.z/bg['MiddleJoint'].translation.z
report={};clips={}
for cname,clip in src['clips'].items():
    result=[];clamps=[];span=0.;previous={};errors=[]
    motion_scale=1.0 if any(k in cname for k in ['Idle','Shoot','Rocket','Jump_Loop']) else .4 if 'Turn_' in cname else .55
    for fi,frame in enumerate(clip['frames']):
        sl={n:mat(t) for n,t in zip(names,frame)};sg=globals_(sl)
        ls={}
        for n in names:
            delta=base[n].inverted()@sl[n]
            dt,dq,ds=delta.decompose()
            assert max(abs(x-1) for x in ds)<.02,(cname,n,'source scale animation requires explicit support',list(ds))
            # Local bone X spin/recoil transfers directly to the new gun axis.
            dt*=pelvis_ratio*(motion_scale if n=='MiddleJoint' else 1.0)
            ls[n]=rest[n]@Matrix.LocRotScale(dt,dq,Vector((1,1,1)))
        pose=globals_(ls)
        root=pose['root'];rootrot=root.to_quaternion()
        for side in ['R','L']:
            upper,lower,foot,toe=['UpperLeg_'+side,'Leg_'+side,'Foot_'+side,'FootEnd_'+side]
            h0,k0,a0=[rg[n].translation for n in [upper,lower,foot]]
            l1=(k0-h0).length;l2=(a0-k0).length
            sh,sk,sa=[bg[n].translation for n in [upper,lower,foot]]
            stride=(l1+l2)/((sk-sh).length+(sa-sk).length)
            source_rel=sg['root'].inverted()@sg[foot]
            baseline_rel=rootinv@bg[foot]
            desired=root@(a0+(source_rel.translation-baseline_rel.translation)*stride*motion_scale)
            h=pose[upper].translation.copy();d=desired-h;distance=d.length
            limited=min(max(distance,abs(l1-l2)+.01),l1+l2-.01)
            if abs(limited-distance)>.01:clamps.append({'frame':fi,'side':side,'cm':abs(limited-distance)})
            direction=d.normalized();ankle=h+direction*limited
            along=(l1*l1-l2*l2+limited*limited)/(2*limited)
            initial=(a0-h0).normalized();bend=k0-h0-initial*((k0-h0).dot(initial))
            bend=rootrot@bend;bend-=direction*bend.dot(direction);bend.normalize()
            knee=h+direction*along+bend*math.sqrt(max(0,l1*l1-along*along))
            for n,oldvec,newvec,p in [(upper,k0-h0,knee-h,h),(lower,a0-k0,ankle-knee,knee)]:
                q=(rootrot@oldvec).rotation_difference(newvec)@rootrot
                m=(q.to_matrix()@rg[n].to_3x3()).to_4x4();m.translation=p;pose[n]=m
            delta_rot=source_rel.to_quaternion()@baseline_rel.to_quaternion().inverted()
            m=(rootrot.to_matrix()@delta_rot.to_matrix()@rg[foot].to_3x3()).to_4x4();m.translation=ankle;pose[foot]=m
            pose[toe]=m@ls[toe]
            ik='IK_Foot_'+side;pose[ik]=root@rg[ik];pose[ik].translation=ankle
            errors.append(abs((pose[lower].translation-pose[upper].translation).length-l1))
            errors.append(abs((pose[foot].translation-pose[lower].translation).length-l2))
        row=[]
        for n in names:
            local=pose.get(parents[n],Matrix.Identity(4)).inverted()@pose[n]
            t,q,s=local.decompose();q.normalize()
            if n in previous and previous[n].dot(q)<0:q.negate()
            previous[n]=q.copy()
            row.append({'t':list(t),'q':[q.x,q.y,q.z,q.w],'s':[1,1,1]})
            span=max(span,(t-rest[n].translation).length)
        result.append(row)
    clips[cname]={'source':clip['path'],'seconds':clip['seconds'],'frames':result}
    report[cname]={'keys':len(result),'seconds':clip['seconds'],'crouched_stride_factor':motion_scale,'max_translation_delta_cm':span,'clamped_ankle_samples':len(clamps),'max_clamp_cm':max([x['cm'] for x in clamps] or [0]),'rigid_leg_length_error_cm':max(errors)}
    assert max(errors)<.001
    assert not clamps,(cname,clamps)
(OUT/'retargeted_animation_data.json').write_text(json.dumps({'bones':names,'clips':clips}),encoding='utf8')
(OUT/'retarget_report.json').write_text(json.dumps({'success':True,'method':'Common source idle baseline, local rotation delta, proportional translation, analytic two-link legs and root-relative feet; originals unchanged','pelvis_ratio':pelvis_ratio,'clips':report},indent=2),encoding='utf8')
print(json.dumps(report,indent=2))
