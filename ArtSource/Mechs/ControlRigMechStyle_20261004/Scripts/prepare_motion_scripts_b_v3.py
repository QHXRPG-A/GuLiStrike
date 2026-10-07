"""Clone the prior native motion workflow without editing frozen B2 scripts."""
from pathlib import Path
import shutil
R=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004');S=R/'Scripts';O=R/'Production_B_v3'
shutil.copytree(R/'Production_B_v2/AnimationSource',O/'AnimationSource',dirs_exist_ok=True)
for stem in ('render_motion','verify_movies','audit_movie_fidelity'):
 text=(S/f'{stem}_b_v2.py').read_text(encoding='utf-8').replace('B_v2','B_v3').replace('B-v2','B-v3')
 if stem=='render_motion':
  text=text.replace("geometry_hash=json.loads((O/'native_geometry_audit.json').read_text(encoding='utf-8'))['lods'][0]['geometry_skin_normal_sha256']", "geometry_hash=hashlib.sha256((O/'realtime_readback_audit.json').read_bytes()).hexdigest()")
  text=text.replace("s=bpy.data.scenes['REVIEW_B_ReferenceMatched']", "s=bpy.data.scenes['STYLE_REALTIME_B_v3']")
  text=text.replace('s.render.use_freestyle=True','s.render.use_freestyle=False')
  text=text.replace("body=bpy.data.objects['B_Review_LOD0_Body']","body=bpy.data.objects['ControlRigMech_LOD0_Body']")
  text=text.replace("'geometry_skin_normals_sha256':geometry_hash","'saved_body_readback_report_sha256':geometry_hash")
  text=text.replace('native Freestyle actual production body','actual skinned structural-line shader + editable outline shell; Freestyle disabled')
 (S/f'{stem}_b_v3.py').write_text(text,encoding='utf-8')
print('B3 motion scripts and exact source action samples prepared')
