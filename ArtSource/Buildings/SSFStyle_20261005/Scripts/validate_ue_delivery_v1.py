"""Independent reload, dependency, real animation-component and render verification.
Only unsaved temporary actors in /Engine/Maps/Entry; no map is saved.
"""
import unreal,json,time,traceback,math
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');D=R/'UE_Delivery_v1';V=D/'Preview';V.mkdir(exist_ok=True)
TARGET='/Game/GuLiStrike/Buildings/SSFStylized';OWNER='GuLi.SSFStylized.B_v1.20261006'
REPORT={'success':False,'independent_process_reload':True,'maps_saved':[],'gameplay_integrated':False,'assets':[],'component_animation_checks':[],'captures':[]}
def dump(): (D/'ue_validation.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
def tr_error(a,b):
 q,r=a.rotation,b.rotation;qa=(q.x,q.y,q.z,q.w);qb=(r.x,r.y,r.z,r.w)
 dot=abs(sum(x*y for x,y in zip(qa,qb)))/(sum(x*x for x in qa)*sum(x*x for x in qb))**.5
 return ((a.translation-b.translation).length(),(a.scale3d-b.scale3d).length(),math.degrees(2*math.acos(min(1,dot))))

def original_runtime_sampling_pose(anim,mesh,bones,t):
 # Runtime uses the original target sampling rate, which is 1 FPS on the
 # Strategy idle and 2 FPS on two Factory clips. Resample editable keys at
 # that rate and interpolate local quaternions as the runtime codec does.
 # Comparing 1-FPS playback to continuous editor Euler curves gives a false
 # 180-degree mismatch even for the unmodified original animation.
 rate=anim.get_editor_property('platform_target_frame_rate').get_editor_property('default')
 fps=rate.numerator/rate.denominator;model=anim.data_model_interface;mr=model.get_frame_rate()
 count=int(math.floor(model.get_number_of_frames()*fps/(mr.numerator/mr.denominator)+1e-7))
 frame=min(max(t*fps,0.),float(count));low=int(math.floor(frame));high=min(low+1,count);alpha=frame-low
 if anim.get_editor_property('interpolation')==unreal.AnimInterpolationType.STEP:alpha=0.
 options=unreal.AnimPoseEvaluationOptions();options.evaluation_type=unreal.AnimDataEvalType.RAW
 options.optional_skeletal_mesh=mesh;options.incorporate_root_motion_into_pose=False
 a=unreal.AnimPoseExtensions.get_anim_pose_at_time(anim,low/fps,options)
 b=a if high==low else unreal.AnimPoseExtensions.get_anim_pose_at_time(anim,high/fps,options)
 world={}
 for bone in bones:
  x=a.get_bone_pose(bone.bone_name,unreal.AnimPoseSpaces.LOCAL);y=b.get_bone_pose(bone.bone_name,unreal.AnimPoseSpaces.LOCAL)
  q,r=x.rotation,y.rotation;qa=(q.x,q.y,q.z,q.w);qb=(r.x,r.y,r.z,r.w)
  sign=1. if sum(i*j for i,j in zip(qa,qb))>=0 else -1.
  v=[i*(1-alpha)+j*alpha*sign for i,j in zip(qa,qb)];length=sum(i*i for i in v)**.5
  local=unreal.Transform();local.translation=x.translation*(1-alpha)+y.translation*alpha
  local.scale3d=x.scale3d*(1-alpha)+y.scale3d*alpha;local.rotation=unreal.Quat(*[i/length for i in v])
  parent=str(bone.parent_bone_name);world[str(bone.bone_name)]=unreal.MathLibrary.compose_transforms(local,world[parent]) if parent else local
 return world,{'target_FPS':fps,'sample_key_times_s':[low/fps,high/fps],'interpolation_alpha':alpha}
def main():
 assert '-SSFStyleValidateWorker' in unreal.SystemLibrary.get_command_line()
 im=json.loads((D/'ue_import.json').read_text(encoding='utf8'));assert im['success']
 previous=json.loads((D/'ue_validation.json').read_text(encoding='utf8')) if '-SSFReusePoseValidation' in unreal.SystemLibrary.get_command_line() else None
 if previous:
  assert previous['success'] and len(previous['component_animation_checks'])==198
  REPORT['component_animation_checks']=previous['component_animation_checks'];REPORT['pose_evidence_reused_after_material_only_repair']=True
 unreal.EditorPythonScripting.set_keep_python_script_alive(True)
 reg=unreal.AssetRegistryHelpers.get_asset_registry();reg.wait_for_completion();reg.scan_paths_synchronous([TARGET],force_rescan=True)
 lib=unreal.EditorAssetLibrary;source=json.loads((R/'Source/source_manifest.json').read_text(encoding='utf8'))
 sk=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem);st=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
 options=unreal.AssetRegistryDependencyOptions(include_hard_package_references=True,include_soft_package_references=True,
   include_searchable_names=False,include_hard_management_references=False,include_soft_management_references=False)
 dependencies={};unexpected=[];inventory=[];classes={}
 for data in reg.get_assets_by_path(TARGET,recursive=True):
  o=data.get_asset();assert lib.get_metadata_tag(o,'GuLi.StyleOwner')==OWNER,o.get_path_name()
  cls=o.get_class().get_name();classes[cls]=classes.get(cls,0)+1
  inventory.append({'path':o.get_path_name(),'class':cls})
  deps=[str(p) for p in reg.get_dependencies(data.package_name,options)];dependencies[str(data.package_name)]=deps
  unexpected.extend((str(data.package_name),p) for p in deps if p.startswith('/Game/Assets/SSF_Buildings'))
 REPORT['asset_count']=len(dependencies);REPORT['dependencies']=dependencies;REPORT['source_dependencies']=unexpected
 REPORT['asset_inventory']=inventory;REPORT['asset_classes']=classes
 dump()
 assert not unexpected,('formal assets still reference original package',unexpected)
 for a in im['assets']:
  mesh=unreal.load_asset(a['path']);skin=isinstance(mesh,unreal.SkeletalMesh);sub=sk if skin else st
  assert sub.get_lod_count(mesh)==3
  if skin:
   bones=unreal.SkeletonService.list_bones(a['path']);src=unreal.SkeletonService.list_bones(a['source']);assert len(bones)==len(src)
   maxerr=[0,0,0]
   for b,c in zip(bones,src):
    assert b.bone_name==c.bone_name and b.parent_bone_name==c.parent_bone_name
    maxerr=[max(x,y) for x,y in zip(maxerr,tr_error(b.local_transform,c.local_transform))]
   assert maxerr[0]<.02 and maxerr[1]<.001 and maxerr[2]<.1,(a['key'],maxerr)
   screens=[p.get_editor_property('screen_size').get_editor_property('default') for p in mesh.get_editor_property('source_models')]
  else:screens=list(st.get_lod_screen_sizes(mesh));assert [mesh.get_num_triangles(i) for i in range(3)]==a['actual_triangles']
  assert max(abs(x-y) for x,y in zip(screens,(1.,.10,.035)))<1e-6,(a['key'],screens)
  for i in range(3):
   settings=sub.get_lod_build_settings(mesh,i)
   assert settings.get_editor_property('use_full_precision_u_vs') and not settings.get_editor_property('recompute_normals')
  REPORT['assets'].append({'key':a['key'],'path':a['path'],'reload_success':True,'LOD_count':3,'screen_sizes':screens,'physics':a['physics']})
 actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem);world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
 assert world.get_path_name().startswith('/Engine/Maps/Entry')
 for actor in actors.get_all_level_actors():actor.set_is_temporarily_hidden_in_editor(True)
 def spawn(cls,label):
  actor=actors.spawn_actor_from_class(cls,unreal.Vector());assert actor;actor.set_actor_label('SSF_ResourceReview_'+label);return actor
 source_actor=spawn(unreal.SkeletalMeshActor,'SourcePose');source_comp=source_actor.get_component_by_class(unreal.SkeletalMeshComponent)
 dest_actor=spawn(unreal.SkeletalMeshActor,'FormalPose');dest_comp=dest_actor.get_component_by_class(unreal.SkeletalMeshComponent)
 for c in (source_comp,dest_comp):
  c.set_update_animation_in_editor(True);c.set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
  c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
 source_actor.set_is_temporarily_hidden_in_editor(True)
 jobs=[];assets={a['key']:a for a in im['assets']}
 for anim in im['animations']:
  for lod in range(3):
   for fraction in (0,.5,1.):jobs.append((anim,lod,anim['duration_s']*fraction))
 # Camera is only a temporary entry-world preview actor.
 camera=spawn(unreal.CameraActor,'Camera');cc=camera.get_component_by_class(unreal.CameraComponent);cc.set_field_of_view(25)
 pp=unreal.PostProcessSettings()
 for k,v in dict(override_auto_exposure_method=True,auto_exposure_method=unreal.AutoExposureMethod.AEM_MANUAL,
  override_auto_exposure_apply_physical_camera_exposure=True,auto_exposure_apply_physical_camera_exposure=False,
  override_auto_exposure_bias=True,auto_exposure_bias=0,override_bloom_intensity=True,bloom_intensity=0,
  override_motion_blur_amount=True,motion_blur_amount=0).items():pp.set_editor_property(k,v)
 cc.set_editor_property('post_process_settings',pp);cc.set_editor_property('post_process_blend_weight',1)
 for cmd in ('r.Streaming.FullyLoadUsedTextures 1','r.Streaming.PoolSize 2500','r.ScreenPercentage 100','r.AntiAliasingMethod 1','ShowFlag.Tonemapper 0'):
  unreal.SystemLibrary.execute_console_command(world,cmd)
 unreal.ViewportService.set_view_mode('lit');unreal.ViewportService.set_realtime(True);unreal.ViewportService.set_exposure(True,0)
 # An unsaved neutral backdrop makes the actual outline visible in previews.
 # It belongs only to this discarded Entry world, not to the delivery assets.
 background=spawn(unreal.StaticMeshActor,'NeutralBackdrop')
 background_comp=background.get_component_by_class(unreal.StaticMeshComponent)
 background_comp.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Plane'))
 background_comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
 background_material=unreal.new_object(unreal.Material,name='SSF_UnsavedNeutralBackdrop')
 background_material.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
 background_color=unreal.MaterialEditingLibrary.create_material_expression(background_material,unreal.MaterialExpressionConstant3Vector)
 background_color.set_editor_property('constant',unreal.LinearColor(.8796,.85499,.80695,1))
 assert unreal.MaterialEditingLibrary.connect_material_property(background_color,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
 unreal.MaterialEditingLibrary.recompile_material(background_material);background_comp.set_material(0,background_material)
 subjects={}
 for a in im['assets']:
  mesh=unreal.load_asset(a['path']);actor=spawn(unreal.SkeletalMeshActor if a['type']=='SkeletalMesh' else unreal.StaticMeshActor,a['key'])
  comp=actor.get_component_by_class(unreal.SkeletalMeshComponent if a['type']=='SkeletalMesh' else unreal.StaticMeshComponent)
  if a['type']=='SkeletalMesh':
   comp.set_skeletal_mesh_asset(mesh);comp.set_update_animation_in_editor(True)
   comp.set_animation_mode(unreal.AnimationMode.ANIMATION_SINGLE_NODE)
   comp.set_editor_property('visibility_based_anim_tick_option',unreal.VisibilityBasedAnimTickOption.ALWAYS_TICK_POSE_AND_REFRESH_BONES)
  else:comp.set_static_mesh(mesh)
  comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION);subjects[a['key']]=(actor,comp)
  actor.set_is_temporarily_hidden_in_editor(True)
 captures=[{'key':a['key'],'LOD':lod,'name':a['key']+'_LOD'+str(lod),'fov':25.,'distance_cm':max(a['dimensions_cm'])*(4.2 if a['key']=='Reactor' else 3.2),'purpose':'same_camera_LOD_comparison'} for a in im['assets'] for lod in range(3)]
 for a in im['assets']:
  if a['key'] not in ('AirBase','CloningCenter','CommandCenter','MilitaryFactory','Reactor','StrategyCenter'):continue
  for label,metres,fov in (('Near_35m',35.,25.),('Tactical_300m',300.,55.),('Tactical_700m',700.,55.),('Overview_1500m',1500.,55.)):
   captures.append({'key':a['key'],'LOD':None,'name':a['key']+'_'+label,'fov':fov,'distance_cm':metres*100,'purpose':'project_distance_auto_LOD'})
 if '-SSFAutoLODRefreshOnly' in unreal.SystemLibrary.get_command_line():
  assert previous
  REPORT['captures']=[c for c in previous['captures'] if c['LOD'] is not None]
  captures=[c for c in captures if c['LOD'] is None]
 if '-SSFManualLODRefreshOnly' in unreal.SystemLibrary.get_command_line():
  assert previous
  REPORT['captures']=[c for c in previous['captures'] if c['LOD'] is None]
  captures=[c for c in captures if c['LOD'] is not None]
 if '-SSFReactorFramingRefreshOnly' in unreal.SystemLibrary.get_command_line():
  assert previous
  REPORT['captures']=[c for c in previous['captures'] if c['key']!='Reactor' or c['LOD'] is None]
  captures=[c for c in captures if c['key']=='Reactor' and c['LOD'] is not None]
 state={'phase':'capture' if previous else 'poses','index':0,'configured':False,'warm':0,'task':None,'started':time.monotonic()};pose_cache={}
 if previous:dest_actor.set_is_temporarily_hidden_in_editor(True)
 def finish():
  log=(D/'ue_validation.log').read_text(encoding='utf8',errors='replace')
  assert 'Failed to compile Material '+TARGET not in log,'A formal material fell back to the default shader'
  assert len(REPORT['captures'])==54 and len({c['name'] for c in REPORT['captures']})==54
  for capture in REPORT['captures']:
   if capture['LOD'] is not None and 'component_predicted_LOD' in capture:
    assert capture['LOD']==capture['component_predicted_LOD'],('Manual LOD evidence mismatch',capture['name'])
  REPORT.update(success=True,engine=unreal.SystemLibrary.get_engine_version(),animation_count=22,pose_LOD_cases=len(jobs),
                rendered_all_10_assets_all_3_LODs=True,temporary_entry_world_discarded=True)
  dump();unreal.SystemLibrary.quit_editor()
 def tick(delta):
  try:
   if time.monotonic()-state['started']>600:raise RuntimeError('Validation worker exceeded ten minutes')
   if state['phase']=='poses':
    if state['index']==len(jobs):
     source_actor.set_is_temporarily_hidden_in_editor(True);dest_actor.set_is_temporarily_hidden_in_editor(True)
     state.update(phase='capture',index=0,configured=False);dump();return
    anim,lod,t=jobs[state['index']];a=assets[anim['key']]
    if not state['configured']:
     source_comp.set_skeletal_mesh_asset(unreal.load_asset(a['source']));dest_comp.set_skeletal_mesh_asset(unreal.load_asset(a['path']))
     source_comp.set_forced_lod(1);dest_comp.set_forced_lod(lod+1)
     for c,path in ((source_comp,anim['source']),(dest_comp,anim['path'])):
      c.set_animation_mode(unreal.AnimationMode.ANIMATION_BLUEPRINT);c.override_animation_data(unreal.load_asset(path),False,False,t,0.)
     state.update(configured=True,warm=.08);return
    state['warm']-=delta
    if state['warm']>0:return
    assert dest_comp.get_anim_instance() and abs(dest_comp.get_position()-t)<.001
    errors=[0.,0.,0.];worst=['','',''];original_compressed_error=[0.,0.,0.]
    # The approved Blender motion was built from the editable original keys.
    # Compare actual new components to that source RAW pose. Separately record
    # original lossy component differences: recompressing exact keys need not
    # reproduce the original codec's quantization noise (e.g. 0.15 degree).
    bones=unreal.SkeletonService.list_bones(a['path']);cache_key=(anim['source'],t)
    if cache_key not in pose_cache:pose_cache[cache_key]=original_runtime_sampling_pose(unreal.load_asset(anim['source']),unreal.load_asset(a['source']),bones,t)
    approved_pose,sampling=pose_cache[cache_key]
    for b in bones:
     x=source_comp.get_socket_transform(b.bone_name,unreal.RelativeTransformSpace.RTS_COMPONENT)
     y=dest_comp.get_socket_transform(b.bone_name,unreal.RelativeTransformSpace.RTS_COMPONENT)
     raw=approved_pose[str(b.bone_name)]
     current=tr_error(raw,y)
     original_compressed_error=[max(i,j) for i,j in zip(original_compressed_error,tr_error(x,y))]
     worst=[str(b.bone_name) if j>i else w for i,j,w in zip(errors,current,worst)]
     errors=[max(i,j) for i,j in zip(errors,current)]
    assert errors[0]<.15 and errors[1]<.005 and errors[2]<.15,(anim['path'],lod,t,errors,worst)
    REPORT['component_animation_checks'].append({'animation':anim['path'],'LOD':lod,'time_s':t,
     'baseline':'original editable RAW pose at original runtime target rate with local quaternion interpolation; actual new compressed component',
     'original_runtime_sampling':sampling,
     'all_bones_error_translation_cm_scale_angle_deg':errors,'original_lossy_component_difference_cm_scale_angle_deg':original_compressed_error,
     'instance_seconds':dest_comp.get_position()})
    state.update(index=state['index']+1,configured=False);return
   if state['task']:
    if not state['task'].is_task_done():return
    spec=captures[state['index']];name=spec['name']+'.png';assert (V/name).exists(),name
    _,comp=subjects[spec['key']];record=dict(spec,path=str(V/name))
    if isinstance(comp,unreal.SkeletalMeshComponent):
     record['component_predicted_LOD']=comp.get_predicted_lod_level()
     if spec['LOD'] is not None:assert record['component_predicted_LOD']==spec['LOD'],('Rendered LOD differs from requested',spec['name'],record['component_predicted_LOD'])
    REPORT['captures'].append(record);state.update(index=state['index']+1,configured=False,task=None);dump()
   if state['index']==len(captures):unreal.unregister_slate_post_tick_callback(handle);finish();return
   spec=captures[state['index']];key,lod=spec['key'],spec['LOD'];actor,comp=subjects[key]
   if not state['configured']:
    for other,_ in subjects.values():other.set_is_temporarily_hidden_in_editor(other!=actor)
    forced=lod+1 if lod is not None else 0
    if isinstance(comp,unreal.SkeletalMeshComponent):comp.set_forced_lod(forced)
    else:comp.set_forced_lod_model(forced)
    mesh=unreal.load_asset(assets[key]['path']);bounds=mesh.get_imported_bounds() if isinstance(mesh,unreal.SkeletalMesh) else mesh.get_bounds()
    target=bounds.origin;distance=spec['distance_cm'];cc.set_field_of_view(spec['fov']);assert unreal.ViewportService.set_fov(spec['fov'])
    offset=unreal.Vector(.76,.98,.74);offset=offset/offset.length();position=target+offset*distance
    background.set_actor_location(target-offset*max(assets[key]['dimensions_cm'])*1.1,False,False)
    background.set_actor_rotation(unreal.MathLibrary.make_rot_from_z(offset),False)
    background.set_actor_scale3d(unreal.Vector(distance*.04,distance*.04,1))
    rotation=unreal.MathLibrary.find_look_at_rotation(position,target);camera.set_actor_location(position,False,False);camera.set_actor_rotation(rotation,False)
    cc.set_editor_property('aspect_ratio',1.);unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(position,rotation)
    state.update(configured=True,warm=10. if state['index']==0 else 1.);return
   state['warm']-=delta
   if state['warm']>0:return
   state['task']=unreal.AutomationLibrary.take_high_res_screenshot(2048,2048,str(V/(spec['name']+'.png')),camera,delay=.1)
  except Exception:
   unreal.unregister_slate_post_tick_callback(handle);REPORT['error']=traceback.format_exc();dump();unreal.log_error(REPORT['error']);unreal.SystemLibrary.quit_editor()
 handle=unreal.register_slate_post_tick_callback(tick)
 dump()
if __name__=='__main__':
 try:main()
 except Exception:REPORT['error']=traceback.format_exc();dump();unreal.log_error(REPORT['error']);unreal.SystemLibrary.quit_editor()
