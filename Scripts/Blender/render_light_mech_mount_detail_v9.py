"""Render the rear seating detail, from above the shoulder occluders."""
import bpy, sys
from pathlib import Path
from mathutils import Vector, Quaternion

out=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/Production_v9_VentMount/Previews')
mode=sys.argv[sys.argv.index('--')+1]
assert mode in ('before','after')
scene=bpy.data.scenes['Review_Mech_Lightest'];bpy.context.window.scene=scene
direction=Quaternion((-.23115986585617065,-.046226710081100464,.19056953489780426,.952948808670044))@Vector((0,0,1))
for side in (('R',) if mode=='before' else ('R','L')):
    sign=1 if side=='R' else -1
    center=Vector((sign*.47,-.54,3.10))
    view=Vector((sign*direction.x,direction.y,direction.z))
    scene.camera.location=center+view.normalized()*12
    scene.camera.rotation_euler=(center-scene.camera.location).to_track_quat('-Z','Y').to_euler()
    scene.camera.data.ortho_scale=1.35
    suffix='_Before' if mode=='before' else ''
    scene.render.filepath=str(out/f'Mech_Lightest_Mount_{side}{suffix}.png')
    bpy.ops.render.render(write_still=True)
print('MOUNT_DETAIL_READY',mode,flush=True)
