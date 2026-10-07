"""Reduce actual internal line density; keep B3 base silhouettes and all mesh/rig data."""
import bpy,json,hashlib,shutil
import numpy as np
from pathlib import Path
from collections import defaultdict
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004');O=R/'Production_B_v4';T=O/'Textures';T.mkdir(parents=True,exist_ok=True)
assert not (O/'production_manifest.json').exists(),'Do not overwrite a frozen revision'
src=R/'Production_B_v3/ControlRigMech_B_v3_Production.blend'
assert hashlib.sha256(src.read_bytes()).hexdigest()=='6ceea9e11e49203cd00e8cdfbd24f75de99c6ff2af6ba5239a641d3b08b037c1'
bpy.ops.wm.open_mainfile(filepath=str(src));s=bpy.data.scenes['STYLE_REALTIME_B_v3'];s.name='STYLE_REALTIME_B_v4';bpy.context.window.scene=s
rig=bpy.data.objects['Armature'];assert rig.animation_data.action is None and rig.data.pose_position=='POSE'
col=bpy.data.collections['PRODUCTION_LODS_SINGLE_BODY_SECTION']
def preserved_hash(ob):
 m=ob.data;m.calc_loop_triangles();h=hashlib.sha256()
 for values in ([v.co[:] for v in m.vertices],[t.vertices[:] for t in m.loop_triangles],[n.vector[:] for n in m.corner_normals],[u.uv[:] for u in m.uv_layers[0].data],[c.color[:] for c in m.color_attributes['GuLi_PaletteLinear'].data]):h.update(np.asarray(values).tobytes())
 h.update(json.dumps([[g.name for g in ob.vertex_groups],[[[g.group,g.weight] for g in v.groups] for v in m.vertices]],separators=(',',':')).encode())
 return h.hexdigest()
report={'version':'B-v4','user_feedback':'线稿含量低一些，保留基础轮廓','source_blender_sha256':hashlib.sha256(src.read_bytes()).hexdigest(),'three_tone_coefficients':[.42,.74,1.],'outline_mesh_width_and_binding_unchanged':True,'body_geometry_normals_uv0_palette_skin_unchanged':True,'freestyle':False,'lods':[]}
for lod in range(4):
 ob=bpy.data.objects[f'ControlRigMech_LOD{lod}_Body'];m=ob.data;oldhash=preserved_hash(ob)
 uv=m.uv_layers['GuLi_StructuralDistance_RG'];uv2=m.uv_layers['GuLi_StructuralDistance_B']
 rg=np.array([x.uv[:] for x in uv.data],dtype=np.float32);bl=np.array([x.uv[:] for x in uv2.data],dtype=np.float32)
 d=np.column_stack((rg,bl[:,0])).reshape(-1,3,3)
 keys=[(tuple(round(float(x),5) for x in v.co),tuple(sorted((g.group,round(g.weight,6)) for g in v.groups))) for v in m.vertices]
 adj=defaultdict(list);candidates=set();old_count=0
 for ti,p in enumerate(m.polygons):
  vs=list(p.vertices)
  for axis in range(3):
   a,b=vs[(axis+1)%3],vs[(axis+2)%3];key=tuple(sorted((keys[a],keys[b])));adj[key].append(ti)
   if np.count_nonzero(np.abs(d[ti,:,axis])<1e-9)>=2:candidates.add(key);old_count+=1
 kept=set()
 for key in candidates:
  length=float(np.linalg.norm(np.array(key[0][0])-np.array(key[1][0])))
  # Remove short detail edges and tiny fittings, retaining broad armor junctions.
  if length>=.18 and max(m.polygons[i].area for i in adj[key])>=.03:kept.add(key)
 new_count=0
 for ti,p in enumerate(m.polygons):
  vs=list(p.vertices)
  for axis in range(3):
   key=tuple(sorted((keys[vs[(axis+1)%3]],keys[vs[(axis+2)%3]])))
   was=np.count_nonzero(np.abs(d[ti,:,axis])<1e-9)>=2
   if was and key in kept:new_count+=1
   else:d[ti,:,axis]=8.
 flat=d.reshape(-1,3);uv.data.foreach_set('uv',flat[:,:2].ravel());uv2.data.foreach_set('uv',np.column_stack((flat[:,2],np.zeros(len(flat)))).ravel())
 assert preserved_hash(ob)==oldhash,'Only structural UV fields may change'
 mat=ob.active_material.copy();mat.name=f'M_ControlRigMech_B_v4_SparseLines_LOD{lod}';ob.data.materials[0]=mat
 strength=(.30,.22,.10,0.)[lod];mat.node_tree.nodes['InternalLineStrength'].inputs[1].default_value=strength
 width=(.006,.007,.008,.006)[lod];line=mat.node_tree.nodes['StructuralLineWidthMeters'];line.inputs['From Min'].default_value=width-.0015;line.inputs['From Max'].default_value=width+.0015
 ob['B_version']='B-v4';ob['line_method']='sparse major structure lines, subtle strength; base silhouette shell retained'
 tris=len(m.loop_triangles);oldbudget=json.loads((R/'Production_B_v3/realtime_line_report.json').read_text(encoding='utf-8'))['lods'][lod]
 report['lods'].append({'lod':lod,'old_selected_edges':len(candidates),'selected_edges':len(kept),'old_selected_triangle_edges':old_count,'selected_triangle_edges':new_count,'selected_edge_reduction_percent':100*(1-len(kept)/len(candidates)),'internal_line_strength':strength,'internal_line_half_width_m':width,'body_triangles':tris,'outline_triangles':oldbudget['outline_triangles'],'total_triangles':oldbudget['total_triangles'],'total_cap':oldbudget['total_cap'],'total_delta':oldbudget['total_delta'],'preserved_body_hash_before_after':oldhash})
 print('SPARSE_STRUCTURE',report['lods'][-1],flush=True)

def line_nodes(nt,width):
 uv=nt.nodes.new('ShaderNodeUVMap');uv.uv_map='GuLi_StructuralDistance_RG';uv2=nt.nodes.new('ShaderNodeUVMap');uv2.uv_map='GuLi_StructuralDistance_B'
 a=nt.nodes.new('ShaderNodeSeparateXYZ');b=nt.nodes.new('ShaderNodeSeparateXYZ');nt.links.new(uv.outputs[0],a.inputs[0]);nt.links.new(uv2.outputs[0],b.inputs[0])
 low=nt.nodes.new('ShaderNodeMath');low.operation='MINIMUM';low2=nt.nodes.new('ShaderNodeMath');low2.operation='MINIMUM';nt.links.new(a.outputs['X'],low.inputs[0]);nt.links.new(a.outputs['Y'],low.inputs[1]);nt.links.new(low.outputs[0],low2.inputs[0]);nt.links.new(b.outputs['X'],low2.inputs[1])
 mask=nt.nodes.new('ShaderNodeMapRange');mask.inputs['From Min'].default_value=width-.0015;mask.inputs['From Max'].default_value=width+.0015;mask.inputs['To Min'].default_value=1.;mask.inputs['To Max'].default_value=0.;nt.links.new(low2.outputs[0],mask.inputs['Value']);return mask.outputs[0]
body=bpy.data.objects['ControlRigMech_LOD0_Body'];mainmat=body.active_material
im=bpy.data.images.new('ControlRigMech_B_v4_SparseInternalLineMask_2K',width=2048,height=2048,alpha=True,float_buffer=True);im.colorspace_settings.name='Non-Color'
temp=bpy.data.materials.new('TEMP_B4_SparseLineBake');temp.use_nodes=True;nt=temp.node_tree;nt.nodes.clear();out=nt.nodes.new('ShaderNodeOutputMaterial');em=nt.nodes.new('ShaderNodeEmission');nt.links.new(line_nodes(nt,.006),em.inputs['Color']);nt.links.new(em.outputs[0],out.inputs['Surface']);target=nt.nodes.new('ShaderNodeTexImage');target.image=im;nt.nodes.active=target
body.data.materials[0]=temp;bpy.ops.object.select_all(action='DESELECT');body.hide_set(False);body.select_set(True);bpy.context.view_layer.objects.active=body
s.render.engine='CYCLES';s.cycles.samples=1;bpy.ops.object.bake(type='EMIT',margin=2)
im.filepath_raw=str(T/'ControlRigMech_InternalLineMask_2K.png');im.file_format='PNG';im.save();im.pack();body.data.materials[0]=mainmat;s.render.engine='BLENDER_EEVEE'
for lod in range(4):
 ob=bpy.data.objects[f'ControlRigMech_LOD{lod}_Body'];nt=ob.active_material.node_tree
 for n in nt.nodes:
  if n.type=='TEX_IMAGE' and n.image and 'InternalLineMask' in n.image.name:n.image=im
 ramp=next(n for n in nt.nodes if n.type=='VALTORGB');assert ramp.color_ramp.interpolation=='CONSTANT';assert np.allclose([e.color[0] for e in ramp.color_ramp.elements],[.42,.74,1.])
 assert preserved_hash(ob)==report['lods'][lod]['preserved_body_hash_before_after']
for ob in col.objects:
 if ob.get('production_outline') and ob.get('B_version')=='B-v3':
  ob.name=ob.name.replace('B_v3','B_v4');ob['B_version']='B-v4'
for name in ('BaseColor','FunctionalMask','ORM'):shutil.copy2(R/f'Production_B_v3/Textures/ControlRigMech_{name}_2K.png',T/f'ControlRigMech_{name}_2K.png')
for img in bpy.data.images:
 name=Path(img.filepath).name
 if name and (T/name).is_file():img.filepath='//Textures/'+name
s.render.use_freestyle=False;s.render.resolution_x=s.render.resolution_y=2048;s.render.resolution_percentage=100
setup=json.loads((R/'References_A_v2/reference_setup.json').read_text(encoding='utf-8'))
for view,cam in setup['cameras'].items():
 s.camera.location=cam['location_m'];s.camera.rotation_euler=cam['rotation_radians'];s.camera.data.ortho_scale=cam['ortho_scale_m'];s.render.filepath=str(O/f'ControlRigMech_B_v4_{view}.png');bpy.ops.render.render(write_still=True)
cam=setup['cameras']['Hero'];s.camera.location=cam['location_m'];s.camera.rotation_euler=cam['rotation_radians'];s.camera.data.ortho_scale=cam['ortho_scale_m']
diag=O/'LineDiagnostics';diag.mkdir(exist_ok=True);strength=body.active_material.node_tree.nodes['InternalLineStrength']
saved=strength.inputs[1].default_value
for label,val in [('BaseOutlineOnly',0.),('SparseLines',saved)]:
 strength.inputs[1].default_value=val;s.render.filepath=str(diag/f'ControlRigMech_B_v4_{label}.png');bpy.ops.render.render(write_still=True)
strength.inputs[1].default_value=saved
(O/'line_revision_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(O/'ControlRigMech_B_v4_Production.blend'))
print('B4_SPARSE_LINES_SAVED',flush=True)
