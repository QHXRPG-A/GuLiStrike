"""Native Blender evidence for the two faction palettes, unchanged fixed cameras."""
import bpy,json,hashlib,sys,argparse
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'TeamPalette_B_v2_20261007'
M=json.loads((O/'candidate_manifest.json').read_text(encoding='utf8'));assert M['success']
assert Path(bpy.data.filepath).resolve()==Path(M['candidate_blend']).resolve()
assert hashlib.sha256(Path(M['candidate_blend']).read_bytes()).hexdigest()==M['candidate_blend_sha256']
refs={a['key']:a for a in json.loads((R/'References_A_v7/render_manifest.json').read_text(encoding='utf8'))['assets']}
p=argparse.ArgumentParser();p.add_argument('--phase',choices=['hero','remaining'],default='hero');a=p.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
manifest_file=O/'render_manifest.json'
evidence=json.loads(manifest_file.read_text(encoding='utf8')) if manifest_file.exists() else {'candidate_blend_sha256':M['candidate_blend_sha256'],'renders':[],'success':False}
assert evidence['candidate_blend_sha256']==M['candidate_blend_sha256']

def show(asset,lod):
    for entry in asset['lods']:
        for name in (entry['body'],entry.get('outline')):
            if name:
                ob=bpy.data.objects[name];ob.hide_render=entry['LOD']!=lod;ob.hide_set(entry['LOD']!=lod)

def render(scene,name,details,resolution=(2048,2048)):
    if any(r['name']==name for r in evidence['renders']):return
    bpy.context.window.scene=scene;scene.render.resolution_x,scene.render.resolution_y=resolution
    scene.render.resolution_percentage=100;scene.render.use_freestyle=False
    scene.render.filepath=str(O/'Renders'/(name+'.png'));bpy.ops.render.render(write_still=True)
    record={'name':name,'file':'Renders/'+name+'.png','resolution':list(resolution),'scene':scene.name,'native_Blender_render':True,**details}
    evidence['renders'].append(record);manifest_file.write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf8')
    print('SSF_TEAM_RENDER_READY',name,flush=True)

for asset in M['assets']:
    key,team=asset['key'],asset['team'];scene=bpy.data.scenes[asset['scene']];bpy.context.window.scene=scene;show(asset,0)
    views=['Hero'] if a.phase=='hero' else ['Front','Left','Back']
    for view in views:
        cam=refs[key]['views'][view];scene.camera.location=cam['camera_location_m'];scene.camera.rotation_euler=cam['rotation_rad'];scene.camera.data.ortho_scale=cam['ortho_scale_m']
        render(scene,team+'_'+key+'_'+view,{'team':team,'asset':key,'view':view,'LOD':0,'camera_matches_B_v1_reference':True})
    if a.phase=='remaining':
        cam=refs[key]['views']['Hero'];scene.camera.location=cam['camera_location_m'];scene.camera.rotation_euler=cam['rotation_rad'];scene.camera.data.ortho_scale=cam['ortho_scale_m']
        for lod in (1,2):
            show(asset,lod);render(scene,team+'_'+key+'_Hero_LOD'+str(lod),{'team':team,'asset':key,'view':'Hero','LOD':lod,'camera_matches_B_v1_reference':True})
        show(asset,0)
if a.phase=='hero':
    for team in ('Blue','Red'):
        render(bpy.data.scenes[M[team+'_assembly']['scene']],team+'_Assembly',{'team':team,'view':'Assembly','LOD':0},(4096,4096))
    render(bpy.data.scenes[M['review_scene']['scene']],'Blue_Red_Assembly_Compare',{'view':'Dual_team_assembly','LOD':0},(4096,2600))
evidence['success']=len(evidence['renders'])==75
manifest_file.write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf8')
print('SSF_TEAM_RENDER_PHASE_DONE',a.phase,len(evidence['renders']),flush=True)
