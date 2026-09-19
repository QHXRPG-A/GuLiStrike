import bpy,json
import numpy as np
from pathlib import Path
from mathutils import Vector
ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919');OUT=ROOT/'Production_v6_Swap';OUT.mkdir(exist_ok=True);(OUT/'Previews').mkdir(exist_ok=True)
def linear(h):
    c=[int(h[i:i+2],16)/255 for i in (0,2,4)];return np.array([v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in c]+[1],np.float32)
leg=bpy.data.objects['WORK_Mech_Lightest__Mech_Legs_Lt.001'];attr=leg.data.color_attributes['SourceRegion_CleanPaint']
colors=np.empty(len(attr.data)*4,np.float32);attr.data.foreach_get('color',colors);colors=colors.reshape(-1,4)
selected=np.max(np.abs(colors-linear('E3E6E0')),axis=1)<1e-6
assert selected.any();colors[selected]=linear('587F9B');attr.data.foreach_set('color',colors.reshape(-1))
fairing=bpy.data.objects['WORK_Mech_Lightest__Sealed_Armor_Fairing'];mat=fairing.data.materials[0];mat.name='PART6_LightMech_ClosedArmor_White'
mat.node_tree.nodes['Paint_x_Three_Tones'].inputs[1].default_value=linear('E3E6E0')
next(n for n in mat.node_tree.nodes if n.type=='BSDF_PRINCIPLED').inputs['Base Color'].default_value=linear('E3E6E0')
baseline=json.loads((ROOT/'Production_v3_InkCel/production_report.json').read_text());key='Mech_Lightest'
scene=bpy.data.scenes['Review_'+key];bpy.context.window.scene=scene;bpy.context.view_layer.update();cam=scene.camera
bounds=baseline['assets'][key]['bounds'];lo=Vector(bounds['min']);hi=Vector(bounds['max']);center=(lo+hi)/2;span=max(hi-lo)
for view,d in [('Hero',Vector((1.5,-2,1.05))),('Front',Vector((0,-1,0))),('Side',Vector((1,0,0))),('Rear',Vector((0,1,0)))]:
    cam.location=center+d.normalized()*span*3;rot=(center-cam.location).to_track_quat('-Z','Y');cam.rotation_euler=rot.to_euler()
    pts=[Vector((x,y,z)) for x in (lo.x,hi.x) for y in (lo.y,hi.y) for z in (lo.z,hi.z)];right=rot@Vector((1,0,0));up=rot@Vector((0,1,0))
    w=max(p.dot(right) for p in pts)-min(p.dot(right) for p in pts);h=max(p.dot(up) for p in pts)-min(p.dot(up) for p in pts);cam.data.ortho_scale=max(w,h*scene.render.resolution_x/scene.render.resolution_y)*1.12
    scene.render.filepath=str(OUT/'Previews'/(key+'_'+view+'.png'));bpy.ops.render.render(write_still=True)
changed=[]
for o in bpy.data.collections['WORK_'+key].objects:
    if o.type!='MESH' or o.get('ink_layer'):continue
    for material in o.data.materials:
        for e in material.node_tree.nodes['THREE_TONE_Shadow_Mid_Light'].color_ramp.elements:changed.append((e,e.color[:]));e.color=(1,1,1,1)
scene.render.filepath=str(OUT/'Previews/Mech_Lightest_Rear_Paint.png');bpy.ops.render.render(write_still=True)
for e,c in changed:e.color=c
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'Mechs_ComponentColors_v6.blend'),check_existing=False)
report=json.loads((ROOT/'Production_v5_Parts/component_paint_report.json').read_text());report['source']=str(ROOT/'Production_v5_Parts/Mechs_ComponentColors_v5.blend');report['revision']='6 blue/white swapped per user'
for part in report['parts'][leg.name]:part['color']='587F9B'
report['parts'][fairing.name][0]['color']='E3E6E0';report['swapped_color_corners']=int(selected.sum())
(OUT/'component_paint_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print('BLUE_WHITE_SWAP_READY',flush=True)
