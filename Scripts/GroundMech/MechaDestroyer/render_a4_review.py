"""Render saved, real Blender geometry for the user's visual review only."""
import bpy,json,hashlib,time
from pathlib import Path
OUT=Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/Production_A4_B_v1')
start=time.time()
report={'success':False,'blend':bpy.data.filepath,'blend_sha256':hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest(),'outputs':[],
        'purpose':'User comparison with approved A4; no assistant pixel inspection or image tests'}
for name in ['GroundMech_A4_01_TeamReview','GroundMech_A4_02_ThreeViews','GroundMech_A4_03_SingleInspect']:
    scene=bpy.data.scenes[name]
    scene.render.engine='CYCLES';scene.cycles.samples=32;scene.cycles.use_denoising=True
    bpy.ops.render.render(write_still=True,scene=name)
    p=Path(scene.render.filepath);assert p.is_file() and p.stat().st_size>0
    report['outputs'].append({'scene':name,'path':str(p),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
    (OUT/'render_manifest.json').write_text(json.dumps(report,indent=2),encoding='utf8')
report['success']=True;report['seconds']=time.time()-start
(OUT/'render_manifest.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print('GMA4_RENDER_COMPLETE '+json.dumps({'success':True,'images':len(report['outputs']),'seconds':report['seconds']}))
