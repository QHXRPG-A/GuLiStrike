"""Supersede the failed global-collapse distance tiers with conservative angular dissolves."""
import bpy,json,shutil,math
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004'); P=R/'Production_B_v1'; O=R/'Production_B_v2'; O.mkdir(exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(P/'ControlRigMech_B_v1_Production.blend'))
s=bpy.data.scenes['PORTABLE_SHADER_LODS']; bpy.context.window.scene=s
rig=bpy.data.objects['Armature']; rig.animation_data.action=None; rig.data.pose_position='REST'
col=bpy.data.collections['PRODUCTION_LODS_SINGLE_BODY_SECTION']
body=bpy.data.objects['ControlRigMech_LOD0_Body']
for lod,angle in ((1,.12),(2,.30),(3,.50)):
    old=bpy.data.objects[f'ControlRigMech_LOD{lod}_Body']; mesh=body.data.copy()
    old.data=mesh
    for n in rig.data.bones.keys():
        if n not in old.vertex_groups: old.vertex_groups.new(name=n)
    for m in list(old.modifiers): old.modifiers.remove(m)
    dec=old.modifiers.new('ConservativeAngularDistanceLOD','DECIMATE'); dec.decimate_type='DISSOLVE'; dec.angle_limit=angle
    dec.use_dissolve_boundaries=False; dec.delimit={'SHARP'}
    bpy.ops.object.select_all(action='DESELECT'); old.hide_set(False); old.select_set(True); bpy.context.view_layer.objects.active=old
    bpy.ops.object.modifier_apply(modifier=dec.name)
    transfer=old.modifiers.new('PreserveNearTierSurfaceNormals','DATA_TRANSFER'); transfer.object=body
    transfer.use_loop_data=True; transfer.data_types_loops={'CUSTOM_NORMAL'}; transfer.loop_mapping='POLYINTERP_NEAREST'
    bpy.ops.object.modifier_apply(modifier=transfer.name)
    arm=old.modifiers.new('Original152BoneBinding','ARMATURE'); arm.object=rig
    mat=body.data.materials[0].copy(); mat.name=f'M_ThreeTone_SafeLOD{lod}'
    mat.node_tree.nodes['InternalLineStrength'].inputs[1].default_value=0 if lod==3 else .6 if lod==2 else 1
    old.data.materials.clear(); old.data.materials.append(mat)
    old['LOD']=lod; old['screen_size']=(1,.4,.16,.06)[lod]; old['production_body']=True
    old['reduction_method']='conservative angular dissolve preserving sharp boundaries and all source pieces, then transfer near-tier surface normals'
    review=bpy.data.objects[f'B_Review_LOD{lod}_Body']; review.data=old.data.copy()
    review.data.materials.clear(); review.data.materials.append(bpy.data.objects['B_Review_LOD0_Body'].data.materials[0])
    old.hide_render=True; old.hide_set(True)
for folder in ('Textures','AnimationSource'):
    shutil.copytree(P/folder,O/folder,dirs_exist_ok=True)
for name in ('construction_report.json','regular_caps_report.json','motion_contact_audit.json'):
    shutil.copy2(P/name,O/name)
report=json.loads((P/'production_render_report.json').read_text(encoding='utf-8')); report['version']='B-v2'
report['revises_B_v1']='withdrawn by assistant visual QA: distance LOD damage and frozen-first-frame video encoding; user did not reject or approve B-v1'
report['lods']=[]
for i in range(4):
    ob=bpy.data.objects[f'ControlRigMech_LOD{i}_Body']; ob.data.calc_loop_triangles()
    outline=bpy.data.objects.get(f'ControlRigMech_LOD{i}_Outline')
    if outline: outline.data.calc_loop_triangles()
    tris=len(ob.data.loop_triangles); otris=len(outline.data.loop_triangles) if outline else 0
    caps=((32000,8000),(14000,3000),(5000,1000),(2000,0))[i]
    report['lods'].append({'lod':i,'body_triangles':tris,'outline_triangles':otris,'total_triangles':tris+otris,
        'caps':{'body':caps[0],'outline':caps[1],'total':sum(caps)},'difference':{'body':max(0,tris-caps[0]),'outline':max(0,otris-caps[1]),'total':max(0,tris+otris-sum(caps))},
        'body_sections':1,'outline_sections':1 if outline else 0,'screen_size':(1,.4,.16,.06)[i],'internal_lines':(1,1,.6,0)[i]})
setup=json.loads((R/'References_A_v2/reference_setup.json').read_text(encoding='utf-8')); cam=setup['cameras']['Hero']
s.camera.location=cam['location_m']; s.camera.rotation_euler=cam['rotation_radians']; s.camera.data.ortho_scale=cam['ortho_scale_m']
s.render.use_freestyle=False; s.render.resolution_x=s.render.resolution_y=2048; s.render.resolution_percentage=100
D=O/'LODComparison'; D.mkdir(exist_ok=True)
for lod in range(4):
    for ob in col.objects: ob.hide_render=ob['LOD']!=lod
    s.render.filepath=str(D/f'ControlRigMech_B_v2_LOD{lod}_SameCamera.png'); bpy.ops.render.render(write_still=True,scene=s.name)
for ob in col.objects: ob.hide_render=ob['LOD']!=0
bpy.context.window.scene=bpy.data.scenes['REVIEW_B_ReferenceMatched']; rig.data.pose_position='POSE'
(O/'production_render_report.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
bpy.ops.wm.save_as_mainfile(filepath=str(O/'ControlRigMech_B_v2_Production.blend'))
print('B_V2_SAFE_LODS_READY',json.dumps(report['lods']),flush=True)
