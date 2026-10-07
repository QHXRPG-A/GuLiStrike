"""Import the explicitly released B_v1 into owned formal resource packages only.
No level, gameplay Blueprint, data table, native code or source package is saved.
Run only in our -SSFStyleImportWorker editor.
"""
import unreal,json,hashlib,traceback,math,sys
from pathlib import Path
from fractions import Fraction
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');P=R/'Production_B_v1';D=R/'UE_Delivery_v1'
TARGET='/Game/GuLiStrike/Buildings/SSFStylized';OWNER='GuLi.SSFStylized.B_v1.20261006'
L=unreal.EditorAssetLibrary;T=unreal.AssetToolsHelpers.get_asset_tools();E=unreal.MaterialEditingLibrary
SK=unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem);ST=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
sys.path.insert(0,str(R/'Scripts'))
from ue_struct_text_v1 import parse as struct_parse,parts as struct_parts,split as struct_field
REPORT={'success':False,'stage':'begin','target':TARGET,'source_version':'SSF_Production_B_v1','assets':[],'animations':[],
        'saved_packages':[],'gameplay_integration':False,'maps_saved':[],'source_packages_modified':False,
        'budget_status':'24 disclosed body-cap differences retained in the specifically released version; no performance acceptance inferred'}
def checkpoint(): (D/'ue_import.json').write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf8')
def owned(path):
 assert path.startswith(TARGET+'/'),path
 if L.does_asset_exist(path):assert L.get_metadata_tag(unreal.load_asset(path),'GuLi.StyleOwner')==OWNER,('unowned existing asset',path)
def save(o):
 path=o.get_path_name();assert path.startswith(TARGET+'/'),path
 L.set_metadata_tag(o,'GuLi.StyleOwner',OWNER);L.set_metadata_tag(o,'GuLi.SourceVersion','SSF_Production_B_v1')
 L.set_metadata_tag(o,'GuLi.ReleaseRecord',str(R/'approval_B_import_20261006.json'))
 assert L.save_loaded_asset(o,False),path
 if path not in REPORT['saved_packages']:REPORT['saved_packages'].append(path)
 return o
def preview_mesh(o,mesh,source):
 # Remap existing protected preview soft references only inside our copied
 # package. Skeleton preview is duplicate-transient; its related formal mesh
 # is discoverable through the new mesh's Skeleton link and this metadata.
 T.rename_referencing_soft_object_paths([o.get_outer()],{unreal.SoftObjectPath(source):unreal.SoftObjectPath(mesh.get_path_name())})
 L.set_metadata_tag(o,'GuLi.PreviewMesh',mesh.get_path_name())
def create(path,cls,factory):
 owned(path)
 if L.does_asset_exist(path):return unreal.load_asset(path)
 folder,name=path.rsplit('/',1);L.make_directory(folder)
 o=T.create_asset(name,folder,cls,factory);assert o,path;return save(o)
def duplicate(source,path):
 owned(path)
 if L.does_asset_exist(path):return unreal.load_asset(path)
 o=L.duplicate_asset(source,path);assert o,(source,path)
 L.set_metadata_tag(o,'GuLi.SourceAsset',source);return save(o)
def imported(file,path,options=None):
 owned(path);folder,name=path.rsplit('/',1);L.make_directory(folder)
 task=unreal.AssetImportTask()
 for k,v in dict(filename=str(file),destination_path=folder,destination_name=name,automated=True,async_=False,
                 replace_existing=True,replace_existing_settings=True,save=False).items():task.set_editor_property(k,v)
 task.factory=unreal.FbxFactory() if options else unreal.TextureFactory()
 if options:task.options=options
 T.import_asset_tasks([task]);o=unreal.load_asset(path);assert o,(path,list(task.imported_object_paths))
 L.set_metadata_tag(o,'GuLi.StyleSourceSHA256',hashlib.sha256(Path(file).read_bytes()).hexdigest())
 L.set_metadata_tag(o,'GuLi.StyleOwner',OWNER);return o
def linear(code):
 vals=[int(code[i:i+2],16)/255 for i in (1,3,5)]
 return tuple(v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in vals)+(1.,)
def node(mat,cls,**props):
 o=E.create_material_expression(mat,cls)
 for k,v in props.items():o.set_editor_property(k,v)
 return o
def link(a,p,b,q):assert E.connect_material_expressions(a,p,b,q),(a,p,b,q)
def custom(mat,code,inputs,output=unreal.CustomMaterialOutputType.CMOT_FLOAT3):
 o=node(mat,unreal.MaterialExpressionCustom,code=code,output_type=output)
 slots=[]
 for name in inputs:
  s=unreal.CustomInput();s.set_editor_property('input_name',name);slots.append(s)
 o.set_editor_property('inputs',slots)
 for name,(src,pin) in inputs.items():link(src,pin,o,name)
 return o
def material(path,translucent=False):
 o=create(path,unreal.Material,unreal.MaterialFactoryNew());E.delete_all_material_expressions(o)
 o.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
 o.set_editor_property('used_with_skeletal_mesh',True);o.set_editor_property('two_sided',False)
 o.set_editor_property('blend_mode',unreal.BlendMode.BLEND_TRANSLUCENT if translucent else unreal.BlendMode.BLEND_OPAQUE)
 return o
def finish_material(o):E.layout_material_expressions(o);E.recompile_material(o);return save(o)
def body_master():
 m=material(TARGET+'/Shared/Materials/M_SSF_ThreeToneLine')
 normal=node(m,unreal.MaterialExpressionPixelNormalWS)
 light=node(m,unreal.MaterialExpressionVectorParameter,parameter_name='Art Light Direction',default_value=unreal.LinearColor(.35,.55,.76,0))
 ink=node(m,unreal.MaterialExpressionVectorParameter,parameter_name='Ink Color',default_value=unreal.LinearColor(*linear('#1A182F')))
 base=node(m,unreal.MaterialExpressionVectorParameter,parameter_name='Base Color',default_value=unreal.LinearColor(1,1,1,1))
 team=node(m,unreal.MaterialExpressionVectorParameter,parameter_name='Team Color',default_value=unreal.LinearColor(*linear('#EE9D58')))
 uv0=node(m,unreal.MaterialExpressionTextureCoordinate,coordinate_index=0)
 rg=node(m,unreal.MaterialExpressionTextureCoordinate,coordinate_index=2)
 bt=node(m,unreal.MaterialExpressionTextureCoordinate,coordinate_index=3)
 mask=node(m,unreal.MaterialExpressionTextureSampleParameter2D,parameter_name='Internal Line Mask',sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
 # Use an actual mask as the master's default so shader compilation is valid.
 mask.texture=unreal.load_asset(TARGET+'/AirBase/Textures/T_AirBase_InternalLineMask_2K')
 link(uv0,'',mask,'')
 val=custom(m,'float code=1.0-BT.y; float lod=floor(code+0.001); float teamMask=step(0.2,code-lod); float fade=lod<0.5?1.0:(lod<1.5?0.7:0.0); float3 palette=float3(RG.x,1.0-RG.y,BT.x); palette=lerp(palette,Team,teamMask); float d=dot(normalize(N),normalize(Light)); float band=d<0.38?0.40:(d<0.68?0.72:1.0); float inkMask=saturate(max(Mask.r*0.65,Mask.g)*fade); return lerp(palette*Base*band,Ink,inkMask);',
  {'RG':(rg,''),'BT':(bt,''),'Mask':(mask,'RGB'),'N':(normal,''),'Light':(light,'RGB'),'Base':(base,'RGB'),'Team':(team,'RGB'),'Ink':(ink,'RGB')})
 assert E.connect_material_property(val,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
 return finish_material(m)
def outline_material():
 m=material(TARGET+'/Shared/Materials/M_SSF_Outline')
 ink=node(m,unreal.MaterialExpressionVectorParameter,parameter_name='Ink Color',default_value=unreal.LinearColor(*linear('#1A182F')))
 assert E.connect_material_property(ink,'RGB',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
 return finish_material(m)
def display_master(light=False):
 name='M_SSF_LightDisplay' if light else 'M_SSF_SourceDisplay';m=material(TARGET+'/Shared/Materials/'+name,True)
 uv=node(m,unreal.MaterialExpressionTextureCoordinate,coordinate_index=1)
 tint=node(m,unreal.MaterialExpressionVectorParameter,parameter_name='Team Color',default_value=unreal.LinearColor(*linear('#EE9D58')))
 file='T_Light_SourceEmissionAlpha_1K' if light else 'T_SSF_Shared_SourceLogo_1K'
 tex=node(m,unreal.MaterialExpressionTextureSampleParameter2D,parameter_name='Source Display',texture=unreal.load_asset(TARGET+'/Shared/Textures/'+file),
  sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS if light else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
 link(uv,'',tex,'')
 opacity=custom(m,'return saturate(dot(Mask,float3(0.2126,0.7152,0.0722))*4.2);',{'Mask':(tex,'RGB')},unreal.CustomMaterialOutputType.CMOT_FLOAT1) if light else tex
 assert E.connect_material_property(opacity,'' if light else 'A',unreal.MaterialProperty.MP_OPACITY)
 assert E.connect_material_property(tint,'RGB',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
 return finish_material(m)
def instance(key,name,parent,theme,mask=None):
 m=create(TARGET+'/'+key+'/Materials/MI_'+key+'_'+name,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
 E.set_material_instance_parent(m,parent)
 E.set_material_instance_vector_parameter_value(m,'Base Color',unreal.LinearColor(1,1,1,1))
 E.set_material_instance_vector_parameter_value(m,'Team Color',unreal.LinearColor(*linear(theme['Accent'])))
 if mask:E.set_material_instance_texture_parameter_value(m,'Internal Line Mask',mask)
 E.update_material_instance(m);return save(m)
def fbx_options(skeletal,skeleton):
 ui=unreal.FbxImportUI();kind=unreal.FBXImportType.FBXIT_SKELETAL_MESH if skeletal else unreal.FBXImportType.FBXIT_STATIC_MESH
 for k,v in dict(import_materials=False,import_textures=False,import_mesh=True,import_as_skeletal=skeletal,import_animations=False,
  create_physics_asset=False,automated_import_should_detect_type=False,mesh_type_to_import=kind,original_import_type=kind).items():ui.set_editor_property(k,v)
 if skeletal:ui.skeleton=skeleton
 data=ui.skeletal_mesh_import_data if skeletal else ui.static_mesh_import_data
 props=dict(convert_scene=True,convert_scene_unit=True,force_front_x_axis=False,import_uniform_scale=1.,
  normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS_AND_TANGENTS,import_mesh_lo_ds=False)
 if skeletal:props.update(update_skeleton_reference_pose=False,use_t0_as_ref_pose=False,preserve_smoothing_groups=True)
 else:props.update(combine_meshes=True,auto_generate_collision=False,remove_degenerates=False,generate_lightmap_u_vs=False)
 for k,v in props.items():data.set_editor_property(k,v)
 return ui
def bone_contract(source,mesh):
 a=list(unreal.SkeletonService.list_bones(source.get_path_name()));b=list(unreal.SkeletonService.list_bones(mesh.get_path_name()))
 assert len(a)==len(b);dt=ds=da=0.
 for x,y in zip(a,b):
  assert x.bone_name==y.bone_name and x.parent_bone_name==y.parent_bone_name,(str(x.bone_name),str(y.bone_name))
  p,q=x.local_transform,y.local_transform;dt=max(dt,(p.translation-q.translation).length());ds=max(ds,(p.scale3d-q.scale3d).length())
  u,v=p.rotation,q.rotation;da=max(da,math.degrees(2*math.acos(min(1,abs(u.x*v.x+u.y*v.y+u.z*v.z+u.w*v.w)))))
 assert dt<.02 and ds<.001 and da<.1,(mesh.get_name(),'reference pose',dt,ds,da)
 return {'bones':len(a),'translation_error_cm':dt,'scale_error':ds,'angle_error_deg':da,'root_scale':list(b[0].local_transform.scale3d.to_tuple())}
def export_mesh(mesh,key,skin):
 out=D/'UE_Readback';out.mkdir(exist_ok=True);path=out/(key+'.fbx')
 opt=unreal.FbxExportOption();opt.ascii=False;opt.collision=False;opt.level_of_detail=True;opt.export_morph_targets=False
 opt.bake_material_inputs=unreal.FbxMaterialBakeMode.DISABLED
 task=unreal.AssetExportTask()
 for k,v in dict(object=mesh,filename=str(path),options=opt,automated=True,replace_identical=True,prompt=False,
  exporter=unreal.SkeletalMeshExporterFBX() if skin else unreal.StaticMeshExporterFBX()).items():task.set_editor_property(k,v)
 assert unreal.Exporter.run_asset_export_task(task),key
 return str(path)
def clone_animations(source,meshes,skeletons):
 mapping={}
 for row in source['animations']:
  old=unreal.load_asset(row['path']);key=next(k for k,s in skeletons.items() if meshes[k]['skeleton_source'].split('.')[0]==row['skeleton'].split('.')[0])
  dest=TARGET+'/'+key+'/Animations/'+old.get_name();owned(dest)
  if L.does_asset_exist(dest):
   prior=unreal.load_asset(dest)
   if prior.get_editor_property('skeleton')==skeletons[key]:new=prior
   else:
    # Only the failed, task-owned copy is replaced. Never delete a source asset.
    assert L.get_metadata_tag(prior,'GuLi.StyleOwner')==OWNER
    assert L.delete_asset(dest);new=None
  else:new=None
  if not new:
   factory=unreal.AnimSequenceFactory();factory.set_editor_property('target_skeleton',skeletons[key])
   folder,name=dest.rsplit('/',1);L.make_directory(folder);new=T.create_asset(name,folder,unreal.AnimSequence,factory);assert new
   # Copy every original raw local T/R/S key through engine extension getters
   # and the controller. Editable transform curves are copied separately.
   model=old.data_model_interface;count=model.get_number_of_keys();frames=model.get_number_of_frames()
   ctl=new.controller;ctl.open_bracket('Copy complete SSF source raw animation',False)
   try:
    rate=model.get_frame_rate();current=new.data_model_interface.get_frame_rate()
    # Some originals use non-integer rates. A common rational factor permits
    # the Sequencer controller to keep them without an enormous intermediate FPS.
    if (rate.numerator,rate.denominator)!=(current.numerator,current.denominator):
     fraction=Fraction(int(rate.numerator),int(rate.denominator));current_fraction=Fraction(int(current.numerator),int(current.denominator))
     bridge=Fraction(math.gcd(fraction.numerator,current_fraction.numerator),math.lcm(fraction.denominator,current_fraction.denominator))
     ctl.set_frame_rate(unreal.FrameRate(bridge.numerator,bridge.denominator),False)
     ctl.set_number_of_frames(unreal.FrameNumber(1),False)
    ctl.set_frame_rate(rate,False);ctl.set_number_of_frames(unreal.FrameNumber(frames),False)
    names=list(model.get_bone_track_names());tracks={str(name):[[],[],[]] for name in names}
    # Sequencer models intentionally return empty deprecated RawTrack structs.
    # Source evaluation selects base keys without additive editable curves.
    options=unreal.AnimPoseEvaluationOptions();options.set_editor_property('evaluation_type',unreal.AnimDataEvalType.SOURCE)
    for frame in range(count):
     pose=unreal.AnimPoseExtensions.get_anim_pose_at_frame(old,frame,options)
     for name in names:
      tr=pose.get_bone_pose(name,unreal.AnimPoseSpaces.LOCAL)
      for values,v in zip(tracks[str(name)],(tr.translation,tr.rotation,tr.scale3d)):values.append(v)
    for name in names:
     arrays=tracks[str(name)]
     assert ctl.add_bone_curve(name,False);assert ctl.set_bone_track_keys(name,*arrays,False)
    curve_data=model.get_editor_property('LegacyCurveData').export_text()
    for kind,curve_type in (('FloatCurves',unreal.RawCurveTrackTypes.RCT_FLOAT),('TransformCurves',unreal.RawCurveTrackTypes.RCT_TRANSFORM)):
     text=struct_field(curve_data,kind)
     for curve_text in struct_parts(text[1:-1]) if text and text!='()' else []:
      curve=struct_parse(curve_text);cid=unreal.AnimationCurveIdentifier();cid.set_curve_identifier(curve['CurveName'],curve_type)
      assert ctl.add_curve(cid,int(curve.get('CurveTypeFlags',4)),False)
      if 'Color' in curve:
       c=curve['Color'];ctl.set_curve_color(cid,unreal.LinearColor(c.get('R',0),c.get('G',0),c.get('B',0),c.get('A',1)),False)
      if 'Comment' in curve:ctl.set_curve_comment(cid,curve['Comment'],False)
      def copy_rich(identifier,rich_text):
       kt=struct_field(rich_text,'Keys');keys=[]
       for key_text in struct_parts(kt[1:-1]) if kt and kt!='()' else []:
        key=unreal.RichCurveKey();assert key.import_text(key_text);keys.append(key)
       if keys:assert ctl.set_curve_keys(identifier,keys,False)
      if kind=='FloatCurves':copy_rich(cid,struct_field(curve_text,'FloatCurve'))
      else:
       for field,channel in (('TranslationCurve',unreal.TransformCurveChannel.POSITION),('RotationCurve',unreal.TransformCurveChannel.ROTATION),('ScaleCurve',unreal.TransformCurveChannel.SCALE)):
        channel_text=struct_field(curve_text,field)
        for i,axis in enumerate((unreal.VectorCurveChannel.X,unreal.VectorCurveChannel.Y,unreal.VectorCurveChannel.Z)):
         child=cid.copy();assert child.get_transform_child_curve_identifier(channel,axis)
         copy_rich(child,struct_field(channel_text,'FloatCurves['+str(i)+']'))
    assert not list(model.get_editor_property('AnimatedBoneAttributes'))
   finally:ctl.close_bracket(False)
   for prop in ('rate_scale','interpolation','enable_root_motion','root_motion_root_lock','force_root_lock','bone_compression_settings','curve_compression_settings'):
    new.set_editor_property(prop,old.get_editor_property(prop))
   unreal.AnimationLibrary.copy_anim_notifies_from_sequence(old,new,True)
   new.set_preview_skeletal_mesh(unreal.load_asset(meshes[key]['path']))
   L.set_metadata_tag(new,'GuLi.SourceAsset',row['path']);L.set_metadata_tag(new,'GuLi.AnimationCopy','all original model frames, original rate and full local T/R/S; no skipped frames')
  # The wrapper is read-only, while its per-platform Default member is editable.
  # Change the owned struct member so the normal property-chain notification
  # resamples the copied animation at the original target rate.
  source_rate=old.get_editor_property('platform_target_frame_rate')
  target_rate=new.get_editor_property('platform_target_frame_rate')
  target_rate.set_editor_property('default',source_rate.get_editor_property('default'))
  assert new.get_editor_property('platform_target_frame_rate').export_text()==source_rate.export_text()
  save(new)
  assert new.get_editor_property('skeleton')==skeletons[key]
  length=old.get_editor_property('sequence_length');assert abs(new.get_editor_property('sequence_length')-length)<1e-6
  frames=unreal.AnimationLibrary.get_num_frames(old);assert unreal.AnimationLibrary.get_num_frames(new)==frames,(dest,frames,unreal.AnimationLibrary.get_num_frames(new))
  error=0.
  for bone in unreal.SkeletonService.list_bones(meshes[key]['path']):
   for f in (0,.25,.5,.75,1):
    a=unreal.AnimationLibrary.get_bone_pose_for_time(old,bone.bone_name,length*f,False)
    b=unreal.AnimationLibrary.get_bone_pose_for_time(new,bone.bone_name,length*f,False)
    qa=(a.rotation.x,a.rotation.y,a.rotation.z,a.rotation.w);qb=(b.rotation.x,b.rotation.y,b.rotation.z,b.rotation.w)
    qerr=min(max(abs(x-y) for x,y in zip(qa,qb)),max(abs(x+y) for x,y in zip(qa,qb)))
    error=max(error,(a.translation-b.translation).length(),(a.scale3d-b.scale3d).length(),qerr)
  # The pose reader/controller crosses double/float model boundaries. This
  # tolerance is 0.01 mm for translations; scale/quaternion data are verified
  # again through actual evaluated components after independent reload.
  assert error<.001,(dest,error)
  src_model=old.data_model_interface;dst_model=new.data_model_interface
  assert list(src_model.get_bone_track_names())==list(dst_model.get_bone_track_names()),dest
  source_curves=struct_parse(src_model.get_editor_property('LegacyCurveData').export_text())
  copied_curves=struct_parse(dst_model.get_editor_property('LegacyCurveData').export_text())
  # UE's controller changes colors only on float curves; transform-curve
  # swatches are editor UI metadata. All keys, flags and channels must match.
  curve_color_metadata_equal=source_curves==copied_curves
  for curves in (source_curves,copied_curves):
   for kind in ('FloatCurves','TransformCurves'):
    for curve in curves.get(kind) or []:curve.pop('Color',None)
  assert source_curves==copied_curves,(dest,'editable curve data changed',source_curves,copied_curves)
  assert not list(src_model.get_editor_property('AnimatedBoneAttributes')) and not list(dst_model.get_editor_property('AnimatedBoneAttributes'))
  mapping[row['path'].split('.')[0]]=dest
  REPORT['animations'].append({'source':row['path'],'path':dest,'key':key,'duration_s':length,'frames':frames,'sampled_all_bones_max_error':error,'skeleton':skeletons[key].get_path_name(),'editable_motion_curves_preserved':True,'editor_curve_swatch_metadata_equal':curve_color_metadata_equal,'float_curves':src_model.get_number_of_float_curves(),'transform_curves':src_model.get_number_of_transform_curves()})
  checkpoint()
 return mapping
def drone_blueprint(source,meshes,anims):
 original=source['blueprints'][0]['path'];path=TARGET+'/Drone/Blueprints/TB1_MoveDrone';bp=duplicate(original,path)
 sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);data=unreal.SubobjectDataBlueprintFunctionLibrary;changed=[]
 for h in sub.k2_gather_subobject_data_for_blueprint(bp):
  o=data.get_object(data.get_data(h))
  if not isinstance(o,unreal.SkeletalMeshComponent):continue
  assert o.get_path_name().startswith(bp.get_path_name()),o.get_path_name()
  o.modify();o.set_skeletal_mesh_asset(unreal.load_asset(meshes['Drone']['path']));o.set_editor_property('override_materials',[])
  ad=o.get_editor_property('animation_data');old=ad.get_editor_property('anim_to_play')
  if old and old.get_path_name().split('.')[0] in anims:
   ad.set_editor_property('anim_to_play',unreal.load_asset(anims[old.get_path_name().split('.')[0]]));o.set_editor_property('animation_data',ad)
  changed.append(o.get_path_name())
 pins=[]
 for graph in unreal.BlueprintService.list_graphs(path):
  for n in unreal.BlueprintService.get_nodes_in_graph(path,graph.graph_name):
   for pin in n.pins:
    if pin.is_connected or not pin.is_input or pin.pin_type!='object':continue
    if pin.pin_name in ('NewAnimToPlay','AnimToPlay','Animation','NewAnimation'):
     value=next(v for k,v in anims.items() if k.endswith('/TB1_Drone_Fly_Anim'))
    elif pin.pin_name in ('NewMesh','SkeletalMesh','NewSkeletalMesh'):value=meshes['Drone']['path']
    else:continue
    assert unreal.BlueprintService.set_node_pin_value(path,graph.graph_name,n.node_id,pin.pin_name,value)
    pins.append({'graph':graph.graph_name,'node':n.node_title,'pin':pin.pin_name,'value':value})
 unreal.BlueprintEditorLibrary.compile_blueprint(bp);save(bp)
 REPORT['drone_blueprint']={'path':path,'source':original,'components_updated':changed,'object_pins_updated':pins,'dependencies_pending_readback':True}
 assert changed,'Drone blueprint has no editable skeletal component'
def run():
 auth=json.loads((R/'approval_B_import_20261006.json').read_text(encoding='utf8'));assert auth['status']=='released_for_formal_UE_storage' and not auth['gameplay_integration_authorized']
 assert hashlib.sha256((P/'SSF_Production_B_v1.blend').read_bytes()).hexdigest()==auth['source_blend_sha256']
 export=json.loads((D/'export_report.json').read_text(encoding='utf8'));back=json.loads((D/'fbx_readback.json').read_text(encoding='utf8'));assert export['success'] and back['success']
 source=json.loads((R/'Source/source_manifest.json').read_text(encoding='utf8'))
 checks={(r['asset'],r['LOD']):r for r in back['checks']}
 assets={a['key']:a for a in export['assets']};skeletons={};textures={}
 for a in export['assets']:
  REPORT['stage']='textures_'+a['key'];checkpoint()
  images={}
  for role,row in a['textures'].items():
   file=P/row['file'];path=(TARGET+'/Shared/Textures/' if role in ('display','shared_display') else TARGET+'/'+a['key']+'/Textures/')+'T_'+file.stem
   if path not in textures:
    tex=imported(file,path);is_mask=role in ('line','functional','orm','display')
    tex.set_editor_property('srgb',not is_mask);tex.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS if is_mask else unreal.TextureCompressionSettings.TC_DEFAULT)
    tex.set_editor_property('address_x',unreal.TextureAddress.TA_CLAMP);tex.set_editor_property('address_y',unreal.TextureAddress.TA_CLAMP)
    textures[path]=save(tex)
   images[role]=textures[path]
  a['_images']=images
  if a['bone_count']:
   src=unreal.load_asset(a['skeleton_source']);dest=TARGET+'/'+a['key']+'/Meshes/'+src.get_name()
   skeletons[a['key']]=duplicate(a['skeleton_source'],dest)
 body=body_master();ink=outline_material();display=display_master();light=display_master(True)
 for a in export['assets']:
  key=a['key'];REPORT['stage']='mesh_'+key;checkpoint();print('SSF_IMPORT_BEGIN',key,flush=True)
  skin=bool(a['bone_count']);original=unreal.load_asset(a['source']);name=original.get_name() if skin else 'SM_TB1_'+key
  path=TARGET+'/'+key+'/Meshes/'+name;opts=fbx_options(skin,skeletons.get(key))
  mesh=imported(a['lods'][0]['fbx'],path,opts);sub=SK if skin else ST
  for row in a['lods']:
   assert row['sha256']==checks[(key,row['LOD'])]['fbx_sha256']
   if row['LOD']:assert sub.import_lod(mesh,row['LOD'],row['fbx'])==row['LOD'],(key,row['LOD'])
  assert sub.get_lod_count(mesh)==3
  if skin:
   infos=list(mesh.get_editor_property('source_models'))
   for i,info in enumerate(infos):
    screen=info.get_editor_property('screen_size');screen.set_editor_property('default',(1.,.10,.035)[i]);info.set_editor_property('screen_size',screen)
   mesh.set_editor_property('source_models',infos)
  else:
   assert ST.set_lod_screen_sizes(mesh,[1.,.10,.035])
  for lod in range(3):
   setting=sub.get_lod_build_settings(mesh,lod)
   for k,v in dict(recompute_normals=False,recompute_tangents=False,use_full_precision_u_vs=True).items():setting.set_editor_property(k,v)
   sub.set_lod_build_settings(mesh,lod,setting)
  mats={'SSF_Outline':ink}
  if key=='Light':mats['SSF_LightDisplay']=instance(key,'Display',light,a['theme'])
  else:
   mats['SSF_Body']=instance(key,'Body',body,a['theme'],a['_images']['line'])
   if 'shared_display' in a['_images']:mats['SSF_Display']=instance(key,'Display',display,a['theme'])
  slots=list(mesh.get_editor_property('materials' if skin else 'static_materials'))
  names=[]
  for slot in slots:
   name=str(slot.get_editor_property('imported_material_slot_name'));assert name in mats,(key,name,list(mats))
   slot.set_editor_property('material_interface',mats[name]);names.append(name)
  mesh.set_editor_property('materials' if skin else 'static_materials',slots)
  contract=bone_contract(original,mesh) if skin else {}
  if skin:
   assert mesh.skeleton==skeletons[key]
   physics=original.get_editor_property('physics_asset')
   if physics:
    copy=duplicate(physics.get_path_name(),TARGET+'/'+key+'/Meshes/'+physics.get_name());preview_mesh(copy,mesh,original.get_path_name());save(copy);mesh.set_editor_property('physics_asset',copy)
   preview_mesh(skeletons[key],mesh,original.get_path_name());save(skeletons[key])
  bounds=mesh.get_imported_bounds() if skin else mesh.get_bounds()
  actual=list((bounds.box_extent*2).to_tuple());want=[100*(a['lods'][0]['bounds_m']['max'][i]-a['lods'][0]['bounds_m']['min'][i]) for i in range(3)]
  assert max(abs(x-y) for x,y in zip(actual,want))<.1,(key,'dimensions',actual,want)
  save(mesh);fbx=export_mesh(mesh,key,skin)
  record={'key':key,'source':a['source'],'path':path,'type':mesh.get_class().get_name(),'skeleton_source':a['skeleton_source'],
   'skeleton':mesh.skeleton.get_path_name() if skin else None,'physics':mesh.get_editor_property('physics_asset').get_path_name() if skin and mesh.get_editor_property('physics_asset') else None,
   'LOD_count':3,'screen_sizes':[1.,.1,.035],'material_slots':names,'dimensions_cm':actual,'reference_contract':contract,
   'body_and_outline_triangles':[{'body':r['body_triangles'],'outline':r['outline_triangles']} for r in a['lods']],
   'UE_export_readback':fbx,'full_precision_UVs':True,'textures':[o.get_path_name() for o in a['_images'].values()]}
  if not skin:
   record['actual_triangles']=[mesh.get_num_triangles(i) for i in range(3)]
   assert record['actual_triangles']==[r['triangles'] for r in a['lods']],(key,record['actual_triangles'])
   assert all(ST.get_num_uv_channels(mesh,i)==4 for i in range(3))
  REPORT['assets'].append(record);checkpoint();print('SSF_IMPORT_DONE',key,flush=True)
 meshes={r['key']:r for r in REPORT['assets']};anims=clone_animations(source,meshes,skeletons)
 drone_blueprint(source,meshes,anims)
 assert len(REPORT['assets'])==10 and len(REPORT['animations'])==22
 REPORT.update(success=True,stage='import_saved_pending_independent_reload',textures=len(textures),skeletons=len(skeletons),engine=unreal.SystemLibrary.get_engine_version())
 checkpoint();print('SSF_FORMAL_STORAGE_IMPORT_COMPLETE',flush=True)
if __name__=='__main__':
 assert '-SSFStyleImportWorker' in unreal.SystemLibrary.get_command_line()
 world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world();flag='Interchange.FeatureFlags.Import.Enable';before=unreal.SystemLibrary.get_console_variable_int_value(flag)
 unreal.SystemLibrary.execute_console_command(world,flag+' 0')
 try:run()
 except Exception:REPORT['error']=traceback.format_exc();checkpoint();unreal.log_error(REPORT['error'])
 finally:unreal.SystemLibrary.execute_console_command(world,flag+' '+str(before));unreal.SystemLibrary.quit_editor()
