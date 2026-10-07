"""Render all complete source clips, three actual LODs side by side, then encode MP4."""
import bpy,json,math,sys,argparse,hashlib,shutil
import numpy as np
from pathlib import Path
from mathutils import Vector,Matrix,Quaternion
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1';V=O/'AnimationPreviews';V.mkdir(exist_ok=True)
p=argparse.ArgumentParser();p.add_argument('--assets',default='');args=p.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
file=O/'SSF_Production_B_v1.blend';blend_sha=hashlib.sha256(file.read_bytes()).hexdigest();bpy.ops.wm.open_mainfile(filepath=str(file))
report=json.loads((O/'construction_report.json').read_text(encoding='utf8'));src=json.loads((R/'Source/source_manifest.json').read_text(encoding='utf8'))
sources={a['key']:a for a in src['meshes']};refs={a['key']:a for a in json.loads((R/'References_A_v7/render_manifest.json').read_text(encoding='utf8'))['assets']}
selected=set(args.assets.split(',')) if args.assets else None
S=Matrix.Diagonal((1.,-1.,1.,1.))
def ue(v):
 if isinstance(v,dict):t,q,s=v['translation_cm'],v['rotation_xyzw'],v['scale']
 else:t,q,s=v[:3],v[3:7],v[7:10]
 return S@Matrix.LocRotScale(Vector(t)*.01,Quaternion((q[3],q[0],q[1],q[2])),Vector(s))@S
ink=bpy.data.materials.new('ActualMovie_Labels');ink.use_nodes=True;nodes=ink.node_tree.nodes;nodes.clear();out=nodes.new('ShaderNodeOutputMaterial');emit=nodes.new('ShaderNodeEmission');emit.inputs[0].default_value=(.01033,.00913,.02843,1);ink.node_tree.links.new(emit.outputs[0],out.inputs['Surface'])
results=[]
for a in report['assets']:
 key=a['key']
 if not a['rig'] or selected and key not in selected:continue
 original_scene=bpy.data.scenes[a['scene']];bpy.context.window.scene=original_scene;bpy.context.view_layer.update()
 original_rig=bpy.data.objects[a['rig']];original_rig.animation_data.action=None
 info=sources[key];names=[b['name'] for b in info['bones']];parents={b['name']:b['parent'] for b in info['bones']};invref={b['name']:ue(b['global']).inverted() for b in info['bones']}
 body=bpy.data.objects[a['lods'][0]['body']]
 boundpoints={}
 for n in names:
  gid=body.vertex_groups[n].index
  coords=[v.co for v in body.data.vertices if any(g.group==gid and g.weight>.00001 for g in v.groups)]
  if not coords:continue
  lo=Vector([min(v[i] for v in coords) for i in range(3)]);hi=Vector([max(v[i] for v in coords) for i in range(3)])
  boundpoints[n]=[Vector((x,y,z)) for x in (lo.x,hi.x) for y in (lo.y,hi.y) for z in (lo.z,hi.z)]
 for clip in a['animations']:
  video_file=V/(clip+'.mp4');record_file=V/(clip+'_report.json')
  if record_file.exists() and video_file.exists():
   existing=json.loads(record_file.read_text());assert existing['source_blend_sha256']==blend_sha;results.append(existing);continue
  raw=json.loads((O/'AnimationSource'/(clip+'_FullPose.json')).read_text());duration=raw['duration_s'];action=bpy.data.actions[clip]
  from mathutils import Euler
  rotation=Euler(refs[key]['views']['Hero']['rotation_rad']).to_quaternion();right=rotation@Vector((1,0,0));up=rotation@Vector((0,1,0));forward=rotation@Vector((0,0,1))
  # All 30fps source samples bound the complete visible motion, including
  # destruction debris. Every LOD receives the same fixed camera and timeline.
  xp=[];yp=[];xyz=[]
  for sample in raw['frames']:
   world={}
   for i,n in enumerate(names):
    local=ue(sample['local_tqs'][i]);world[n]=world[parents[n]]@local if parents[n] else local
    if n not in boundpoints:continue
    delta=world[n]@invref[n]
    for v in boundpoints[n]:
     now=delta@v;xp.append(now.dot(right));yp.append(now.dot(up));xyz.append(now)
  lo=Vector([min(v[i] for v in xyz) for i in range(3)]);hi=Vector([max(v[i] for v in xyz) for i in range(3)]);center=(lo+hi)/2
  # Match projected centers rather than letting a distant destruction fragment
  # move the models toward one edge of all three panels.
  center+=right*((min(xp)+max(xp))/2-center.dot(right))+up*((min(yp)+max(yp))/2-center.dot(up))
  scale=max(refs[key]['views']['Hero']['ortho_scale_m'],1.22*(max(xp)-min(xp)),1.22*(max(yp)-min(yp)))
  scene=bpy.data.scenes.new('Motion_'+clip);bpy.context.window.scene=scene;scene.world=original_scene.world.copy();scene.render.engine='BLENDER_EEVEE';scene.eevee.taa_render_samples=16
  scene.render.resolution_x=1920;scene.render.resolution_y=768;scene.render.resolution_percentage=100;scene.render.fps=30;scene.render.use_freestyle=False;scene.view_settings.view_transform='Standard';scene.view_settings.look='None'
  scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGB'
  camera=bpy.data.objects.new('ActualMotionCamera',bpy.data.cameras.new('ActualMotionCamera'));scene.collection.objects.link(camera);scene.camera=camera;camera.data.type='ORTHO';camera.data.ortho_scale=3*scale;camera.data.clip_end=10000
  camera.location=center+forward*max(100,scale*6);camera.rotation_euler=rotation.to_euler()
  copies=[]
  for lod in range(3):
   rig=original_rig.copy();rig.name=clip+f'_LOD{lod}_Rig';scene.collection.objects.link(rig);rig.location=right*((lod-1)*scale);rig.data.pose_position='POSE';rig.animation_data_create();rig.animation_data.action=action;rig.animation_data.action_slot=action.slots[0];copies.append(rig)
   e=a['lods'][lod]
   for name in (e['body'],e['outline']):
    if not name:continue
    sourceob=bpy.data.objects[name];ob=sourceob.copy();scene.collection.objects.link(ob);ob.name=clip+f'_LOD{lod}_'+sourceob.name;ob.parent=rig;ob.matrix_parent_inverse=Matrix.Identity(4);ob.matrix_basis=Matrix.Identity(4);ob.hide_render=False;ob.hide_set(False)
    for mod in ob.modifiers:
     if mod.type=='ARMATURE':mod.object=rig
  def label(text,location,size):
   data=bpy.data.curves.new(text,'FONT');data.body=text;data.size=size;data.materials.append(ink);ob=bpy.data.objects.new(text,data);scene.collection.objects.link(ob);ob.location=location;ob.rotation_euler=rotation.to_euler();return data
  label(clip,center-right*scale*1.45+up*scale*.535,scale*.034)
  time_label=label('time',center+right*scale*.99+up*scale*.535,scale*.026)
  for lod in range(3):
   e=a['lods'][lod];label(f'LOD{lod}  body {e["body_triangles"]} + outline {e["outline_triangles"]}',center+right*((lod-1)*scale-.45*scale)+up*scale*.465,scale*.027)
  fps=15;count=int(round(duration*fps))+1;folder=O/'Logs'/'MotionFrames'/clip;folder.mkdir(parents=True,exist_ok=True)
  sample_records=[];check_indices=sorted(set([0,count//4,count//2,3*count//4,count-1]))
  for k in range(count):
   t=duration if k==count-1 else min(duration,k/fps);frame=1+t*30;scene.frame_set(int(frame),subframe=frame-int(frame));bpy.context.view_layer.update();time_label.body=f'{t:05.2f}s / {duration:05.2f}s'
   path=folder/f'{k+1:04d}.png';scene.render.filepath=str(path);bpy.ops.render.render(write_still=True)
   if k in check_indices:
    destination=V/(clip+f'_Checkpoint_{len(sample_records)}.png');shutil.copy2(path,destination);sample_records.append({'time_s':t,'frame_index':k+1,'file':destination.name})
   if k%30==0:print('SSF_MOTION_FRAME',clip,k+1,count,flush=True)
  encode=bpy.data.scenes.new('Encode_'+clip);bpy.context.window.scene=encode;encode.render.resolution_x=1920;encode.render.resolution_y=768;encode.render.resolution_percentage=100;encode.render.fps=fps;encode.frame_start=1;encode.frame_end=count;encode.view_settings.view_transform='Standard';encode.view_settings.look='None'
  seq=encode.sequence_editor_create()
  for k in range(1,count+1):
   strip=seq.strips.new_image(f'ActualFrame_{k}',str(folder/f'{k:04d}.png'),1,k);strip.frame_final_duration=1
  encode.render.image_settings.media_type='VIDEO';encode.render.image_settings.file_format='FFMPEG';encode.render.ffmpeg.format='MPEG4';encode.render.ffmpeg.codec='H264';encode.render.ffmpeg.constant_rate_factor='HIGH';encode.render.filepath=str(video_file);bpy.ops.render.render(animation=True)
  result={'asset':key,'clip':clip,'source_duration_s':duration,'preview_fps':fps,'frames':count,'resolution':[1920,768],
   'includes_exact_start_and_end':True,'source_blend_sha256':blend_sha,'all_three_actual_LODs_shown':True,'freestyle':False,
   'source_bone_count':len(names),'camera_fixed_for_complete_motion':True,'panel_ortho_scale_m':scale,'full_30fps_motion_bounds_m':[list(lo),list(hi)],'checkpoints':sample_records,'file':video_file.name,'preview_duration_s':count/fps,'end_frame_hold_seconds':1/fps}
  record_file.write_text(json.dumps(result,indent=2));results.append(result);print('SSF_FULL_SOURCE_MOVIE_OK',clip,count,flush=True)
  bpy.context.window.scene=original_scene
  # Dispose only temporary scene objects and their encode strips; the model
  # datablocks loaded from the production file are never written back.
  for ob in list(scene.objects):bpy.data.objects.remove(ob,do_unlink=True)
  bpy.data.scenes.remove(encode);bpy.data.scenes.remove(scene)
(V/('animation_preview_manifest_'+args.assets.replace(',','_')+'.json' if args.assets else 'animation_preview_manifest.json')).write_text(json.dumps({'source_blend_sha256':blend_sha,'clips':results},indent=2))
print('SSF_NATIVE_MOTION_BATCH_DONE',len(results),flush=True)
