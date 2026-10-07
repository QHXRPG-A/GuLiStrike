import bpy,json
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004/Production_B_v1')
bpy.ops.wm.open_mainfile(filepath=str(R/'ControlRigMech_B_v1_Production.blend'))
print(json.dumps({'scenes':[(s.name,s.users,s.use_fake_user,len(s.objects)) for s in bpy.data.scenes],
                  'active':bpy.context.scene.name,'actions':[a.name for a in bpy.data.actions],
                  'review_objects':[o.name for o in bpy.data.objects if o.name.startswith('B_Review')]},indent=2),flush=True)
