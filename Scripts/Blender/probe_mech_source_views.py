import bpy,sys,traceback,json
sys.path.insert(0,'D:/UE5.7/test1/Scripts/Blender')
import mech_production_common as m
try:
    bpy.context.window.scene=bpy.data.scenes['Mechs_Production_v1']
    m.stage()
    paths=[m.render(['Cockpit_Jet'],'Source_Cockpit_Jet',source=True),m.render(['Mecha_01'],'Source_Mecha_01',source=True)]
    (m.OUT/'probe_render_result.json').write_text(json.dumps({'success':True,'paths':paths}),encoding='utf-8')
except Exception:(m.OUT/'probe_render_result.json').write_text(json.dumps({'success':False,'error':traceback.format_exc()}),encoding='utf-8')
