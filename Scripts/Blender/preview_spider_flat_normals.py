import bpy,sys
sys.path.insert(0,'D:/UE5.7/test1/Scripts/Blender')
import mech_production_common as m
bpy.context.window.scene=bpy.data.scenes['Mechs_Production_v1'];m.stage()
o=m.meshes('SpiderMech')[0]
for f in o.data.polygons:f.use_smooth=False
for mod in o.modifiers:
    if mod.type=='WEIGHTED_NORMAL':mod.show_render=False
m.render(['SpiderMech'],'SpiderMech_Flat')
