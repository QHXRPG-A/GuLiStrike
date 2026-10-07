"""Compare actual Blender-deformed vertices with full-source UE bone deltas."""
import bpy,json,hashlib
import numpy as np
from pathlib import Path
from mathutils import Matrix,Vector,Quaternion
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1'
file=O/'SSF_Production_B_v1.blend';bpy.ops.wm.open_mainfile(filepath=str(file))
report=json.loads((O/'construction_report.json').read_text(encoding='utf8'));source=json.loads((R/'Source/source_manifest.json').read_text(encoding='utf8'));sources={a['key']:a for a in source['meshes']}
S=Matrix.Diagonal((1.,-1.,1.,1.))
def ue(v):
 if isinstance(v,dict):t,q,s=v['translation_cm'],v['rotation_xyzw'],v['scale']
 else:t,q,s=v[:3],v[3:7],v[7:10]
 return S@Matrix.LocRotScale(Vector(t)*.01,Quaternion((q[3],q[0],q[1],q[2])),Vector(s))@S
results=[]
for a in report['assets']:
 if not a['rig']:continue
 key=a['key'];scene=bpy.data.scenes[a['scene']];bpy.context.window.scene=scene;bpy.context.view_layer.update();rig=bpy.data.objects[a['rig']];rig.data.pose_position='POSE'
 bones=sources[key]['bones'];names=[b['name'] for b in bones];parents={b['name']:b['parent'] for b in bones};inverse={b['name']:ue(b['global']).inverted() for b in bones}
 bases=[]
 for e in a['lods']:
  ob=bpy.data.objects[e['body']];pts=np.array([(*v.co,1.) for v in ob.data.vertices]);groups={n:[] for n in names};weights={n:[] for n in names}
  for v in ob.data.vertices:
   for g in v.groups:
    if g.weight>0:
     n=ob.vertex_groups[g.group].name;groups[n].append(v.index);weights[n].append(g.weight)
  bases.append((ob,pts,{n:(np.array(ids,dtype=int),np.array(weights[n])) for n,ids in groups.items() if ids}))
 for clip in a['animations']:
  raw=json.loads((O/'AnimationSource'/(clip+'_FullPose.json')).read_text());action=bpy.data.actions[clip];rig.animation_data.action=action;rig.animation_data.action_slot=action.slots[0]
  checkpoints=[]
  for i in sorted(set([0,len(raw['frames'])//4,len(raw['frames'])//2,3*len(raw['frames'])//4,len(raw['frames'])-1])):
   sample=raw['frames'][i];t=sample['time_s'];f=1+t*30;scene.frame_set(int(f),subframe=f-int(f));bpy.context.view_layer.update();world={}
   for j,n in enumerate(names):world[n]=world[parents[n]]@ue(sample['local_tqs'][j]) if parents[n] else ue(sample['local_tqs'][j])
   errors=[];outline_finite=[]
   for lod,(ob,pts,groups) in enumerate(bases):
    expected=np.zeros((len(pts),4))
    for n,(indices,weights) in groups.items():expected[indices]+=(pts[indices]@np.array(world[n]@inverse[n],dtype=float).T)*weights[:,None]
    ev=ob.evaluated_get(bpy.context.evaluated_depsgraph_get());m=ev.to_mesh();actual=np.array([v.co[:] for v in m.vertices]);assert len(actual)==len(pts) and np.isfinite(actual).all();ev.to_mesh_clear()
    error=float(np.max(np.linalg.norm(actual-expected[:,:3],axis=1)));assert error<.0002,(clip,t,lod,error);errors.append(error)
    if a['lods'][lod]['outline']:
     out=bpy.data.objects[a['lods'][lod]['outline']].evaluated_get(bpy.context.evaluated_depsgraph_get());mesh=out.to_mesh();ok=np.isfinite(np.array([v.co[:] for v in mesh.vertices])).all();out.to_mesh_clear();assert ok;outline_finite.append(True)
   checkpoints.append({'time_s':t,'LOD_vertex_max_error_m':errors,'all_deformed_outline_vertices_finite':all(outline_finite)})
  results.append({'asset':key,'clip':clip,'five_actual_motion_checkpoints':checkpoints,'every_body_vertex_compared_with_source_bone_delta':True,'all_three_LODs':True});print('SSF_NATIVE_DEFORM_CLIP_OK',clip,max(max(c['LOD_vertex_max_error_m']) for c in checkpoints),flush=True)
 rig.animation_data.action=None
result={'source_blend_sha256':hashlib.sha256(file.read_bytes()).hexdigest(),'all_22_clips':len(results)==22,'every_clip_three_LODs':True,'clip_checks':results,'maximum_deformed_vertex_error_m':max(max(max(c['LOD_vertex_max_error_m']) for c in r['five_actual_motion_checkpoints']) for r in results),'UE_import_roundtrip_not_yet_run':True}
(O/'animation_deformation_validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8');assert result['all_22_clips'];print('SSF_ACTUAL_ANIMATION_DEFORMATION_OK',result['maximum_deformed_vertex_error_m'],flush=True)
