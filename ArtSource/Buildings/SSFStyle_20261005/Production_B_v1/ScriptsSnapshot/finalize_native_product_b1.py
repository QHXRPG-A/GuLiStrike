"""Finalize editable normals, source translucent maps, complete major outlines and assembly."""
import bpy,bmesh,json,ast,math
from pathlib import Path
from mathutils import Vector,Matrix
from mathutils.bvhtree import BVHTree
import numpy as np
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1'
assert not (O/'production_manifest.json').exists()
bpy.ops.wm.open_mainfile(filepath=str(O/'SSF_Production_B_v1.blend'))
report=json.loads((O/'construction_report.json').read_text(encoding='utf8'))
for prior in [s for s in bpy.data.scenes if s.name.startswith('Production_Assembly')]:
 for ob in list(prior.objects):bpy.data.objects.remove(ob,do_unlink=True)
 bpy.data.scenes.remove(prior)
ref=json.loads((R/'References_A_v7/render_manifest.json').read_text(encoding='utf8'));refs={a['key']:a for a in ref['assets']}
module=ast.parse((R/'Scripts/build_atlas_lines_b1.py').read_text(encoding='utf8'))
functions=ast.Module(body=[n for n in module.body if isinstance(n,ast.FunctionDef) and n.name in ('linear','outline')],type_ignores=[])
exec(compile(functions,'NativeOutlineFunctions','exec'));INK=linear('#1A182F')

def normal_source(helper):
 m=helper.data;m.calc_loop_triangles();world=helper.matrix_world.copy()
 points=[world@v.co for v in m.vertices];triangles=list(m.loop_triangles)
 tree=BVHTree.FromPolygons(points,[list(t.vertices) for t in triangles],all_triangles=True)
 normals=[(world.to_3x3().inverted().transposed()@n.vector).normalized() for n in m.corner_normals]
 return points,triangles,tree,normals
def normals_from_source(ob,sources):
 m=ob.data;attribute=m.attributes['SSF_ComponentID'];world=ob.matrix_world.copy();to_local=world.to_3x3().transposed();values=[(0,0,1)]*len(m.loops)
 for p in m.polygons:
  points,tris,tree,normals=sources[attribute.data[p.index].value]
  center=world@p.center
  for li in p.loop_indices:
   point=world@m.vertices[m.loops[li].vertex_index].co;query=point*.995+center*.005
   _,normal,index,_=tree.find_nearest(query);t=tris[index];v0,v1,v2=[points[i] for i in t.vertices]
   u=v1-v0;v=v2-v0;w=point-v0;a,b,c,d,e=u.dot(u),u.dot(v),v.dot(v),w.dot(u),w.dot(v);den=a*c-b*b
   if abs(den)>1e-14:
    f=max(0.,(c*d-b*e)/den);g=max(0.,(a*e-b*d)/den)
    if f+g>1.:f,g=f/(f+g),g/(f+g)
    normal=normals[t.loops[0]]*(1-f-g)+normals[t.loops[1]]*f+normals[t.loops[2]]*g
   if normal.length_squared<1e-12:normal=p.normal.copy()
   values[li]=tuple((to_local@normal).normalized())
  p.use_smooth=True
 m.normals_split_custom_set(values);m.update()
 # A decimated thin cap can produce an invalid compressed custom normal at
 # a sharp mirrored corner. Give just those loops their nonzero face normal.
 invalid=[i for i,n in enumerate(m.corner_normals) if n.vector.length<.99]
 if invalid:
  invalid=set(invalid)
  for p in m.polygons:
   for li in p.loop_indices:
    if li in invalid:values[li]=tuple(p.normal.normalized())
  m.normals_split_custom_set(values);m.update()
  assert all(n.vector.length>.99 for n in m.corner_normals),(ob.name,'zero corner normal remained')

display_images={}

def preserve_rigid_ownership(ob,parts):
 if not ob.vertex_groups:return
 owners={}
 for p in ob.data.polygons:
  owner=parts[ob.data.attributes['SSF_ComponentID'].data[p.index].value]['bone']
  for vi in p.vertices:
   if vi in owners:assert owners[vi]==owner
   owners[vi]=owner
 targets={}
 for vi,owner in owners.items():
  if owner in ('SOURCE_FLEX','STATIC'):continue
  for membership in list(ob.data.vertices[vi].groups):ob.vertex_groups[membership.group].remove([vi])
  targets.setdefault(owner,[]).append(vi)
 for owner,indices in targets.items():ob.vertex_groups[owner].add(indices,1.,'REPLACE')
def portable_display_image(original,label,size=1024):
 if label in display_images:return display_images[label]
 im=original.copy();im.name=label;im.scale(size,size);path=O/'Textures'/(label+'.png');im.filepath_raw=str(path);im.file_format='PNG';im.save();im.pack()
 display_images[label]=im;return im
for asset in report['assets']:
 key=asset['key'];scene=bpy.data.scenes[asset['scene']];bpy.context.window.scene=scene;bpy.context.view_layer.update()
 rig=bpy.data.objects[asset['rig']] if asset['rig'] else None
 if rig:rig.data.pose_position='REST'
 helpers={i:normal_source(bpy.data.objects[p['source_helper']]) for i,p in enumerate(asset['parts'])}
 edit=bpy.data.collections[asset['editable_collection']]
 for ob in edit.objects:
  # Face-interior interpolation retains the approved rest surface normals;
  # ambiguous nearest-corner transfers are removed from the editable stack.
  for modifier in list(ob.modifiers):
   if modifier.type=='DATA_TRANSFER':ob.modifiers.remove(modifier)
   elif modifier.type=='MIRROR':modifier.use_mirror_vertex_groups=False
  preserve_rigid_ownership(ob,asset['parts'])
  normals_from_source(ob,helpers)
 for e in asset['lods']:
  body=bpy.data.objects[e['body']]
  preserve_rigid_ownership(body,asset['parts'])
  normals_from_source(body,helpers)
  if e['outline']:bpy.data.objects.remove(bpy.data.objects[e['outline']],do_unlink=True)
  col=bpy.data.collections[key+f'_LOD{e["LOD"]}_BAKED']
  out,stats=outline(key,e['LOD'],asset,scene,col,rig);e.update(stats);e['outline']=out.name if out else None
  if out:normals_from_source(out,helpers)
  # Remove unused source palette slots on the sprite; only its actual source
  # translucent material remains. Building displays preserve their own UV.
  if key=='Light':
   active=body.data.materials[body.data.polygons[0].material_index].copy();active.name=f'M_SSF_Light_SourceTranslucent_LOD{e["LOD"]}'
   body.data.materials.clear();body.data.materials.append(active)
   for p in body.data.polygons:p.material_index=0
  for mat in body.data.materials:
   if not mat.use_nodes:continue
   for node in mat.node_tree.nodes:
    if node.type=='TEX_IMAGE' and node.image and ('Logo' in node.image.name or key=='Light'):
     label='SSF_Shared_SourceLogo_1K' if key!='Light' else 'Light_SourceEmissionAlpha_1K'
     node.image=portable_display_image(node.image,label)
  e['actual_body_sections']=len({p.material_index for p in body.data.polygons})
  e['actual_total_sections']=e['actual_body_sections']+int(out is not None)
  e['total_triangles']=e['body_triangles']+e['outline_triangles'];e['total_cap']=e['body_cap']+e['outline_cap']
  e['body_delta']=max(0,e['body_triangles']-e['body_cap']);e['outline_delta']=max(0,e['outline_triangles']-e['outline_cap']);e['total_delta']=max(0,e['total_triangles']-e['total_cap'])
 if key=='Light':asset['textures']={'display':{'file':'Textures/Light_SourceEmissionAlpha_1K.png','resolution':[1024,1024],'colorspace':display_images['Light_SourceEmissionAlpha_1K'].colorspace_settings.name}}
 elif any(e['actual_body_sections']==2 for e in asset['lods']):asset['textures']['shared_display']={'file':'Textures/SSF_Shared_SourceLogo_1K.png','resolution':[1024,1024],'shared_between_six_buildings':True}
 if rig:rig.data.pose_position='POSE'
 print('SSF_NATIVE_FINAL_ASSET',key,[(e['body_triangles'],e['outline_triangles'],e['actual_total_sections']) for e in asset['lods']],flush=True)

# Original-scale actual-product composition, using the same approved reference
# layout and camera directions. All geometry and material datablocks are shared.
old=next(s for s in bpy.data.scenes if 'ReferenceAssembly' in s.name)
assembly=bpy.data.scenes.new('Production_Assembly');bpy.context.window.scene=assembly;assembly.world=old.world.copy();assembly.render.engine='BLENDER_EEVEE';assembly.eevee.taa_render_samples=48
assembly.render.resolution_x=assembly.render.resolution_y=4096;assembly.render.resolution_percentage=100;assembly.render.use_freestyle=False
assembly.view_settings.view_transform='Standard';assembly.view_settings.look='None';assembly.render.image_settings.file_format='PNG';assembly.render.image_settings.color_mode='RGBA'
cam=old.camera.copy();cam.data=old.camera.data.copy();cam.name='ActualProduct_AssemblyCamera';assembly.collection.objects.link(cam);assembly.camera=cam
assets={a['key']:a for a in report['assets']}
def instance(key,offset,suffix=''):
 a=assets[key];rig=bpy.data.objects[a['rig']] if a['rig'] else None;newrig=None
 if rig:
  newrig=rig.copy();newrig.name='Assembly_'+key+suffix+'_Rig';newrig.animation_data_clear();newrig.location=Vector(offset);assembly.collection.objects.link(newrig)
 for name in (a['lods'][0]['body'],a['lods'][0]['outline']):
  if not name:continue
  original=bpy.data.objects[name];copy=original.copy();copy.name='Assembly_'+key+suffix+'_'+original.name;copy.hide_render=False;assembly.collection.objects.link(copy)
  if newrig:
   copy.parent=newrig;copy.matrix_parent_inverse=Matrix.Identity(4);copy.matrix_basis=original.matrix_basis.copy()
  else:copy.location+=Vector(offset)
  for mod in copy.modifiers:
   if mod.type=='ARMATURE':mod.object=newrig
  copy.hide_set(False)
for key,offset in ref['assembly']['layout_m'].items():instance(key,offset)
for i,pos in enumerate([(13,-33,1.8),(27,-33,1.8),(-28,12,1.8),(-14,12,1.8)]):
 instance('Lamp',pos,'_'+str(i));instance('Light',(pos[0],pos[1]-.95,pos[2]+3.75),'_'+str(i))
bpy.context.view_layer.update();points=[o.matrix_world@Vector(p) for o in assembly.objects if o.type=='MESH' for p in o.bound_box]
lo=Vector([min(p[i] for p in points) for i in range(3)]);hi=Vector([max(p[i] for p in points) for i in range(3)]);center=(lo+hi)/2;extent=max(hi-lo)
cam.data.ortho_scale=extent*1.48;cam.location=center+Vector((.7,-1.2,1.4)).normalized()*extent*4;cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
report['assembly']={'scene':assembly.name,'layout_m':ref['assembly']['layout_m'],'center_m':list(center),'extent_m':extent,'not_a_gameplay_level':True}
report['stage']='actual_Blender_product_ready_for_B_evidence';report['B_approval']='pending';report['budget_exceptions_approved']=False;report['editable_normal_transfer']='face-interior source corner-normal interpolation; no ambiguous corner transfer modifiers'
report['line_mask_channels']={'R':'source panel lines, strength .65','G':'actual mechanical crease/border lines, strength 1.0','B':'unused','LOD_fade':[1.,.70,0.]}
report['rigid_binding']='one source owner bone per metal component, weight 1; mirror vertex-group swapping disabled; original cloning hoses retain flexible weights'
report['all_images_packed']=all(im.packed_file for im in bpy.data.images if im.source=='FILE' and im.users>0)
(O/'construction_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
bpy.ops.wm.save_as_mainfile(filepath=str(O/'SSF_Production_B_v1.blend'))
print('SSF_NATIVE_PRODUCT_FINALIZED',report['all_images_packed'],flush=True)
