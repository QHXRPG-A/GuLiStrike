"""Capture full source animation at 30fps, read only, one asset per invocation."""
import unreal,json,math,hashlib,time
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
O=R/'Production_B_v1/AnimationSource';O.mkdir(parents=True,exist_ok=True)
source=json.loads((R/'Source/source_manifest.json').read_text(encoding='utf8'))
# A small local selector is written by the orchestration script; it is not an UE asset.
key=(O/'capture_key.txt').read_text(encoding='utf8').strip()
mesh=next(m for m in source['meshes'] if m['key']==key)
clips=[a for a in source['animations'] if a['skeleton']==mesh['skeleton']]
records=[]
for clip in clips:
 file=O/(clip['name']+'_FullPose.json')
 if file.exists(): continue
 start=time.time();duration=float(clip['duration_s']);fps=30
 names=[b['name'] for b in mesh['bones']]
 frames=[]
 for index in range(math.ceil(duration*fps-1e-4)+1):
  t=min(index/fps,duration)
  ordered=sorted(unreal.AnimSequenceService.get_pose_at_time(clip['path'],t,False),key=lambda x:int(x.bone_index))
  assert [str(b.bone_name) for b in ordered]==names
  values=[]
  for b in ordered:
   tr=b.transform;v=tr.translation;q=tr.rotation;s=tr.scale3d
   values.append([float(v.x),float(v.y),float(v.z),float(q.x),float(q.y),float(q.z),float(q.w),float(s.x),float(s.y),float(s.z)])
  frames.append({'time_s':t,'local_tqs':values})
 data={'asset':key,'name':clip['name'],'path':clip['path'],'source_fbx_sha256':clip['sha256'],
  'duration_s':duration,'fps':fps,'source_compressed_keys':clip['frames'],'bone_names':names,
  'space':'UE local cm / xyzw quaternion / original scale','frames':frames,'ue_assets_saved':False}
 file.write_text(json.dumps(data,separators=(',',':')),encoding='utf8')
 records.append({'clip':clip['name'],'frames':len(frames),'bones':len(names),'seconds':round(time.time()-start,2)})
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'asset':key,'records':records,'ue_assets_saved':False}))
