"""Check actual Blender geometry/rig/material facts, never inspect render pixels."""
import bpy,json,math
from pathlib import Path
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view

OUT=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v1')
PREFIX='GMA4_'
scene=bpy.data.scenes['GroundMech_A4_Construction']
previous=bpy.context.window.scene
bpy.context.window.scene=scene
rig=bpy.data.objects[PREFIX+'Rig_BLUE']
red=bpy.data.objects[PREFIX+'Rig_RED']
parts=[o for o in scene.objects if o.type=='MESH' and o.get('author')=='GuLi.GroundMech.ApprovedA4.B1' and not o.get('outline_only')]
errors=[]
for o in parts:
    expected=o['rigid_bone']
    for v in o.data.vertices:
        groups=[g for g in v.groups if g.weight>1e-6]
        if len(groups)!=1 or abs(groups[0].weight-1)>1e-6 or o.vertex_groups[groups[0].group].name!=expected:
            errors.append({'object':o.name,'bad_weight_vertex':v.index});break
    if len(o.data.uv_layers)!=1:errors.append({'object':o.name,'uv_layers':len(o.data.uv_layers)})

def refresh():
    rig.update_tag();red.update_tag();scene.frame_set(scene.frame_current);bpy.context.view_layer.update()

def centroid(name):
    o=bpy.data.objects[PREFIX+name];ev=o.evaluated_get(bpy.context.evaluated_depsgraph_get());me=ev.to_mesh()
    c=ev.matrix_world@(sum((v.co for v in me.vertices),Vector())/len(me.vertices));ev.to_mesh_clear();return c

controls={p:rig[p] for p in ['AimYaw','AimPitch','RightRecoil','LeftRecoil']}
for p in controls:rig[p]=0.0
refresh()
baseline={p:centroid(p) for p in ['Cannon_OpenBore_R','Cannon_OpenBore_L','Cannon_Box_R','Cannon_Box_L','Hip_ArmorPlate_R','Chest_Armor_R']}
rig['RightRecoil']=.1;refresh()
right=(centroid('Cannon_OpenBore_R')-baseline['Cannon_OpenBore_R']);left=(centroid('Cannon_OpenBore_L')-baseline['Cannon_OpenBore_L'])
if abs(right.length-.1)>1e-4 or left.length>1e-5:errors.append({'recoil_right_m':list(right),'unexpected_left_m':list(left)})
rig['RightRecoil']=0;rig['LeftRecoil']=.1;refresh()
left_test=(centroid('Cannon_OpenBore_L')-baseline['Cannon_OpenBore_L']);right_test=(centroid('Cannon_OpenBore_R')-baseline['Cannon_OpenBore_R'])
if abs(left_test.length-.1)>1e-4 or right_test.length>1e-5:errors.append({'recoil_left_m':list(left_test),'unexpected_right_m':list(right_test)})
rig['LeftRecoil']=0;rig['AimYaw']=25;refresh()
leg_move=(centroid('Hip_ArmorPlate_R')-baseline['Hip_ArmorPlate_R']).length
chest_move=(centroid('Chest_Armor_R')-baseline['Chest_Armor_R']).length
if leg_move>1e-5 or chest_move<.02:errors.append({'aim_yaw_chest_move':chest_move,'unexpected_leg_move':leg_move})
for p,v in controls.items():rig[p]=v
refresh()

socket_results=[]
spec=json.loads((OUT/'geometry_report.json').read_text())['sockets']
for item in spec:
    e=bpy.data.objects[PREFIX+item['name']]
    delta=(e.matrix_world.translation-Vector(item['position'])).length
    socket_results.append({'name':item['name'],'bone':e.parent_bone,'reference_position_error_m':delta})
    if delta>1e-4:errors.append({'socket':item['name'],'position_error_m':delta})

materials=[]
for m in {slot.material for o in parts for slot in o.material_slots if slot.material}:
    if m.get('color_role') not in ['Ink','Amber']:
        ramps=[n for n in m.node_tree.nodes if n.type=='VALTORGB']
        if len(ramps)!=1 or len(ramps[0].color_ramp.elements)!=3 or ramps[0].color_ramp.interpolation!='CONSTANT':errors.append({'material':m.name,'three_tones':False})
    materials.append({'name':m.name,'role':m.get('color_role'),'base_tones':m.get('tone_factors',[])[:]})

framing=[]
for scene_name in ['GroundMech_A4_01_TeamReview','GroundMech_A4_02_ThreeViews']:
    target=bpy.data.scenes[scene_name];bounds=[]
    bpy.context.window.scene=target;target.view_layers[0].update()
    for inst in target.objects:
        if inst.instance_type!='COLLECTION' or not inst.instance_collection:continue
        for o in inst.instance_collection.all_objects:
            if o.type!='MESH' or o.get('outline_only'):continue
            bounds += [world_to_camera_view(target,target.camera,inst.matrix_world@o.matrix_world@Vector(p)) for p in o.bound_box]
    lo=[min(p[i] for p in bounds) for i in [0,1]];hi=[max(p[i] for p in bounds) for i in [0,1]]
    framing.append({'scene':scene_name,'normalized_min':lo,'normalized_max':hi})
    if min(lo)<0 or max(hi)>1:errors.append({'scene':scene_name,'camera_clips_geometry':True})

result={'success':not errors,'errors':errors,'rigid_weight_parts':len(parts),'bone_count':len(rig.data.bones),
        'right_recoil_delta_m':list(right),'left_recoil_delta_m':list(left_test),'other_gun_stationary':left.length<1e-5 and right_test.length<1e-5,
        'upper_body_aim_keeps_legs_fixed':leg_move<1e-5,'sockets':socket_results,'materials':materials,'camera_framing':framing,
        'visual_fidelity':'Not assessed by assistant; user review separate','original_reference_mesh_modified':False}
(OUT/'blender_data_checks.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
bpy.context.window.scene=previous
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'GroundMech_A4_Editable_B_v1.blend'))
