import bpy,json,sys,math
from mathutils import Vector
from pathlib import Path
O=Path('D:/UE5.7/test1/ArtSource/LocalTeamColorProduction_B_v1_20261008')
keys=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [p.name.replace('_PaletteOnly_B_v1.blend','') for p in sorted((O/'Models').glob('*.blend'))]
for key in keys:
    bpy.ops.wm.open_mainfile(filepath=str(O/'Models'/(key+'_PaletteOnly_B_v1.blend')),load_ui=False)
    for team in ('Blue','Red'):
        s=bpy.data.scenes[team+'_'+key];bpy.context.window.scene=s
        s.render.filepath=str(O/'Previews'/(key+'_'+team+'_Hero.png'))
        bpy.ops.render.render(write_still=True)
        print('NATIVE_B_PREVIEW',key,team,flush=True)
        cam=s.camera;hero=cam.matrix_world.copy();hero_scale=cam.data.ortho_scale
        dg=bpy.context.evaluated_depsgraph_get()
        obs=[o for o in s.objects if o.type=='MESH' and not o.hide_render]
        pts=[o.evaluated_get(dg).matrix_world@Vector(c) for o in obs for c in o.evaluated_get(dg).bound_box]
        lo=Vector([min(p[i] for p in pts) for i in range(3)]);hi=Vector([max(p[i] for p in pts) for i in range(3)]);center=(lo+hi)/2
        xyz=hi-lo;cam.data.ortho_scale=max(xyz.z,xyz.x/(s.render.resolution_x/s.render.resolution_y),xyz.y/(s.render.resolution_x/s.render.resolution_y))*1.15
        if key in ('DefaultSoldier','WM01','ResourceFactory'):directions={'Front':(1,0,0),'Left':(0,-1,0),'Back':(-1,0,0)}
        elif key=='ManualOutpost':directions={'Front':(-1,0,0),'Left':(0,-1,0),'Back':(1,0,0)}
        else:directions={'Front':(0,-1,0),'Left':(1,0,0),'Back':(0,1,0)}
        for view,direction in directions.items():
            cam.location=center+Vector(direction)*max(xyz)*4;cam.rotation_euler=(center-cam.location).to_track_quat('-Z','Y').to_euler()
            s.render.filepath=str(O/'Previews'/(key+'_'+team+'_'+view+'.png'));bpy.ops.render.render(write_still=True)
        cam.matrix_world=hero;cam.data.ortho_scale=hero_scale
        print('ORTHOGRAPHIC_B_THREE_VIEWS',key,team,flush=True)
