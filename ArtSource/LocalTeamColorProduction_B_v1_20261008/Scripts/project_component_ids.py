import bpy,json,sys
from pathlib import Path
from mathutils import Vector
from bpy_extras.object_utils import world_to_camera_view
sys.path.insert(0,str(Path(__file__).parent))
import build_palette_only as c
out={}
for key in ['DefaultSoldier','SSF_AirBase','SSF_CloningCenter','SSF_CommandCenter','SSF_MilitaryFactory','SSF_Reactor','SSF_StrategyCenter']:
    bpy.ops.wm.open_mainfile(filepath=str(c.O/'Models'/(key+'_PaletteOnly_B_v1.blend')),load_ui=False)
    s=bpy.data.scenes['Blue_'+key];bpy.context.window.scene=s
    report=json.loads((c.O/'Reports'/(key+'_production.json')).read_text(encoding='utf8'))
    maps=report['teams'][0]['region_mapping'];records=maps[next(n for n in maps if 'LOD0' in n)]
    records=[r for r in records if r['faces']>=18 and max(r['dimensions'])>.5]
    cam=s.camera;hero=cam.matrix_world.copy();hero_scale=cam.data.ortho_scale
    rec=report['teams'][0]['view'];lo=Vector(rec['lo']);hi=Vector(rec['hi']);center=Vector(rec['center']);xyz=hi-lo
    out[key]={}
    for view,direction in [('Hero',None),('Front',(1,0,0) if key=='DefaultSoldier' else (0,-1,0)),('Left',(0,-1,0) if key=='DefaultSoldier' else (-1,0,0))]:
        if direction:
            cam.data.ortho_scale=max(xyz.z,xyz.x/(s.render.resolution_x/s.render.resolution_y),xyz.y/(s.render.resolution_x/s.render.resolution_y))*1.15
            cam.location=center+Vector(direction)*max(xyz)*4;cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
        bpy.context.view_layer.update()
        out[key][view]=[dict(id=r['source_component'],role=r['role'],xy=list(world_to_camera_view(s,cam,Vector(r['center'])))[:2],dims=r['dimensions']) for r in records]
    cam.matrix_world=hero;cam.data.ortho_scale=hero_scale
(c.O/'Reports/component-id-projections.json').write_text(json.dumps(out,indent=2),encoding='utf8')
print('READONLY_COMPONENT_ID_PROJECTIONS_READY')
