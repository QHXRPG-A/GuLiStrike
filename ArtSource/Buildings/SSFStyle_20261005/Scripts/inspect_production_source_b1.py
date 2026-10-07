"""Read native approved source and one FBX clip; do not alter any published file."""
import bpy,json
from pathlib import Path
from collections import Counter
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
O=R/'Production_B_v1';O.mkdir(exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(R/'References_A_v7/SSF_ReferenceDesign_v7.blend'))
baseline=json.loads((R/'Baseline/blender_source_manifest.json').read_text())
out={'scenes':[], 'assets':[]}
for asset in baseline['assets']:
 s=bpy.data.scenes[asset['scene']];bpy.context.window.scene=s
 meshes=[];rigs=[]
 for ob in s.objects:
  if ob.type=='MESH':
   m=ob.data;m.calc_loop_triangles()
   meshes.append({'name':ob.name,'matrix_world':[list(row) for row in ob.matrix_world],
    'triangles':len(m.loop_triangles),'vertices':len(m.vertices),'polygons':len(m.polygons),
    'uvs':[x.name for x in m.uv_layers], 'materials':[x.name if x else None for x in m.materials],
    'face_material_counts':dict(Counter(p.material_index for p in m.polygons)),
    'modifiers':[(x.name,x.type) for x in ob.modifiers],
    'groups':[g.name for g in ob.vertex_groups],
    'weighted_vertex_counts':dict(Counter(len(v.groups) for v in m.vertices))})
  if ob.type=='ARMATURE':
   rigs.append({'name':ob.name,'matrix_world':[list(row) for row in ob.matrix_world],
     'bone_count':len(ob.data.bones),'root_bones':[(b.name,list(b.matrix_local)) for b in ob.data.bones if not b.parent]})
   for b in rigs[-1]['root_bones']:b[1][:]=[list(row) for row in b[1]]
 out['assets'].append({'key':asset['key'],'meshes':meshes,'rigs':rigs,'part_count':len(asset['parts'])})
out['materials']=[{'name':m.name,'diffuse':list(m.diffuse_color),
 'nodes':[{'name':n.name,'type':n.type,'image':n.image.name if n.type=='TEX_IMAGE' and n.image else None} for n in m.node_tree.nodes]}
 for m in bpy.data.materials if m.name.startswith('A3_MilitaryFactory')][:2]
s=bpy.data.scenes.new('AnimationImportProbe');bpy.context.window.scene=s
bpy.ops.import_scene.fbx(filepath=str(R/'Source/Animations/TB1_MilitaryFactory_OpenDoor_Anim.fbx'),use_anim=True,automatic_bone_orientation=False)
out['clip_probe']=[{'name':o.name,'type':o.type,'matrix_world':[list(row) for row in o.matrix_world],
 'bones':len(o.data.bones) if o.type=='ARMATURE' else None,
 'action':o.animation_data.action.name if o.animation_data and o.animation_data.action else None,
 'range':list(o.animation_data.action.frame_range) if o.animation_data and o.animation_data.action else None} for o in s.objects]
(O/'source_native_probe.json').write_text(json.dumps(out,indent=2),encoding='utf8')
print('SSF_NATIVE_PROBE_OK',[(a['key'],a['part_count'],a['rigs'][0]['bone_count'] if a['rigs'] else 0) for a in out['assets']],flush=True)
