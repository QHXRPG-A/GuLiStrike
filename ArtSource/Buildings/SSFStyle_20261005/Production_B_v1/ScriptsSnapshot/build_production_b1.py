"""Build SSF source-faithful editable parts, three safe LODs and restored rigs.

Approval is resolved by hash. Every connected mechanical component survives every
body LOD; simplification is rejected when it alters its closed topology, silhouette
or surface beyond the recorded per-part tolerance. Budget exceptions are reported.
"""
import bpy,bmesh,json,math,sys,argparse
import numpy as np
from pathlib import Path
from collections import defaultdict,Counter
from mathutils import Vector,Matrix,Quaternion
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1'
parser=argparse.ArgumentParser();parser.add_argument('--assets',default='');args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
assert not (O/'production_manifest.json').exists(),'Published production version is frozen'
approval=json.loads((R/'approval_A.json').read_text(encoding='utf8'))
assert approval['status']=='approved' and approval['approved_version']=='SSF_Reference_A_v7'
source=json.loads((R/'Source/source_manifest.json').read_text(encoding='utf8'))
baseline=json.loads((R/'Baseline/blender_source_manifest.json').read_text())
reference=json.loads((R/'References_A_v7/render_manifest.json').read_text(encoding='utf8'))
sources={m['key']:m for m in source['meshes']};refs={m['key']:m for m in reference['assets']}
CAPS={'AirBase':[(4300,700),(2300,400),(1050,0)],'CloningCenter':[(2200,300),(1100,200),(500,0)],
 'CommandCenter':[(3800,600),(1900,300),(850,0)],'MilitaryFactory':[(1800,250),(900,150),(400,0)],
 'Reactor':[(3000,500),(1500,250),(700,0)],'StrategyCenter':[(3600,600),(1800,300),(800,0)],
 'Floor':[(6500,1000),(3500,500),(1500,0)],'Drone':[(210,40),(110,20),(50,0)],
 'Lamp':[(48,0)]*3,'Light':[(2,0)]*3}
bpy.ops.wm.open_mainfile(filepath=str(R/'References_A_v7/SSF_ReferenceDesign_v7.blend'))
report={'version':'SSF_Production_B_v1','stage':'native_geometry_construction',
 'approved_reference':approval,'blender_version':bpy.app.version_string,'assets':[],
 'source_assets_modified':False,'UE_formal_import_performed':False}

def tri_count(me):me.calc_loop_triangles();return len(me.loop_triangles)
def tree(me):return BVHTree.FromPolygons([v.co for v in me.vertices],[list(p.vertices) for p in me.polygons],all_triangles=False)
def nonmanifold(me):
 bm=bmesh.new();bm.from_mesh(me);n=sum(not e.is_manifold for e in bm.edges);bm.free();return n
def surface_error(a,b):
 at,bt=tree(a),tree(b)
 points_a=[v.co for v in a.vertices]+[p.center for p in a.polygons]
 points_b=[v.co for v in b.vertices]+[p.center for p in b.polygons]
 def max_distance(points,target):
  distances=[target.find_nearest(v)[3] for v in points]
  return max(x for x in distances if x is not None) if distances else float('inf')
 return max(max_distance(points_a,bt),max_distance(points_b,at))
def weld_weight_compatible(me):
 bm=bmesh.new();bm.from_mesh(me);deform=bm.verts.layers.deform.active;bins=defaultdict(list)
 for v in bm.verts:bins[tuple(sorted((i,round(w,6)) for i,w in v[deform].items())) if deform else ()].append(v)
 for vertices in bins.values():
  if len(vertices)>1:bmesh.ops.remove_doubles(bm,verts=vertices,dist=.000001)
 bmesh.ops.dissolve_degenerate(bm,dist=.0000001,edges=list(bm.edges))
 bmesh.ops.dissolve_limit(bm,angle_limit=.001,verts=list(bm.verts),edges=list(bm.edges),use_dissolve_boundaries=False,delimit={'MATERIAL'})
 bm.to_mesh(me);bm.free();me.update()
def symmetric_x(me,tolerance):
 kd=KDTree(len(me.vertices))
 for v in me.vertices:kd.insert(v.co,v.index)
 kd.balance()
 return max(kd.find(Vector((-v.co.x,v.co.y,v.co.z)))[2] for v in me.vertices)<=tolerance
def setup_scene(key,old):
 s=bpy.data.scenes.new('Production_'+key);bpy.context.window.scene=s
 s.world=old.world.copy();s.render.engine='BLENDER_EEVEE';s.eevee.taa_render_samples=48
 s.render.resolution_x=s.render.resolution_y=2048;s.render.resolution_percentage=100
 s.render.use_freestyle=False;s.view_settings.view_transform='Standard';s.view_settings.look='None'
 s.view_settings.exposure=0;s.view_settings.gamma=1;s.render.image_settings.file_format='PNG';s.render.image_settings.color_mode='RGBA'
 s.render.fps=30;s.unit_settings.system='METRIC';s.unit_settings.scale_length=1
 cam=old.camera.copy();cam.data=old.camera.data.copy();cam.name='ProductionCamera_'+key;s.collection.objects.link(cam);s.camera=cam
 return s
def reconstruct_rig(key,old,scene,old_world):
 info=sources[key];data=bpy.data.armatures.new('SSF_'+key+'_SourceCompatible');rig=bpy.data.objects.new('Rig_'+key,data);scene.collection.objects.link(rig)
 bpy.context.view_layer.objects.active=rig;rig.select_set(True);bpy.ops.object.mode_set(mode='EDIT')
 for item in info['bones']:
  b=data.edit_bones.new(item['name'])
  if item['name']=='Root':
   m=old_world.copy();rot=m.to_3x3().normalized().to_4x4();rot.translation=m.translation;b.matrix=rot;b.length=.5
  else:
   src=old.data.bones[item['name']];head=old_world@src.head_local;tail=old_world@src.tail_local
   rot=(old_world.to_3x3()@src.matrix_local.to_3x3()).normalized().to_4x4();rot.translation=head
   b.head=head;b.tail=tail;b.matrix=rot;b.length=max((tail-head).length,.02)
  if item['parent']:b.parent=data.edit_bones[item['parent']]
  b.use_connect=False;b.use_deform=True
 bpy.ops.object.mode_set(mode='OBJECT');rig.select_set(False);rig.show_in_front=True
 for item in info['bones']:
  bone=data.bones[item['name']];assert (bone.parent.name if bone.parent else '')==item['parent'];bone['UE_reference_local_TQS']=json.dumps(item['local'])
 rig['source_skeleton']=info['skeleton'];rig['source_asset_pivot_m']=[0.,0.,0.];rig['approved_reference']='A_v7';rig.data.pose_position='REST'
 return rig
def copy_part(src,polys,label,col,rig,names,source_world):
 ids=sorted({v for p in polys for v in p.vertices});mp={v:i for i,v in enumerate(ids)}
 points=[source_world@src.data.vertices[i].co for i in ids]
 lo=Vector([min(v[i] for v in points) for i in range(3)]);hi=Vector([max(v[i] for v in points) for i in range(3)]);center=(lo+hi)/2
 me=bpy.data.meshes.new(label);me.from_pydata([v-center for v in points],[],[[mp[v] for v in p.vertices] for p in polys]);me.update()
 for mat in src.data.materials:me.materials.append(mat)
 for p,q in zip(me.polygons,polys):p.material_index=q.material_index;p.use_smooth=q.use_smooth
 normal_matrix=source_world.to_3x3().inverted().transposed();normal_values=[(normal_matrix@src.data.corner_normals[i].vector).normalized() for p in polys for i in p.loop_indices]
 me.normals_split_custom_set(normal_values)
 for old_uv in src.data.uv_layers:
  uv=me.uv_layers.new(name='SourceUV' if old_uv==src.data.uv_layers.active else old_uv.name)
  coords=[old_uv.data[i].uv[:] for p in polys for i in p.loop_indices];uv.data.foreach_set('uv',np.array(coords,dtype=np.float32).ravel())
 ob=bpy.data.objects.new(label,me);col.objects.link(ob);ob.location=center
 if rig:
  for name in names:ob.vertex_groups.new(name=name)
  old_names=[g.name for g in src.vertex_groups]
  for new_index,old_index in enumerate(ids):
   weights={old_names[g.group]:g.weight for g in src.data.vertices[old_index].groups if g.weight>1e-8}
   total=sum(weights.values());assert total>0
   for n,w in weights.items():ob.vertex_groups[n].add([new_index],w/total,'REPLACE')
  ob.parent=rig;ob.matrix_parent_inverse=Matrix.Identity(4)
 attr=me.color_attributes.new(name='SSF_PaletteLinear',type='FLOAT_COLOR',domain='CORNER')
 values=[tuple(me.materials[p.material_index].diffuse_color) for p in me.polygons for _ in p.loop_indices]
 attr.data.foreach_set('color',np.array(values,dtype=np.float32).ravel())
 return ob
def choose_reduction(ob,lod,source_helper,symmetric,keep_exact=False,flex=False):
 src=ob.data;tris=tri_count(src)
 if keep_exact or tris<=8 or flex and lod==0:return {'ratio':1.,'error_m':0.,'triangles':tris,'source_triangles':tris,'symmetry':False}
 dims=np.ptp(np.array([v.co[:] for v in src.vertices]),axis=0);span=max(dims);closed=nonmanifold(src)==0
 # Near geometry has a strict dimensional tolerance. Small solid fittings keep
 # their complete volume; low LODs use the same tolerance within each component.
 # The intermediate dimension governs reduction. A tall antenna must be
 # protected by its cross-section, rather than its height, to stay straight.
 tolerances=(.001,.018,.035);profile_span=float(sorted(dims)[1]);allowed=max(.000002,profile_span*tolerances[lod])
 if flex:allowed*=.4
 original_nonmanifold=nonmanifold(src)
 targets=((.78,.88,.96),(.38,.55,.72,.86),(.12,.22,.35,.5,.7,.88))[lod]
 result={'ratio':1.,'error_m':0.,'triangles':tris,'source_triangles':tris,'symmetry':symmetric,'tolerance_m':allowed}
 for ratio in targets:
  if tris*ratio<8 and closed:continue
  mod=ob.modifiers.new('Trial_Safe_Reduction','DECIMATE');mod.ratio=ratio;mod.use_collapse_triangulate=True;mod.use_symmetry=symmetric;mod.symmetry_axis='X'
  ev=ob.evaluated_get(bpy.context.evaluated_depsgraph_get());dest=ev.to_mesh();count=tri_count(dest)
  valid=count>= (8 if closed else 2) and nonmanifold(dest)<=original_nonmanifold
  error=surface_error(src,dest) if valid else float('inf')
  bounds_old=np.array([v.co[:] for v in src.vertices]);bounds_new=np.array([v.co[:] for v in dest.vertices]) if count else np.empty((0,3))
  bbox_error=float(max(np.abs(bounds_old.min(0)-bounds_new.min(0)).max(),np.abs(bounds_old.max(0)-bounds_new.max(0)).max())) if count else float('inf')
  ev.to_mesh_clear();ob.modifiers.remove(mod)
  if valid and error<=allowed and bbox_error<=allowed:
   result.update(ratio=ratio,error_m=error,bounds_error_m=bbox_error,triangles=count);break
 if result['ratio']<1:
  mod=ob.modifiers.new('LOD%d_ConservativeSegmentReduction'%lod,'DECIMATE');mod.ratio=result['ratio'];mod.use_collapse_triangulate=True;mod.use_symmetry=symmetric;mod.symmetry_axis='X'
 return result
def bake_join(parts,col,name,rig,names):
 evaluated=[];dg=bpy.context.evaluated_depsgraph_get()
 for ob in parts:
  ev=ob.evaluated_get(dg);me=bpy.data.meshes.new_from_object(ev,preserve_all_data_layers=True,depsgraph=dg)
  dst=bpy.data.objects.new('Bake_'+ob.name,me);col.objects.link(dst);dst.matrix_world=ob.matrix_world.copy()
  for g in ob.vertex_groups:dst.vertex_groups.new(name=g.name)
  evaluated.append(dst)
 bpy.ops.object.select_all(action='DESELECT')
 for ob in evaluated:ob.select_set(True)
 bpy.context.view_layer.objects.active=evaluated[0];bpy.ops.object.join();body=bpy.context.object;body.name=name
 m=body.matrix_world.copy()
 for v in body.data.vertices:v.co=m@v.co
 body.matrix_world=Matrix.Identity(4)
 if rig:
  body.parent=rig;body.matrix_parent_inverse=Matrix.Identity(4)
  skin=body.modifiers.new('Original_SourceCompatible_Binding','ARMATURE');skin.object=rig
 return body

selected=set(args.assets.split(',')) if args.assets else None
for a in baseline['assets']:
 key=a['key']
 if selected and key not in selected:continue
 oldscene=bpy.data.scenes[a['scene']];bpy.context.window.scene=oldscene;bpy.context.view_layer.update()
 source_worlds={o.name:o.matrix_world.copy() for o in oldscene.objects}
 rig_old=next((o for o in oldscene.objects if o.type=='ARMATURE'),None)
 scene=setup_scene(key,oldscene);rig=reconstruct_rig(key,rig_old,scene,source_worlds[rig_old.name]) if rig_old else None
 names=[b['name'] for b in sources[key].get('bones',[])];helpers=bpy.data.collections.new(key+'_SOURCE_NORMAL_HELPERS');scene.collection.children.link(helpers);helpers.hide_render=True
 edit=bpy.data.collections.new(key+'_EDITABLE_MECHANICAL_PARTS');scene.collection.children.link(edit)
 lodcols=[]
 for lod in range(3):
  c=bpy.data.collections.new(key+'_LOD%d_BAKED'%lod);scene.collection.children.link(c);lodcols.append(c)
 rec={'key':key,'scene':scene.name,'rig':rig.name if rig else None,'source_triangles':sources[key]['lod0_triangles'],
  'source_dimensions_m':a['dimensions_m'],'parts':[],'lods':[],'rig_bone_count':len(names),'near_geometric_tolerance_fraction':.001}
 groups=[]
 for part in a['parts']:
  src=bpy.data.objects[part['object']];polys=[src.data.polygons[i] for i in part['polygons']]
  # The source-connected partition can meet at coincident corners driven by
  # different bones. Separate their rigid mechanical ownership before welding.
  bins=defaultdict(list);oldnames=[g.name for g in src.vertex_groups]
  for p in polys:
   weights=[[(oldnames[g.group],g.weight) for g in src.data.vertices[v].groups if g.weight>1e-8] for v in p.vertices]
   rigid=bool(rig) and all(len(w)==1 and abs(w[0][1]-1)<.00001 for w in weights) and len({w[0][0] for w in weights})==1
   flex=bool(rig) and not rigid
   slot=src.data.materials[p.material_index];display=key=='Light' or 'Logo' in slot.name or (slot.use_nodes and any(n.type=='BSDF_TRANSPARENT' for n in slot.node_tree.nodes))
   bone=weights[0][0][0] if rigid else 'SOURCE_FLEX' if flex else 'STATIC'
   bins[(bone,display)].append(p)
  for (bone,display),faces in bins.items():
   label=f'{key}_P{part["id"]:03d}_{bone}' + ('_Display' if display else '')
   ob=copy_part(src,faces,label,edit,rig,names,source_worlds[src.name])
   component=ob.data.attributes.new(name='SSF_ComponentID',type='INT',domain='FACE')
   for item in component.data:item.value=len(rec['parts'])
   helper=ob.copy();helper.data=ob.data.copy();helper.name=label+'_OriginalSurface';helpers.objects.link(helper);helper.hide_render=True;helper.hide_set(True)
   if key not in ('Lamp','Light'):weld_weight_compatible(ob.data)
   helper['source_polygons']=json.dumps([p.index for p in faces]);helper['approved_reference']='A_v7'
   ob['source_connected_part']=part['id'];ob['source_bone']=bone;ob['source_surface_helper']=helper.name;ob['display_section']=display
   ob['geometry_role']='flexible hose' if bone=='SOURCE_FLEX' else 'rigid mechanical component';ob['approved_reference']='A_v7'
   symmetric=bone!='SOURCE_FLEX' and symmetric_x(ob.data,.0001)
   # Keep an editable mirror only when the entire source component proves
   # symmetric around its own local plane; source bone ownership remains intact.
   if symmetric and not display and key not in ('Lamp','Light') and len(ob.data.polygons)>6:
    mir=ob.modifiers.new('Verified_Source_Symmetry','MIRROR');mir.use_axis=(True,False,False);mir.use_bisect_axis=(True,False,False);mir.use_clip=True;mir.merge_threshold=.000001;mir.use_mirror_vertex_groups=False
    ev=ob.evaluated_get(bpy.context.evaluated_depsgraph_get());me=ev.to_mesh()
    error=surface_error(ob.data,me);more_faces=tri_count(me)>tri_count(ob.data);ev.to_mesh_clear()
    if error>.0001 or more_faces:ob.modifiers.remove(mir)
   lodparts=[ob];partrec={'object':ob.name,'part_id':part['id'],'bone':bone,'display':display,'source_faces':len(faces),'source_helper':helper.name,'verified_symmetry':symmetric,'lods':[]}
   for lod in (1,2):
    dst=ob.copy();dst.data=ob.data.copy();dst.name=label+f'_LOD{lod}';edit.objects.link(dst);lodparts.append(dst)
   for lod,dst in enumerate(lodparts):
    choice=choose_reduction(dst,lod,helper,symmetric,display or key in ('Lamp','Light'),bone=='SOURCE_FLEX')
    transfer=dst.modifiers.new('Approved_Surface_Normal_Transfer','DATA_TRANSFER');transfer.object=helper;transfer.use_loop_data=True;transfer.data_types_loops={'CUSTOM_NORMAL'};transfer.loop_mapping='POLYINTERP_NEAREST';transfer.use_object_transform=True
    # Bind after geometry evaluation for authoring; the delivery copies receive
    # the same actual armature modifier after their surfaces are joined.
    dst['LOD']=lod;partrec['lods'].append(choice)
   groups.append((lodparts,display));rec['parts'].append(partrec)
 for lod in range(3):
  chosen=[ps[lod] for ps,_ in groups]
  body=bake_join(chosen,lodcols[lod],key+f'_LOD{lod}_Body',rig,names)
  body['LOD']=lod;body['screen_size']=(1.,.10,.035)[lod];body['production_body']=True;body['source_asset_pivot_m']=[0.,0.,0.]
  rec['lods'].append({'LOD':lod,'body':body.name,'body_triangles':tri_count(body.data),'body_cap':CAPS[key][lod][0],
   'outline_cap':CAPS[key][lod][1],'retained_mechanical_parts':len(chosen),'body_delta':max(0,tri_count(body.data)-CAPS[key][lod][0])})
 edit.hide_render=True;edit.hide_viewport=False
 for ob in edit.objects:
  if rig:
   skin=ob.modifiers.new('Original_SourceCompatible_Binding','ARMATURE');skin.object=rig
  ob.hide_set(True)
 for lod,c in enumerate(lodcols):
  for ob in c.objects:ob.hide_render=lod!=0;ob.hide_set(lod!=0)
 if rig:rig.data.pose_position='POSE'
 rec['editable_collection']=edit.name;rec['helper_collection']=helpers.name
 report['assets'].append(rec)
 print('SSF_GEOMETRY_READY',key,[(r['body_triangles'],r['body_delta']) for r in rec['lods']],len(rec['parts']),flush=True)
out=O/('SSF_B1_GeometryProbe.blend' if selected else 'SSF_B1_Geometry.blend')
bpy.ops.wm.save_as_mainfile(filepath=str(out))
(O/('geometry_probe_report.json' if selected else 'geometry_report.json')).write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('SSF_PRODUCTION_GEOMETRY_OK',str(out),flush=True)
