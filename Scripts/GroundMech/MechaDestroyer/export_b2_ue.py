"""Export an isolated, evaluated copy; never save or reshape the editable source."""
import bpy,json,hashlib
from pathlib import Path
from mathutils import Matrix,Vector
OUT=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v2/UE_Export')
source_path=Path(bpy.data.filepath)
original=bpy.data.objects['GMA4B2_Rig_BLUE']
original.data.pose_position='REST'
bpy.context.window.scene=bpy.data.scenes['GroundMech_A4B2_Construction']
bpy.context.view_layer.update()
deps=bpy.context.evaluated_depsgraph_get()
parts=[o for o in bpy.data.collections['GMA4B2_TEAM_BLUE'].all_objects if o.type=='MESH']
vertices=[];faces=[];face_materials=[];weights=[];mats=[];parts_report=[]
for obj in parts:
    evaluated=obj.evaluated_get(deps)
    data=evaluated.to_mesh(preserve_all_data_layers=True,depsgraph=deps)
    offset=len(vertices)
    bone=obj.get('rigid_bone')
    if not bone:
        groups=set(obj.vertex_groups[g.group].name for v in obj.data.vertices for g in v.groups if g.weight>.999)
        assert len(groups)==1,(obj.name,groups)
        bone=next(iter(groups))
    assert bone in original.data.bones
    vertices.extend(tuple((obj.matrix_world@v.co)*100) for v in data.vertices)
    weights.extend([bone]*len(data.vertices))
    slots=[]
    for s in obj.material_slots:
        role=s.material.get('color_role',s.material.name.split('_')[-1])
        if role not in mats:mats.append(role)
        slots.append(mats.index(role))
    for p in data.polygons:
        faces.append(tuple(offset+i for i in p.vertices));face_materials.append((slots[p.material_index],p.use_smooth))
    parts_report.append({'name':obj.name,'bone':bone,'vertices':len(data.vertices)})
    evaluated.to_mesh_clear()
scene=bpy.data.scenes.new('UE_EXPORT_ONLY');scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=.01
bpy.context.window.scene=scene
rig=original.copy();rig.data=original.data.copy();rig.animation_data_clear();rig.name='Armature';scene.collection.objects.link(rig)
rig.matrix_world=Matrix.Identity(4)
for b in rig.pose.bones:
    for c in list(b.constraints):b.constraints.remove(c)
    b.matrix_basis=Matrix.Identity(4)
bpy.context.view_layer.objects.active=rig;rig.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
for b in rig.data.edit_bones:b.head*=100;b.tail*=100
bpy.ops.object.mode_set(mode='OBJECT')
data=bpy.data.meshes.new('SK_GroundMech_A4B2');data.from_pydata(vertices,[],faces);data.update()
mesh=bpy.data.objects.new('SK_GroundMech_A4B2',data);scene.collection.objects.link(mesh)
for role in mats:
    m=bpy.data.materials.new('M_'+role);data.materials.append(m)
for p,(m,smooth) in zip(data.polygons,face_materials):p.material_index=m;p.use_smooth=smooth
for b in rig.data.bones:
    indices=[i for i,n in enumerate(weights) if n==b.name]
    if indices:mesh.vertex_groups.new(name=b.name).add(indices,1,'REPLACE')
mod=mesh.modifiers.new('RigidSkin','ARMATURE');mod.object=rig;mesh.parent=rig
mesh.select_set(True);bpy.context.view_layer.objects.active=mesh
bpy.context.view_layer.update()
dest=OUT/'SK_GroundMech_A4B2.fbx'
bpy.ops.export_scene.fbx(filepath=str(dest),use_selection=True,object_types={'MESH','ARMATURE'},global_scale=1,apply_unit_scale=True,apply_scale_options='FBX_SCALE_ALL',use_space_transform=True,bake_space_transform=False,axis_forward='-Y',axis_up='Z',use_mesh_modifiers=True,mesh_smooth_type='FACE',add_leaf_bones=False,use_armature_deform_only=False,bake_anim=False,path_mode='AUTO',use_custom_props=False)
report={'success':True,'source':str(source_path),'source_sha256':hashlib.sha256(source_path.read_bytes()).hexdigest(),'fbx':str(dest),'fbx_sha256':hashlib.sha256(dest.read_bytes()).hexdigest(),'materials':mats,'parts':parts_report,'bones':[b.name for b in rig.data.bones],'vertices':len(vertices),'dimensions_cm':[max(v[i] for v in vertices)-min(v[i] for v in vertices) for i in range(3)],'min_cm':[min(v[i] for v in vertices) for i in range(3)],'max_cm':[max(v[i] for v in vertices) for i in range(3)]}
(OUT/'export_report.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps({k:v for k,v in report.items() if k not in ['parts','bones']}))
