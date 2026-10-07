"""Read delivered MP4s through Blender's native decoder and save five checkpoints."""
import bpy,json,hashlib
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1';V=O/'AnimationPreviews';D=V/'Decoded';D.mkdir(exist_ok=True)
manifest=json.loads((V/'animation_preview_manifest.json').read_text());assert len(manifest['clips'])==22
expected_hash=hashlib.sha256((O/'SSF_Production_B_v1.blend').read_bytes()).hexdigest();assert expected_hash==manifest['source_blend_sha256']
results=[]
for clip in manifest['clips']:
 path=V/clip['file'];assert clip['source_blend_sha256']==expected_hash
 s=bpy.data.scenes.new('DeliveredReadback_'+clip['clip']);bpy.context.window.scene=s
 s.render.resolution_x=1920;s.render.resolution_y=768;s.render.resolution_percentage=100;s.render.fps=15;s.view_settings.view_transform='Standard';s.view_settings.look='None';s.render.image_settings.file_format='PNG';s.render.image_settings.color_mode='RGB'
 strip=s.sequence_editor_create().strips.new_movie('ActualDeliveredMP4',str(path),1,1)
 assert strip.frame_duration==clip['frames'],(clip['clip'],strip.frame_duration,clip['frames']);element=strip.elements[0];assert (element.orig_width,element.orig_height)==(1920,768)
 actual_fps=getattr(strip,'fps',None)
 if actual_fps is not None:assert abs(actual_fps-15)<.001
 checkpoints=[]
 for idx,checkpoint in enumerate(clip['checkpoints']):
  s.frame_set(checkpoint['frame_index']);output=D/(clip['clip']+f'_Decoded_{idx}.png');s.render.filepath=str(output);bpy.ops.render.render(write_still=True)
  checkpoints.append({'time_s':checkpoint['time_s'],'frame_index':checkpoint['frame_index'],'file':str(output.relative_to(V)),'native_source':checkpoint['file']})
 results.append({'clip':clip['clip'],'asset':clip['asset'],'file':clip['file'],'mp4_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'frame_count':strip.frame_duration,'native_decoder_fps':actual_fps,'resolution':[element.orig_width,element.orig_height],'decoded_checkpoints':checkpoints})
 bpy.data.scenes.remove(s);print('SSF_DELIVERED_MOVIE_READBACK_OK',clip['clip'],flush=True)
(V/'movie_readback_report.json').write_text(json.dumps({'source_blend_sha256':expected_hash,'all_22_delivered_movies_decoded':True,'clips':results},indent=2),encoding='utf8')
print('SSF_ALL_DELIVERED_MP4_READBACK_OK',len(results),flush=True)
