"""Compare preserved vehicle near skin weights and reference bone transforms from interchange exports."""
import bpy,json,hashlib,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'Scripts/CommanderLOD'))
from common import ART
data=json.loads((ART/'Reports/stage_native.json').read_text(encoding='utf8'))
def lod_index(ob):
 import re
 while ob:
  found=re.search(r'LOD(\d+)',ob.name)
  if found:return int(found[1])
  ob=ob.parent
 return 0
def signature(file):
 bpy.ops.wm.read_factory_settings(use_empty=True);bpy.ops.import_scene.fbx(filepath=str(file),use_anim=False)
 meshes=[o for o in bpy.data.objects if o.type=='MESH' and lod_index(o)==0]
 assert len(meshes)==1,[o.name for o in meshes]
 ob=meshes[0];rig=next(m.object for m in ob.modifiers if m.type=='ARMATURE')
 vertices=[]
 for vertex in ob.data.vertices:
  weights=tuple(sorted((ob.vertex_groups[g.group].name,round(g.weight,6)) for g in vertex.groups if g.weight>1e-7))
  vertices.append((tuple(round(v,6) for v in vertex.co),weights))
 bones=[(bone.name,bone.parent.name if bone.parent else None,tuple(round(v,5) for row in bone.matrix_local for v in row)) for bone in rig.data.bones]
 digest=lambda x:hashlib.sha256(json.dumps(x,sort_keys=True).encode()).hexdigest()
 return dict(vertex_weight_signature=digest(sorted(vertices)),reference_bone_signature=digest(bones),bones=len(bones),vertices=len(vertices))
rows=[]
for unit in data['units']:
 if unit['name'] not in ['ElectromagneticMiner','ConstructionVehicle']:continue
 item=next(m for m in unit['meshes'] if m['type']=='SkeletalMesh')
 candidate=signature(item['fbx']);source=signature(Path(item['fbx']).with_name(Path(item['fbx']).name.replace('_Candidate','_Original')))
 assert candidate==source,(unit['name'],candidate,source)
 rows.append(dict(name=unit['name'],near_skin_and_reference_pose_preserved=True,**candidate))
(ART/'Reports/skin_readback.json').write_text(json.dumps(dict(success=True,units=rows),indent=2),encoding='utf8')
print(json.dumps(rows),flush=True)
