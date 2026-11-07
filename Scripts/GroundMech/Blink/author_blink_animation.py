"""Create the editable A4B2 full-body blink once. Existing artist keys are preserved."""
import json
import math
from pathlib import Path

import unreal


FOLDER = '/Game/GuLiStrike/GroundMech/Animations/A4B2'
NAME = 'Anim_Mecha_Blink'
TRACK = 'Blink phases'
RATE = 150  # Multiple of UE's default 30 fps, with exact 20 ms phase boundaries.


def ease(t):
    t = min(1., max(0., t))
    return t * t * (3. - 2. * t)


def key_animation(pawn_mesh, idle, skill, skeleton_rows):
    # Read the actual idle pose from the same production skeleton the player uses.
    names = [b['name'] for b in skeleton_rows]
    parents = {b['name']: b['parent'] for b in skeleton_rows}
    idle_keys = unreal.AnimationLibrary.get_bone_poses_for_frame(idle, names, 0, False)
    local = dict(zip(names, idle_keys))
    base = {}
    for n in names:
        base[n] = local[n] * base[parents[n]] if parents[n] else local[n]
    lengths = {}
    drop = skill['BlinkCrouchDepthCm'] / pawn_mesh.get_relative_transform().scale3d.z
    for side in ('L', 'R'):
        hip, knee, ankle = [base[b + '_' + side].translation for b in ('UpperLeg', 'Leg', 'Foot')]
        thigh, shin = (knee - hip).length(), (ankle - knee).length()
        lengths[side] = (thigh, shin)
        reach2 = thigh**2 + shin**2 - 2 * thigh * shin * math.cos(math.radians(skill['BlinkKneeAngleDegrees']))
        drop = min(drop, max(0., hip.z - ankle.z - math.sqrt(max(0., reach2 - (hip - ankle).length2d_squared()))))
    windup, conceal, recovery = [skill['Blink' + phase + 'Seconds'] for phase in ('Windup', 'Conceal', 'Recovery')]
    duration = windup + conceal + recovery
    count = round(duration * RATE)
    pitch = lambda degrees: unreal.Quat(math.sin(math.radians(degrees) / 2), 0, 0, math.cos(math.radians(degrees) / 2))
    frames = []
    for f in range(count + 1):
        t = duration * f / count
        amount = ease(t / windup) if t < windup else 1. - ease((t - windup - conceal) / recovery)
        if amount <= 1.e-8:
            frames.append(local)
            continue
        pose = {}
        solved = {}
        down = unreal.Vector(0, 0, -drop * amount)
        for side in ('L', 'R'):
            upper, lower, foot = [b + '_' + side for b in ('UpperLeg', 'Leg', 'Foot')]
            h, k, a = [base[n].translation for n in (upper, lower, foot)]
            hip = h + down
            axis = (a - hip).normal()
            old_axis = (a - h).normal()
            pole = (k - h - old_axis * (k - h).dot(old_axis)).normal()
            pole = old_axis.find_quat_between_normals(axis).rotate_vector(pole)
            thigh, shin = lengths[side]
            reach = (a - hip).length()
            along = (thigh**2 - shin**2 + reach**2) / (2. * reach)
            knee = hip + axis * along + pole * math.sqrt(max(0., thigh**2 - along**2))
            for n, pos, before, after in ((upper, hip, k - h, knee - hip), (lower, knee, a - k, a - knee)):
                rotation = before.normal().find_quat_between_normals(after.normal()) * base[n].rotation
                solved[n] = unreal.Transform(location=pos)
                solved[n].rotation = rotation
            solved[foot] = base[foot]
        for n in names:
            current = local[n] * pose[parents[n]] if parents[n] else local[n].copy()
            if n == 'MiddleJoint':
                current.translation = current.translation + down
            elif n == 'cockpit':
                current.rotation = pitch(-skill['BlinkBodyLeanDegrees'] * amount) * current.rotation
            elif n.startswith('GunArm_'):
                current.rotation = pitch(-(skill['BlinkArmBackDegrees'] - 90.) * amount) * current.rotation
            # Gun_L/R only carry the spinning barrel inside the rigid GunArm
            # housing. Keep their authored local transform: pitching these bones
            # independently pulls the muzzle rings out of the weapon casing.
            pose[n] = solved.get(n, current)
        frames.append({n: pose[n].make_relative(pose[parents[n]]) if parents[n] else pose[n] for n in names})
    return names, frames, (windup, windup + conceal)


def ensure_sequence():
    lib = unreal.EditorAssetLibrary
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    root = Path(unreal.Paths.project_dir()).resolve()
    pawn = unreal.load_asset('/Game/GuLiStrike/GroundMech/BP_GroundMech_Light')
    pawn_mesh = unreal.get_default_object(pawn.generated_class()).get_editor_property('mesh')
    mesh = pawn_mesh.get_editor_property('skeletal_mesh_asset')
    assert mesh.get_name() == 'SK_GroundMech_A4B2'
    path = FOLDER + '/' + NAME
    anim = unreal.load_asset(path) if lib.does_asset_exist(path) else None
    if anim is None:
        populated = unreal.GuLiGroundMechAnimationEditorLibrary.notify_animation_keys_populated
        idle = unreal.load_asset(FOLDER + '/Anim_Mecha_Idle')
        assert mesh.skeleton == idle.get_skeleton()
        table = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Skills')
        skill = next(r for r in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table)) if r['Name'] == 'Blink')
        source = root / 'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/ue_mesh_import.json'
        names, frames, boundaries = key_animation(pawn_mesh, idle, skill, json.loads(source.read_text())['bones'])
        factory = unreal.AnimSequenceFactory()
        factory.set_editor_property('target_skeleton', mesh.skeleton)
        factory.set_editor_property('preview_skeletal_mesh', mesh)
        anim = unreal.AssetToolsHelpers.get_asset_tools().create_asset(NAME, FOLDER, unreal.AnimSequence, factory)
        ctl = anim.controller
        ctl.open_bracket('Author A4B2 blink', False)
        try:
            ctl.set_frame_rate(unreal.FrameRate(RATE, 1), False)
            ctl.set_number_of_frames(unreal.FrameNumber(len(frames) - 1), False)
            for n in names:
                ctl.add_bone_track(n, False)
                keys = [frame[n] for frame in frames]
                assert ctl.set_bone_track_keys(n, [v.translation for v in keys], [v.rotation for v in keys], [v.scale3d for v in keys], False), n
        finally:
            try:
                # The Populated notification copies the model rate to the platform compressor.
                assert populated(anim)
            finally:
                ctl.close_bracket(False)
        anim.set_editor_property('enable_root_motion', False)
        anim.set_editor_property('loop', False)
        unreal.AnimationLibrary.add_animation_notify_track(anim, TRACK)
        for marker, time in zip(('BlinkConceal', 'BlinkRecovery'), boundaries):
            unreal.AnimationLibrary.add_animation_sync_marker(anim, marker, time, TRACK)
        assert lib.save_loaded_asset(anim, False), path
    assert isinstance(anim, unreal.AnimSequence) and anim.get_skeleton() == mesh.skeleton
    markers = {str(m.marker_name): m.time for m in unreal.AnimationLibrary.get_animation_sync_markers(anim)}
    assert 0. < markers['BlinkConceal'] < markers['BlinkRecovery'] < anim.get_play_length()
    return {'asset': anim.get_path_name(), 'keys': anim.data_model_interface.get_number_of_keys(),
            'bones': len(anim.data_model_interface.get_bone_track_names()), 'duration': anim.get_play_length(), 'markers': markers}


def inverse(q):
    return unreal.Quat(-q.x,-q.y,-q.z,q.w)


def angle(a,b):
    return math.degrees(math.acos(max(-1.,min(1.,a.normal().dot(b.normal())))))


def axis_rotation(axis,degrees):
    s=math.sin(math.radians(degrees)*.5)
    return unreal.Quat(axis.x*s,axis.y*s,axis.z*s,math.cos(math.radians(degrees)*.5))


def components(local,names,parents):
    result={}
    for n in names: result[n]=local[n]*result[parents[n]] if parents[n] else local[n].copy()
    return result


def raw_frames(anim,names,fallback):
    tracks={str(n).lower() for n in anim.data_model_interface.get_bone_track_names()}
    options=unreal.AnimPoseEvaluationOptions()
    # Source explicitly omits Persona's additive bone curves. UE 5.7's sequencer
    # model no longer exposes its keys through the legacy raw-track getters.
    options.evaluation_type=unreal.AnimDataEvalType.SOURCE
    options.should_retarget=False
    options.evaluate_curves=False
    result=[]
    for f in range(82):
        pose=unreal.AnimPoseExtensions.get_anim_pose_at_frame(anim,f,options)
        result.append({n:unreal.AnimPoseExtensions.get_bone_pose(pose,n,unreal.AnimPoseSpaces.LOCAL)
                       if n.lower() in tracks else fallback[n].copy() for n in names})
    return result


def digest_animation(anim,names,fallback,only=None):
    import hashlib
    frames=raw_frames(anim,names,fallback)
    values=[[frame[n].export_text() for n in names if only is None or n.lower() in only] for frame in frames]
    model=unreal.load_object(None,anim.data_model_interface.get_path_name())
    curves=model.get_editor_property('legacy_curve_data').export_text()
    return {'raw':hashlib.sha256(json.dumps(values).encode()).hexdigest(),
            'curves':hashlib.sha256(curves.encode()).hexdigest()}


def envelope(f,start,peak,release,end,settle=0.):
    if f<peak: return ease((f-start)/max(1,peak-start))
    if f<=release: return 1.
    if f<end: return 1.-ease((f-release)/(end-release))
    if settle and f<81:
        midpoint=(end+81)*.5
        return -settle*(ease((f-end)/(midpoint-end)) if f<midpoint else 1.-ease((f-midpoint)/(81-midpoint)))
    return 0.


def directional_frames(anim,direction,names,parents,idle):
    source=raw_frames(anim,names,idle); rest=components(idle,names,parents)
    for frame in source:
        for n in ('cockpit','GunArm_L','GunArm_R','Gun_L','Gun_R'): frame[n]=idle[n].copy()
    up=unreal.Vector(0,0,1); lean_axis=up.cross(direction).normal()
    result=[]; metrics={'min_knee':180.,'max_knee':0.,'max_hip_swing':0.,'min_foot_spacing':1.e9,
                        'min_drag_scale':1.,'max_pelvis_travel':0.,'max_pelvis_lift':0.,'max_foot_trail':0.,'max_foot_lift':0.}
    for f in range(82):
        waist=envelope(f,0,8,36,54)
        chest=envelope(f,3,12,36,63,.10)
        arm=envelope(f,6,15,39,66,.12)
        leg=envelope(f,8,18,36,63)

        def solve(factor):
            offset=direction*(50.*waist*factor)+up*(40.*waist*factor)
            feet_offset=-direction*(60.*leg*factor)+up*(40.*leg*factor)
            pose={}; solved={}; diagnostics=[]
            for side in ('L','R'):
                hip_name,knee_name,foot_name=[b+'_'+side for b in ('UpperLeg','Leg','Foot')]
                h,k,a=[rest[n].translation for n in (hip_name,knee_name,foot_name)]
                hip=h+offset; ankle=a+feet_offset
                thigh,shin=(k-h).length(),(a-k).length()
                axis=(ankle-hip).normal(); old_axis=(a-h).normal()
                pole=(k-h-old_axis*(k-h).dot(old_axis)).normal()
                pole=old_axis.find_quat_between_normals(axis).rotate_vector(pole)
                reach=(ankle-hip).length()
                along=(thigh*thigh-shin*shin+reach*reach)/(2.*reach)
                height2=thigh*thigh-along*along
                if height2<0: return None,None
                knee=hip+axis*along+pole*math.sqrt(height2)
                bend=angle(hip-knee,ankle-knee); swing=angle(k-h,knee-hip)
                if not (80.2<=bend<=154.8 and swing<=44.8): return None,None
                for n,pos,before,after in ((hip_name,hip,k-h,knee-hip),(knee_name,knee,a-k,ankle-knee)):
                    t=rest[n].copy(); t.translation=pos
                    t.rotation=before.normal().find_quat_between_normals(after.normal())*rest[n].rotation
                    solved[n]=t
                foot=rest[foot_name].copy(); foot.translation=ankle; solved[foot_name]=foot
                diagnostics.append((bend,swing))
            for n in names:
                current=idle[n]*pose[parents[n]] if parents[n] else idle[n].copy()
                if n=='MiddleJoint': current.translation=current.translation+offset
                elif n=='cockpit': current.rotation=axis_rotation(lean_axis,-30.*chest)*rest[n].rotation
                elif n.startswith('GunArm_'):
                    # Absolute arm lag is capped at 60 degrees, including the chest contribution.
                    # Arms hang below the shoulder: their lag rotation has the
                    # opposite sign to the upright chest's counter-lean.
                    current.rotation=axis_rotation(lean_axis,60.*arm)*rest[n].rotation
                elif n.startswith('IK_Foot_'):
                    foot='Foot_'+n[-1]
                    current=rest[n].make_relative(rest[foot])*pose[foot]
                pose[n]=solved.get(n,current)
            return pose,diagnostics

        factor=1.; pose,diagnostics=solve(factor)
        if pose is None:
            low,high=0.,1.
            for _ in range(22):
                mid=(low+high)*.5
                candidate,_=solve(mid)
                if candidate is None: high=mid
                else: low=mid
            factor=low; pose,diagnostics=solve(factor)
        assert pose is not None,(anim.get_name(),f)
        local={n:pose[n].make_relative(pose[parents[n]]) if parents[n] else pose[n] for n in names}
        if f in (0,81): local={n:t.copy() for n,t in source[f].items()}
        result.append(local)
        metrics['min_drag_scale']=min(metrics['min_drag_scale'],factor)
        metrics['min_foot_spacing']=min(metrics['min_foot_spacing'],(pose['Foot_L'].translation-pose['Foot_R'].translation).length2d())
        for bend,swing in diagnostics:
            metrics['min_knee']=min(metrics['min_knee'],bend); metrics['max_knee']=max(metrics['max_knee'],bend)
            metrics['max_hip_swing']=max(metrics['max_hip_swing'],swing)
        for key,val in [('max_pelvis_travel',50*waist*factor),('max_pelvis_lift',40*waist*factor),
                        ('max_foot_trail',60*leg*factor),('max_foot_lift',40*leg*factor)]: metrics[key]=max(metrics[key],val)
    return source,result,metrics


def pchip_keys(values):
    """Monotone cubic Hermite tangents, no between-key overshoot. End tangents are zero."""
    slopes=[(b-a)*RATE for a,b in zip(values,values[1:])]
    keys=[]
    for i,value in enumerate(values):
        tangent=0.
        if 0<i<len(values)-1 and slopes[i-1]*slopes[i]>0:
            tangent=2*slopes[i-1]*slopes[i]/(slopes[i-1]+slopes[i])
        key=unreal.RichCurveKey()
        key.import_text(f'(InterpMode=RCIM_Cubic,TangentMode=RCTM_User,Time={i/RATE:.9f},Value={value:.9f},ArriveTangent={tangent:.9f},LeaveTangent={tangent:.9f})')
        keys.append(key)
    return keys


def author_directional_sequences(apply=False):
    """Explicit one-time artist operation. Normal imports must never invoke this function."""
    import shutil
    lib=unreal.EditorAssetLibrary
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    root=Path(unreal.Paths.project_dir()).resolve(); out=root/'Artifacts/GroundDirectionalBlink_20261107'
    out.mkdir(parents=True,exist_ok=True)
    rows=json.loads((root/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/ue_mesh_import.json').read_text())['bones']
    names=[b['name'] for b in rows]; parents={b['name']:b['parent'] for b in rows}
    idle_anim=unreal.load_asset(FOLDER+'/Anim_Mecha_Idle')
    idle=dict(zip(names,unreal.AnimationLibrary.get_bone_poses_for_frame(idle_anim,names,0,False)))
    front=unreal.load_asset(FOLDER+'/'+NAME); front_before=digest_animation(front,names,idle)
    upper=['cockpit','GunArm_L','GunArm_R','Gun_L','Gun_R']
    reports=[]
    for suffix,direction in [('back',unreal.Vector(0,-1,0)),('left',unreal.Vector(1,0,0)),('right',unreal.Vector(-1,0,0))]:
        anim=unreal.load_asset(FOLDER+'/'+NAME+'_'+suffix)
        assert anim.get_skeleton()==idle_anim.get_skeleton() and anim.data_model_interface.get_number_of_keys()==82
        assert not list(unreal.AnimationLibrary.get_animation_curve_names(anim,unreal.RawCurveTrackTypes.RCT_TRANSFORM)), 'Existing artist layers: refusing to replace '+anim.get_name()
        retained={str(n).lower() for n in anim.data_model_interface.get_bone_track_names()}-set(n.lower() for n in upper)
        before=digest_animation(anim,names,idle,retained)
        source,desired,metrics=directional_frames(anim,direction,names,parents,idle)
        report={'asset':anim.get_path_name(),'metrics':metrics,'retained_base_hash':before['raw'],'saved':False}
        if apply:
            backup=out/'Before'/f'{anim.get_name()}.uasset'; backup.parent.mkdir(exist_ok=True)
            if not backup.exists(): shutil.copy2(root/'Content/GuLiStrike/GroundMech/Animations/A4B2'/backup.name,backup)
            ctl=anim.controller; ctl.open_bracket('Author directional blink additive layers',True)
            try:
                tracks={str(n).lower() for n in anim.data_model_interface.get_bone_track_names()}
                for n in upper:
                    if n.lower() not in tracks: assert ctl.add_bone_curve(n,True)
                    t=idle[n]
                    assert ctl.set_bone_track_keys(n,[t.translation]*82,[t.rotation]*82,[unreal.Vector(1,1,1)]*82,True)
                for n in names:
                    if n=='root' or n.startswith('Gun_'): continue
                    offsets=[desired[f][n].make_relative(source[f][n]) for f in range(82)]
                    offsets[0]=unreal.Transform(); offsets[-1]=unreal.Transform()
                    ident=unreal.AnimationCurveIdentifier(); ident.set_curve_identifier(n,unreal.RawCurveTrackTypes.RCT_TRANSFORM)
                    assert ctl.add_curve(ident,4,True),n
                    data=[list(v.translation.to_tuple())+list(v.rotation.euler().to_tuple())+[1.,1.,1.] for v in offsets]
                    for component,channel in enumerate((unreal.TransformCurveChannel.POSITION,unreal.TransformCurveChannel.ROTATION,unreal.TransformCurveChannel.SCALE)):
                        for axis,enum in enumerate((unreal.VectorCurveChannel.X,unreal.VectorCurveChannel.Y,unreal.VectorCurveChannel.Z)):
                            child=unreal.AnimationCurveIdentifier(); child.set_curve_identifier(n,unreal.RawCurveTrackTypes.RCT_TRANSFORM)
                            assert child.get_transform_child_curve_identifier(channel,enum)
                            values=[row[component*3+axis] for row in data]
                            if component==1:
                                for i in range(1,len(values)):
                                    while values[i]-values[i-1]>180: values[i]-=360
                                    while values[i]-values[i-1]<-180: values[i]+=360
                            assert ctl.set_curve_keys(child,pchip_keys(values),True),(n,component,axis)
            finally:
                try: assert unreal.GuLiGroundMechAnimationEditorLibrary.notify_animation_keys_populated(anim)
                finally: ctl.close_bracket(True)
            assert digest_animation(anim,names,idle,retained)['raw']==before['raw'],'Base leg tracks changed'
            anim.set_editor_property('enable_root_motion',False); anim.set_editor_property('loop',False)
            assert lib.save_loaded_asset(anim,False)
            report['saved']=True
            report['additive_tracks']=list(map(str,unreal.AnimationLibrary.get_animation_curve_names(anim,unreal.RawCurveTrackTypes.RCT_TRANSFORM)))
        reports.append(report)
    assert digest_animation(front,names,idle)==front_before,'Forward artist data changed'
    report={'applied':apply,'forward_unchanged':front_before,'animations':reports,'visual_verification':'pending_player'}
    (out/('animation-authoring.json' if apply else 'animation-design.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    return report


def ensure_directional_blend():
    """Create the official blend asset without regenerating any animation keys."""
    path=FOLDER+'/BlendSpace_Mecha_Blink_Directional'
    blend=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if blend and len(blend.get_editor_property('sample_data'))==5: return blend
    front=unreal.load_asset(FOLDER+'/'+NAME)
    mesh=unreal.get_default_object(unreal.load_asset('/Game/GuLiStrike/GroundMech/BP_GroundMech_Light').generated_class()).get_editor_property('mesh').get_editor_property('skeletal_mesh_asset')
    assert mesh.skeleton==front.get_skeleton()
    factory=unreal.BlendSpaceFactory1D()
    factory.set_editor_property('target_skeleton',mesh.skeleton)
    factory.set_editor_property('preview_skeletal_mesh',mesh)
    if blend is None: blend=unreal.AssetToolsHelpers.get_asset_tools().create_asset('BlendSpace_Mecha_Blink_Directional',FOLDER,unreal.BlendSpace1D,factory)
    params=list(blend.get_editor_property('blend_parameters'))
    for key,value in {'display_name':'Direction','min':-180.,'max':180.,'grid_num':8,'wrap_input':True}.items(): params[0].set_editor_property(key,value)
    blend.set_editor_property('blend_parameters',params)
    samples=[]
    for degrees,suffix in [(-180,'_back'),(-90,'_left'),(0,''),(90,'_right'),(180,'_back')]:
        sample=unreal.BlendSample(); sample.set_editor_property('animation',unreal.load_asset(FOLDER+'/'+NAME+suffix))
        sample.set_editor_property('sample_value',unreal.Vector(degrees,0,0)); sample.set_editor_property('rate_scale',1.)
        samples.append(sample)
    blend.set_editor_property('sample_data',samples)
    # Resample using the project's existing editor API; then set the one-shot contract.
    assert unreal.GuLiGroundMechAnimationEditorLibrary.rebuild_blend_space(blend,unreal.load_asset(FOLDER+'/BlendSpace_Loco_Walk'))
    blend.set_editor_property('allow_marker_based_sync',False)
    blend.set_editor_property('allow_mesh_space_blending',False)
    blend.set_editor_property('loop',False)
    blend.set_editor_property('notify_trigger_mode',unreal.NotifyTriggerMode.NONE)
    blend.set_editor_property('target_weight_interpolation_speed_per_sec',0.)
    interp=list(blend.get_editor_property('interpolation_param'))
    for value in interp: value.set_editor_property('interpolation_time',0.)
    blend.set_editor_property('interpolation_param',interp)
    assert unreal.EditorAssetLibrary.save_loaded_asset(blend,False)
    return blend


def read_directional_sequences():
    """Numerical asset inspection only: no world tick, screenshots or gameplay."""
    root=Path(unreal.Paths.project_dir()).resolve()
    rows=json.loads((root/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export/ue_mesh_import.json').read_text())['bones']
    names=[b['name'] for b in rows]; parents={b['name']:b['parent'] for b in rows}
    idle_anim=unreal.load_asset(FOLDER+'/Anim_Mecha_Idle')
    idle=dict(zip(names,unreal.AnimationLibrary.get_bone_poses_for_frame(idle_anim,names,0,False)))
    rest=components(idle,names,parents)
    authored=json.loads((root/'Artifacts/GroundDirectionalBlink_20261107/animation-authoring.json').read_text())
    assert digest_animation(unreal.load_asset(FOLDER+'/'+NAME),names,idle)==authored['forward_unchanged']
    reports=[]
    options=unreal.AnimPoseEvaluationOptions(); options.evaluation_type=unreal.AnimDataEvalType.RAW; options.should_retarget=False
    for suffix in ('back','left','right'):
        anim=unreal.load_asset(FOLDER+'/'+NAME+'_'+suffix)
        source=raw_frames(anim,names,idle)
        stats=dict(max_bone_length_error=0.,max_scale_error=0.,max_gun_connection_error=0.,max_gun_angle=0.,
                   min_knee=180.,max_knee=0.,max_hip_swing=0.,max_knee_twist=0.,min_foot_spacing=1.e9,max_endpoint_error=0.)
        for f in range(82):
            pose=unreal.AnimPoseExtensions.get_anim_pose_at_frame(anim,f,options)
            local={n:unreal.AnimPoseExtensions.get_bone_pose(pose,n,unreal.AnimPoseSpaces.LOCAL) for n in names}
            cs=components(local,names,parents)
            for n,t in local.items():
                stats['max_scale_error']=max(stats['max_scale_error'],(t.scale3d-unreal.Vector(1,1,1)).length())
                if n not in ('root','MiddleJoint','IK_Foot_L','IK_Foot_R'):
                    stats['max_bone_length_error']=max(stats['max_bone_length_error'],abs(t.translation.length()-idle[n].translation.length()))
                if n.startswith('Gun_'):
                    stats['max_gun_connection_error']=max(stats['max_gun_connection_error'],(t.translation-idle[n].translation).length())
                    delta=t.rotation*inverse(idle[n].rotation)
                    stats['max_gun_angle']=max(stats['max_gun_angle'],math.degrees(2*math.acos(min(1.,abs(delta.w)))))
                if f in (0,81):
                    delta=t.rotation*inverse(source[f][n].rotation)
                    stats['max_endpoint_error']=max(stats['max_endpoint_error'],(t.translation-source[f][n].translation).length(),math.degrees(2*math.acos(min(1.,abs(delta.w)))))
            assert (local['root'].translation-source[0]['root'].translation).length()<.001
            for side in ('L','R'):
                h,k,a=[cs[n+'_'+side].translation for n in ('UpperLeg','Leg','Foot')]
                rh,rk,ra=[rest[n+'_'+side].translation for n in ('UpperLeg','Leg','Foot')]
                bend=angle(h-k,a-k)
                axis=(a-h).normal(); restaxis=(ra-rh).normal()
                restpole=(rk-rh-restaxis*(rk-rh).dot(restaxis)).normal()
                hinge=(restpole-axis*restpole.dot(axis)).normal(); pole=(k-h-axis*(k-h).dot(axis)).normal()
                stats['min_knee']=min(stats['min_knee'],bend); stats['max_knee']=max(stats['max_knee'],bend)
                stats['max_hip_swing']=max(stats['max_hip_swing'],angle(k-h,rk-rh))
                stats['max_knee_twist']=max(stats['max_knee_twist'],angle(hinge,pole))
            stats['min_foot_spacing']=min(stats['min_foot_spacing'],(cs['Foot_L'].translation-cs['Foot_R'].translation).length2d())
        assert stats['max_bone_length_error']<.01 and stats['max_scale_error']<.0001,stats
        assert stats['max_gun_connection_error']<.01 and stats['max_gun_angle']<.05 and stats['max_endpoint_error']<.05,stats
        assert stats['min_knee']>=79.99 and stats['max_knee']<=155.01 and stats['max_hip_swing']<=45.01 and stats['max_knee_twist']<=25.01 and stats['min_foot_spacing']>=40.,stats
        rate=anim.data_model_interface.get_frame_rate()
        markers={str(m.marker_name):m.time for m in unreal.AnimationLibrary.get_animation_sync_markers(anim)}
        assert rate.numerator==150 and rate.denominator==1 and anim.data_model_interface.get_number_of_keys()==82
        assert abs(anim.get_play_length()-.54)<.0001 and abs(markers['BlinkConceal']-.02)<.0001 and abs(markers['BlinkRecovery']-.24)<.0001
        assert not anim.get_editor_property('enable_root_motion')
        reports.append({'asset':anim.get_path_name(),'samples':82,'stats':stats,'markers':markers})
    result={'success':True,'animations':reports,'forward_unchanged':True,'PIE_started':False,'visual_verification':'pending_player'}
    (root/'Artifacts/GroundDirectionalBlink_20261107/animation-readback.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    return result


if __name__ == '__main__':
    unreal.MCPythonHelper.submit_result(json.dumps(ensure_sequence()))
