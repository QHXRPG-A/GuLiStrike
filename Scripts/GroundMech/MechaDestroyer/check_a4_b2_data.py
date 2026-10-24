"""Narrow B2 checks for rigid binding, independent panels and existing controls."""
from pathlib import Path
import bpy, json, math
from mathutils import Vector

code=Path(__file__).with_name('check_a4_blender_data.py').read_text(encoding='utf8')
for old,new in [
    ('Production_A4_B_v1','Production_A4_B_v2'),
    ('GuLi.GroundMech.ApprovedA4.B1','GuLi.GroundMech.ApprovedA4.B2'),
    ('GMA4_','GMA4B2_'),('GroundMech_A4_','GroundMech_A4B2_'),
    ('Cannon_Box_','Cannon_Volume_'),('Hip_ArmorPlate_','Hip_FullArmor_'),
    ('Chest_Armor_','Chest_IndependentArmor_'),
    ("m.get('tone_factors',[])[:]","[list(v) if hasattr(v, '__iter__') else v for v in m.get('tone_factors',[])]"),
    ('Not assessed by assistant; user review separate','B2 revision in progress; no visual approval or no-difference conclusion')
]:code=code.replace(old,new)
code=code[:code.rfind('bpy.ops.wm.save_as_mainfile')]
code=code.replace('json.dumps(result,ensure_ascii=False,indent=2)','json.dumps(result,ensure_ascii=False,indent=2,default=list)')
exec(compile(code,__file__,'exec'),globals())

bpy.context.window.scene=bpy.data.scenes['GroundMech_A4B2_Construction']
bpy.context.view_layer.update()
rig=bpy.data.objects['GMA4B2_Rig_BLUE']
result['chest_parts']=[]
for side in ['R','L']:
    p=bpy.data.objects['GMA4B2_Chest_IndependentArmor_'+side]
    mount=bpy.data.objects['GMA4B2_Chest_ContinuousCreamFrame_'+side]
    if p.data==mount.data:result['errors'].append({'chest':side,'not_separate_mesh':True})
    if p.get('nominal_depth_m',0)<.1:result['errors'].append({'chest':side,'insufficient_solid_depth':True})
    result['chest_parts'].append({'object':p.name,'distinct_mesh':p.data.name,'mount_mesh':mount.data.name,
                                 'depth_m':p['nominal_depth_m'],'proud_of_mount_m':p['face_proud_of_mount_m'],
                                 'bone':p['rigid_bone'],'vertices':len(p.data.vertices)})

result['gun_axis_checks']=[]
spec=json.loads((OUT/'geometry_report.json').read_text(encoding='utf8'))['sockets']
for s in spec:
    if not s['name'].startswith('Muzzle'):continue
    bone=rig.data.bones[s['bone']]
    direction=Vector(s['direction']).normalized()
    axis=bone.matrix_local.to_3x3().col[0].normalized()
    error=(Vector(s['position'])-bone.head_local).cross(direction).length
    alignment=axis.dot(direction)
    if error>1e-5 or alignment<.99999:result['errors'].append({'bone':bone.name,'axis_error_m':error,'alignment':alignment})
    result['gun_axis_checks'].append({'bone':bone.name,'axis_error_m':error,'alignment':alignment})
result['success']=not result['errors']
(OUT/'blender_data_checks.json').write_text(json.dumps(result,ensure_ascii=False,indent=2,default=list),encoding='utf8')
print('A4_B2_DATA_CHECKS '+json.dumps({'success':result['success'],'errors':result['errors'],'parts':result['rigid_weight_parts'],'chest_parts':result['chest_parts'],'gun_axes':result['gun_axis_checks']}))
