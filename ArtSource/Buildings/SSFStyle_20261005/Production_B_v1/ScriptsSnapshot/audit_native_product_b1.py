"""Inspect actual Blender geometry, weights, matrices, shaders and portable textures."""
import bpy,json,hashlib,math
import numpy as np
from pathlib import Path
from collections import Counter
from mathutils import Matrix
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1'
file=O/'SSF_Production_B_v1.blend';bpy.ops.wm.open_mainfile(filepath=str(file))
report=json.loads((O/'construction_report.json').read_text(encoding='utf8'));source=json.loads((R/'Source/source_manifest.json').read_text(encoding='utf8'));sources={m['key']:m for m in source['meshes']}
checks=[];exceptions=[]
for a in report['assets']:
 key=a['key'];scene=bpy.data.scenes[a['scene']];bpy.context.window.scene=scene;bpy.context.view_layer.update()
 rig=bpy.data.objects[a['rig']] if a['rig'] else None
 hierarchy=True
 if rig:
  expected=sources[key]['bones'];assert [b.name for b in rig.data.bones]==[b['name'] for b in expected]
  hierarchy=all((rig.data.bones[b['name']].parent.name if rig.data.bones[b['name']].parent else '')==b['parent'] for b in expected)
  assert hierarchy and rig.animation_data.action is None
 entry={'asset':key,'source_bones':len(sources[key].get('bones',[])),'source_hierarchy_preserved':hierarchy,'LOD_checks':[]}
 for e in a['lods']:
  ob=bpy.data.objects[e['body']];m=ob.data;m.calc_loop_triangles();tri=len(m.loop_triangles);positions=np.array([v.co[:] for v in m.vertices]);normals=np.array([n.vector[:] for n in m.corner_normals]);palette=m.color_attributes.get('SSF_PaletteLinear')
  components=set(d.value for d in m.attributes['SSF_ComponentID'].data);expected_components=set(range(len(a['parts'])))
  assert components==expected_components,(key,e['LOD'],'A mechanical part disappeared')
  assert tri==e['body_triangles'] and len({p.material_index for p in m.polygons})==e['actual_body_sections']
  normal_lengths=np.linalg.norm(normals,axis=1)
  assert np.isfinite(positions).all() and np.isfinite(normals).all() and np.all(normal_lengths>.99),(key,e['LOD'],float(normal_lengths.min()),len(normals),int(np.sum(normal_lengths<.99)))
  assert max(abs(ob.matrix_world[i][j]-(1 if i==j else 0)) for i in range(4) for j in range(4))<1e-5
  actual_dimensions=positions.max(0)-positions.min(0);dimension_error=np.max(np.abs(actual_dimensions-np.array(a['source_dimensions_m'])))
  weight_errors=[];nonrigid=0;rigid_groups={};bone_group_names=[g.name for g in ob.vertex_groups]
  if rig:
   for v in m.vertices:
    weights=[g.weight for g in v.groups if g.weight>1e-7];weight_errors.append(abs(sum(weights)-1));nonrigid+=int(len(weights)>1)
    assert all(bone_group_names[g.group] in rig.data.bones for g in v.groups)
   assert max(weight_errors)<1e-5 and (key=='CloningCenter' or nonrigid==0),(key,e['LOD'],max(weight_errors),nonrigid)
  body_sections=len({p.material_index for p in m.polygons});outline_tri=0
  if e['outline']:
   out=bpy.data.objects[e['outline']];ev=out.evaluated_get(bpy.context.evaluated_depsgraph_get());me=ev.to_mesh();me.calc_loop_triangles();outline_tri=len(me.loop_triangles);ev.to_mesh_clear()
   assert outline_tri==e['outline_triangles'] and outline_tri<=e['outline_cap']
  assert body_sections+int(outline_tri>0)<=3
  if key not in ('Light',):
   mat=m.materials[0];nt=mat.node_tree;ramp=nt.nodes['Approved_A7_ThreeTone'].color_ramp
   assert ramp.interpolation=='CONSTANT' and all(abs(p-x.position)<1e-6 for p,x in zip((0,.38,.68),ramp.elements))
   assert 'Base Color' in nt.nodes and 'Team Color' in nt.nodes and 'SSF_AtlasUV' in m.uv_layers
   assert abs(nt.nodes['InternalLineStrength'].inputs[1].default_value-(1.,.70,0)[e['LOD']])<1e-6
  item={'LOD':e['LOD'],'body_triangles':tri,'outline_triangles':outline_tri,'sections':body_sections+int(outline_tri>0),
   'body_cap':e['body_cap'],'outline_cap':e['outline_cap'],'body_delta':max(0,tri-e['body_cap']),
   'source_dimensions_m':a['source_dimensions_m'],'actual_dimensions_m':actual_dimensions.tolist(),'dimension_max_difference_m':float(dimension_error),
   'retained_component_count':len(components),'source_component_count':len(a['parts']),'unit_object_scale':True,
   'nonrigid_vertices':nonrigid,'weight_sum_max_error':max(weight_errors) if weight_errors else 0.,'normals_finite_and_unit':True}
  entry['LOD_checks'].append(item)
  if item['body_delta']:exceptions.append({'asset':key,**item,'exception_status':'pending_user_decision','reason':'Source structure, curved profiles, slender cross sections and every animated component are retained'})
 if key=='Light':assert all(e['body_triangles']==2 for e in a['lods'])
 if key=='Lamp':assert all(e['body_triangles']==48 for e in a['lods'])
 checks.append(entry)
 assert not scene.render.use_freestyle
texture_checks=[]
for a in report['assets']:
 for channel,record in a['textures'].items():
  if not isinstance(record,dict) or 'file' not in record:continue
  path=O/record['file'];assert path.exists();texture_checks.append({'asset':a['key'],'channel':channel,**record,'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
result={'version':report['version'],'source_blend_sha256':hashlib.sha256(file.read_bytes()).hexdigest(),'technical_geometry_checks_passed':True,
 'visual_user_approval':False,'budget_caps_all_passed':not exceptions,'budget_exception_count':len(exceptions),'source_UE_import_compatibility_not_yet_verified':True,
 'asset_checks':checks,'textures':texture_checks,'budget_exceptions':exceptions,'all_22_actions_present':all(a['name'] in bpy.data.actions for a in report['animations']),
 'actual_native_action_sample_matrix_max_error':max(a['actual_Blender_sample_matrix_max_error'] for a in report['animations']),
 'all_images_packed':all(im.packed_file for im in bpy.data.images if im.source=='FILE' and im.users>0),'UE_formal_import_performed':False}
assert result['all_22_actions_present'] and result['all_images_packed']
(O/'native_validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print('SSF_NATIVE_GEOMETRY_AUDIT_OK',len(checks),len(exceptions),'budget exceptions pending',flush=True)
