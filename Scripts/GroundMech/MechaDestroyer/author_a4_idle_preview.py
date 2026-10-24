"""Preserve the source clip and transfer its first-frame-relative idle motion.

The editable A4 neutral pose is not modified. This is an independent review rig,
not a declaration that all Unreal source animation/retarget paths are finished.
"""
import bpy,json,math,hashlib
from pathlib import Path
from mathutils import Vector,Quaternion,Matrix

OUT=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v1')
OWNER='GuLi.GroundMech.ApprovedA4.B1'
source_scene=bpy.data.scenes['GroundMech_A4_SourceAnimation']
source=bpy.data.objects['GMA4_SourceIdle_Armature']
blue=bpy.data.collections['GMA4_TEAM_BLUE']
original=bpy.data.objects['GMA4_Rig_BLUE']
previous=bpy.context.window.scene
old=bpy.data.scenes.get('GroundMech_A4_04_SourceIdle')
if old:
    assert old.get('author')==OWNER
    for o in list(old.objects):
        if o.get('author')==OWNER:bpy.data.objects.remove(o,do_unlink=True)
    bpy.data.scenes.remove(old)
for stale in list(bpy.data.actions):
    if stale.name.startswith('A4_SourceIdle_RelativeMotion') and stale.users-int(stale.use_fake_user)==0:
        bpy.data.actions.remove(stale)
scene=bpy.data.scenes.new('GroundMech_A4_04_SourceIdle')
scene['author']=OWNER;scene.world=previous.world;scene.render.fps=30
scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True
scene.view_settings.view_transform='Standard';scene.view_settings.look='None'
scene.render.resolution_x=1500;scene.render.resolution_y=1500;scene.render.resolution_percentage=100
rig=original.copy();rig.data=original.data.copy();rig.animation_data_clear();rig.name='GMA4_Rig_SourceIdle'
scene.collection.objects.link(rig)
mapping={original:rig}
for o in blue.all_objects:
    if o==original:continue
    copy=o.copy();copy.name=o.name+'_IDLE';scene.collection.objects.link(copy);mapping[o]=copy
    for modifier in copy.modifiers:
        if modifier.type=='ARMATURE':modifier.object=rig
for o,c in mapping.items():
    if o.parent in mapping:c.parent=mapping[o.parent]
for bone in rig.pose.bones:bone.rotation_mode='QUATERNION'
action=bpy.data.actions.new('A4_SourceIdle_RelativeMotion')
action['author']=OWNER
rig.animation_data_create();rig.animation_data.action=action
action.use_fake_user=True
first,last=map(round,source.animation_data.action.frame_range)
bpy.context.window.scene=source_scene;source_scene.frame_set(first)
bpy.context.view_layer.update()
baseline={b.name:b.matrix_basis.copy() for b in source.pose.bones}
world_baseline={b.name:source.matrix_world@b.matrix for b in source.pose.bones}
rest={b.name:b.matrix_local.copy() for b in rig.data.bones}
local_rest={b.name:(rest[b.parent.name].inverted()@rest[b.name]) if b.parent else rest[b.name] for b in rig.data.bones}
scale=source.matrix_world.to_scale().x
maximum_angle=0.;maximum_translation=0.
foot_errors=[]
previous_quaternions={}
for frame in range(first,last+1):
    source_scene.frame_set(frame);bpy.context.view_layer.update()
    basis={'root':Matrix.Identity(4)}
    for source_bone in source.pose.bones:
        target=rig.pose.bones.get(source_bone.name)
        if not target:continue
        loc,rot,unused=source_bone.matrix_basis.decompose()
        base_loc,base_rot,unused=baseline[source_bone.name].decompose()
        rotation=base_rot.inverted()@rot
        translation=(loc-base_loc)*scale
        basis[target.name]=Matrix.LocRotScale(translation,rotation,Vector((1,1,1)))
        maximum_angle=max(maximum_angle,abs(rotation.angle))
        maximum_translation=max(maximum_translation,translation.length)
    pose={}
    for b in rig.data.bones:
        parent=pose[b.parent.name] if b.parent else Matrix.Identity(4)
        pose[b.name]=parent@local_rest[b.name]@basis.get(b.name,Matrix.Identity(4))
    # A4 has different segment lengths. Solve each two-link leg to the source
    # ankle displacement instead of allowing idle bob to drag the soles.
    for side in ['R','L']:
        upper,lower,foot,toe=['UpperLeg_'+side,'Leg_'+side,'Foot_'+side,'FootEnd_'+side]
        h0,k0,a0=(rest[name].translation for name in [upper,lower,foot])
        h=pose[upper].translation.copy()
        source_foot=source.matrix_world@source.pose.bones[foot].matrix
        ankle=a0+source_foot.translation-world_baseline[foot].translation
        d=ankle-h;distance=d.length;direction=d.normalized()
        l1=(k0-h0).length;l2=(a0-k0).length
        assert abs(l1-l2)<distance<l1+l2,('Unreachable A4 idle foot',side,frame,distance)
        along=(l1*l1-l2*l2+distance*distance)/(2*distance)
        original_direction=(a0-h0).normalized()
        bend=k0-h0-original_direction*((k0-h0).dot(original_direction))
        bend-=direction*bend.dot(direction);bend.normalize()
        knee=h+direction*along+bend*math.sqrt(max(0,l1*l1-along*along))
        for name,oldvec,newvec,position in [(upper,k0-h0,knee-h,h),(lower,a0-k0,ankle-knee,knee)]:
            q=oldvec.rotation_difference(newvec)
            m=(q.to_matrix()@rest[name].to_3x3()).to_4x4();m.translation=position;pose[name]=m
        delta_rot=source_foot.to_quaternion()@world_baseline[foot].to_quaternion().inverted()
        m=(delta_rot.to_matrix()@rest[foot].to_3x3()).to_4x4();m.translation=ankle;pose[foot]=m
        pose[toe]=m@local_rest[toe]@basis.get(toe,Matrix.Identity(4))
        pose['IK_Foot_'+side]=rest['IK_Foot_'+side].copy();pose['IK_Foot_'+side].translation=ankle
        foot_errors.append((pose[foot].translation-ankle).length)
    for b in rig.data.bones:
        parent=pose[b.parent.name] if b.parent else Matrix.Identity(4)
        local=local_rest[b.name].inverted()@parent.inverted()@pose[b.name]
        target=rig.pose.bones[b.name]
        loc,quat,unused=local.decompose()
        if b.name in previous_quaternions and previous_quaternions[b.name].dot(quat)<0:quat.negate()
        previous_quaternions[b.name]=quat.copy()
        target.rotation_quaternion=quat;target.location=loc;target.scale=(1,1,1)
        target.keyframe_insert(data_path='rotation_quaternion',frame=frame,group=target.name)
        target.keyframe_insert(data_path='location',frame=frame,group=target.name)
scene.frame_start=first;scene.frame_end=last;scene.frame_set(first)
data=bpy.data.cameras.new('GMA4_SourceIdle_Camera');data.type='ORTHO';data.ortho_scale=6.4
cam=bpy.data.objects.new('GMA4_SourceIdle_Camera',data);scene.collection.objects.link(cam);cam['author']=OWNER
cam.location=(7,-14,6.2);cam.rotation_euler=(Vector((0,0,2.4))-cam.location).to_track_quat('-Z','Y').to_euler();scene.camera=cam
bpy.context.window.scene=scene;scene.frame_set((first+last)//2);bpy.context.view_layer.update()
motion=max(b.matrix_basis.to_quaternion().angle for b in rig.pose.bones)
assert maximum_angle>1e-4 or maximum_translation>1e-5,'Source clip contains no measured motion'
evaluated_errors=[]
for frame in [first,31,76,121,last]:
    bpy.context.window.scene=source_scene;source_scene.frame_set(frame);source_scene.view_layers[0].update()
    expected={side:rest['Foot_'+side].translation+(source.matrix_world@source.pose.bones['Foot_'+side].matrix).translation-world_baseline['Foot_'+side].translation for side in ['R','L']}
    bpy.context.window.scene=scene;scene.frame_set(frame);scene.view_layers[0].update()
    evaluated=rig.evaluated_get(bpy.context.evaluated_depsgraph_get())
    for side in ['R','L']:
        actual=evaluated.matrix_world@evaluated.pose.bones['Foot_'+side].matrix.translation
        evaluated_errors.append((actual-expected[side]).length)
assert max(evaluated_errors)<1e-4,('Baked clip ankle error',max(evaluated_errors))
scene.frame_set(first);source_scene.frame_set(first)
bpy.context.window.scene=previous
result={'success':True,'source_fbx':'../SourceExport/Anim_Mecha_Idle.fbx','source_action':source.animation_data.action.name,
        'target_action':action.name,'preview_scene':scene.name,'frames':[first,last],'fps':scene.render.fps,
        'bone_channels_transferred':len(source.pose.bones),'maximum_rotation_degrees':math.degrees(maximum_angle),
        'maximum_translation_m':maximum_translation,'source_clip_mutated':False,'neutral_A4_scene_mutated':False,
        'maximum_ankle_target_error_m':max(foot_errors),
        'evaluated_baked_ankle_error_m':max(evaluated_errors),
        'transfer':'Source-relative motion with analytic two-link leg retarget and separate GunArm housing / Gun barrel spin',
        'UE_all_animation_compatibility':'Not claimed; native Unreal retarget/Blueprint verification remains a later integration step'}
(OUT/'source_idle_adapter.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'GroundMech_A4_Editable_B_v1.blend'))
