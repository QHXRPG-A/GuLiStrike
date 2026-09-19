"""Render native model review views, locked-camera source comparisons and gray models."""
import bpy,sys,json,hashlib,time
from pathlib import Path
sys.path.insert(0,'D:/UE5.7/test1/Scripts/Blender')
import mech_production_common as m
bpy.context.window.scene=bpy.data.scenes['Mechs_Production_v1'];m.stage()
keys=['SpiderMech','Mecha_01','Mecha_02','Mech_Lightest','FireWeapon_01','MissileWeapon_01','Machinegun_lvl1','Missile_01']
status={'success':False,'blend':bpy.data.filepath,'blend_sha256':hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest(),'renders':[]}
dest=m.OUT/'review_b_render_status.json'
def record(path):
    status['renders'].append({'path':str(Path(path).relative_to(m.OUT)),'sha256':hashlib.sha256(Path(path).read_bytes()).hexdigest()});dest.write_text(json.dumps(status,indent=2),encoding='utf-8')
try:
    for key in keys:
        for view in ['Hero','Front','Side','Rear']:record(m.render([key],key,view))
    gray=bpy.data.materials.new('REVIEW_NeutralGray');gray.use_nodes=True
    p=next(n for n in gray.node_tree.nodes if n.type=='BSDF_PRINCIPLED');p.inputs['Base Color'].default_value=(.23,.25,.27,1);p.inputs['Roughness'].default_value=.7;p.inputs['Metallic'].default_value=0
    bpy.context.view_layer.material_override=gray
    for source,label in [(True,'SpiderMech_SourceGray'),(False,'SpiderMech_FinalGray')]:
        for view in ['Hero','Front','Side','Rear']:record(m.render(['SpiderMech'],label,view,source=source))
    bpy.context.view_layer.material_override=None
    status['success']=True
except Exception as e:
    import traceback
    status['error']=str(e);status['traceback']=traceback.format_exc();raise
finally:dest.write_text(json.dumps(status,indent=2),encoding='utf-8')
