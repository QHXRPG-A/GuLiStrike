import bpy,sys,json,traceback
sys.path.insert(0,'D:/UE5.7/test1/Scripts/Blender')
import mech_production_common as m
report={'success':False,'renders':[]}
try:
    bpy.context.window.scene=bpy.data.scenes['Mechs_Production_v1'];m.stage()
    args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
    keys=args or ['SpiderMech','Mecha_01','Mecha_02','Mech_Lightest','FireWeapon_01','MissileWeapon_01','Machinegun_lvl1','Missile_01']
    for key in keys:
        for view in ['Hero']:
            path=m.render([key],key,view);report['renders'].append(path)
            (m.OUT/'preview_render_status.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    report['success']=True
except Exception:report['error']=traceback.format_exc()
(m.OUT/'preview_render_status.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
