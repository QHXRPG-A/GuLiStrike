"""Retarget complete source local poses to the restored source-compatible rest frames."""
import bpy,json,math
import numpy as np
from pathlib import Path
from mathutils import Matrix,Vector,Quaternion
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1'
assert not (O/'production_manifest.json').exists()
bpy.ops.wm.open_mainfile(filepath=str(O/'SSF_B1_Atlas.blend'))
source=json.loads((R/'Source/source_manifest.json').read_text(encoding='utf8'))
report=json.loads((O/'atlas_report.json').read_text(encoding='utf8'))
src={m['key']:m for m in source['meshes']};S=Matrix.Diagonal((1.,-1.,1.,1.));I=Matrix.Identity(4)
def ue_matrix(v):
 if isinstance(v,dict):t,q,s=v['translation_cm'],v['rotation_xyzw'],v['scale']
 else:t,q,s=v[:3],v[3:7],v[7:10]
 return Matrix.LocRotScale(Vector(t)*.01,Quaternion((q[3],q[0],q[1],q[2])),Vector(s))
animations=[]
for asset in report['assets']:
 if not asset['rig']:continue
 key=asset['key'];scene=bpy.data.scenes[asset['scene']];bpy.context.window.scene=scene
 rig=bpy.data.objects[asset['rig']];rig.data.pose_position='POSE';rig.animation_data_create()
 names=[b['name'] for b in src[key]['bones']];parent={b['name']:b['parent'] for b in src[key]['bones']}
 reference={b['name']:S@ue_matrix(b['global'])@S for b in src[key]['bones']};invref={n:m.inverted() for n,m in reference.items()}
 rest={b.name:b.matrix_local.copy() for b in rig.data.bones};invrest={n:m.inverted() for n,m in rest.items()}
 for pb in rig.pose.bones:pb.rotation_mode='QUATERNION'
 clips=[c for c in source['animations'] if c['skeleton']==src[key]['skeleton']]
 for clip in clips:
  raw=json.loads((O/'AnimationSource'/(clip['name']+'_FullPose.json')).read_text());assert raw['bone_names']==names
  channels={n:[] for n in names};previous={};full_error=0.;zero_scale_samples=0
  for frame in raw['frames']:
   source_world={};native_world={}
   for index,n in enumerate(names):
    par=parent[n];local=S@ue_matrix(frame['local_tqs'][index])@S
    source_world[n]=source_world[par]@local if par else local
    # This algebra cancels the pose of the parent without inverting a possibly
    # zero-scaled destruction pose. Rest matrices remain nonsingular.
    basis=invrest[n]@(reference[par] if par else I)@local@invref[n]@rest[n]
    loc,quat,scale=basis.decompose()
    if n in previous and quat.dot(previous[n])<0:quat.negate()
    previous[n]=quat.copy();channels[n].append([*loc,quat.w,quat.x,quat.y,quat.z,*scale])
    native=Matrix.LocRotScale(loc,quat,scale)
    native_world[n]=(native_world[par]@invrest[par]@rest[n]@native) if par else rest[n]@native
    desired=source_world[n]@invref[n]@rest[n]
    full_error=max(full_error,max(abs(native_world[n][r][c]-desired[r][c]) for r in range(4) for c in range(4)))
    zero_scale_samples+=int(min(abs(v) for v in scale)<1e-7)
  action=bpy.data.actions.new(clip['name']);action.use_fake_user=True;action['original_UE_clip_name']=clip['name'];action['source_path']=clip['path'];action['duration_s']=raw['duration_s'];action['approved_reference']='A_v7'
  slot=action.slots.new('OBJECT',rig.name);layer=action.layers.new('OriginalSourceMotion');strip=layer.strips.new(type='KEYFRAME');bag=strip.channelbag(slot,ensure=True)
  frame_numbers=np.array([f['time_s']*30+1 for f in raw['frames']],np.float32)
  for n in names:
   values=np.array(channels[n],np.float32)
   for attribute,start,length in [('location',0,3),('rotation_quaternion',3,4),('scale',7,3)]:
    for axis in range(length):
     fc=bag.fcurves.new(data_path=f'pose.bones["{n}"].{attribute}',index=axis,group_name=n)
     indices=np.arange(len(frame_numbers)) if np.ptp(values[:,start+axis])>1e-8 else np.array([0,len(frame_numbers)-1])
     fc.keyframe_points.add(len(indices));fc.keyframe_points.foreach_set('co',np.column_stack((frame_numbers[indices],values[indices,start+axis])).ravel())
     for point in fc.keyframe_points:point.interpolation='LINEAR'
     fc.update()
  rig.animation_data.action=action;rig.animation_data.action_slot=slot
  native_error=0.;samples=[]
  for idx in sorted(set([0,len(raw['frames'])//4,len(raw['frames'])//2,3*len(raw['frames'])//4,len(raw['frames'])-1])):
   frame=raw['frames'][idx];number=1+frame['time_s']*30;scene.frame_set(int(number),subframe=number-int(number));bpy.context.view_layer.update();world={}
   for i,n in enumerate(names):
    local=S@ue_matrix(frame['local_tqs'][i])@S;world[n]=world[parent[n]]@local if parent[n] else local;expected=world[n]@invref[n]@rest[n]
    actual=rig.pose.bones[n].matrix;native_error=max(native_error,max(abs(actual[r][c]-expected[r][c]) for r in range(4) for c in range(4)))
   samples.append(frame['time_s'])
  result={'asset':key,'name':clip['name'],'action':action.name,'duration_s':raw['duration_s'],'source_sample_rate':30,
   'full_source_samples':len(raw['frames']),'bone_count':len(names),'curve_count':len(bag.fcurves),
   'all_sample_matrix_reconstruction_max_error':full_error,'actual_Blender_sample_matrix_max_error':native_error,
   'actual_Blender_sample_times_s':samples,'source_zero_scale_bone_samples':zero_scale_samples,'all_translation_rotation_scale_channels_preserved':True}
  animations.append(result);print('SSF_SOURCE_ACTION_READY',json.dumps(result),flush=True)
  assert full_error<.002 and native_error<.002,(clip['name'],full_error,native_error)
 rig.animation_data.action=None
 for pb in rig.pose.bones:pb.matrix_basis=Matrix.Identity(4)
 scene.frame_set(1);bpy.context.view_layer.update()
 asset['animations']=[c['name'] for c in clips];asset['rig_names_hierarchy_preserved']=True
assert len(animations)==22
report['animations']=animations;report['stage']='actual_Blender_product_for_B_review';report['B_approval']='pending'
(O/'construction_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
bpy.ops.wm.save_as_mainfile(filepath=str(O/'SSF_Production_B_v1.blend'))
print('SSF_ALL_22_NATIVE_ACTIONS_OK',max(a['actual_Blender_sample_matrix_max_error'] for a in animations),flush=True)
