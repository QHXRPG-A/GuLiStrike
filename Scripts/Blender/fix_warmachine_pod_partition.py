"""Repair only pod rigid IDs, proving the accepted geometry and UV0 are unchanged."""
import bpy
import hashlib
import importlib.util
import json
import shutil
import struct
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
FOLDER=ROOT/'ArtSource/MechanicalAnimation_20260929/WarMachine'
OUT=ROOT/'ArtSource/WarMachineHover_20260930/PodFix'
OUT.mkdir(parents=True,exist_ok=True)
manifest=json.loads((FOLDER/'manifest.json').read_text(encoding='utf-8'))
source=Path(manifest['source'])
assert hashlib.sha256(source.read_bytes()).hexdigest()==manifest['source_sha256'], 'Accepted source changed'
def signature():
    rows={}
    for obj in bpy.data.objects:
        if obj.type!='MESH':continue
        mesh=obj.data
        h=hashlib.sha256()
        for v in mesh.vertices:h.update(struct.pack('<3f',*v.co))
        for p in mesh.polygons:
            h.update(struct.pack('<2i',len(p.vertices),p.material_index))
            for i in p.vertices:h.update(struct.pack('<i',i))
        for loop in mesh.uv_layers[0].data:h.update(struct.pack('<2f',*loop.uv))
        rows[obj.name]={'vertices':len(mesh.vertices),'faces':len(mesh.polygons),'geometry_uv0_material_hash':h.hexdigest()}
    return rows
for name in ['WarMachine_RigidEditable.blend','SM_WarMachine_Rigid.fbx','manifest.json']:
    if not (OUT/('before_'+name)).exists():shutil.copy2(FOLDER/name,OUT/('before_'+name))
bpy.ops.wm.open_mainfile(filepath=str(FOLDER/'WarMachine_RigidEditable.blend'))
before=signature()
spec=importlib.util.spec_from_file_location('rigid_export',ROOT/'Scripts/Blender/export_mass_rigid_animation.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
module.run('WarMachine')
bpy.ops.wm.open_mainfile(filepath=str(FOLDER/'WarMachine_RigidEditable.blend'))
after=signature()
assert before==after,(before,after)
fresh=json.loads((FOLDER/'manifest.json').read_text(encoding='utf-8'))
changed={name:{'before':old,'after':fresh['partitions'][name]} for name,old in manifest['partitions'].items() if old!=fresh['partitions'][name]}
assert changed and all(x['before']==1 and x['after'] in (10,11) for x in changed.values())
assert all(value in (10,11) for name,value in fresh['partitions'].items() if any(token in name for token in ['WM launcher ','WM rim upper wrap','WM missile pod ','WM missile tube ','WM launch tube ','WM rounded missile ']))
for obj in bpy.data.objects:
    if obj.type=='MESH':
        for face in obj.data.polygons:
            ids={round(1-obj.data.uv_layers[2].data[i].uv.y) for i in face.loop_indices}
            assert len(ids)==1, 'A triangle crosses a visibility boundary'
r={'success':True,'geometry_uv0_material_unchanged':before==after,'signatures':after,'changed_groups':changed,'manifest':str(FOLDER/'manifest.json')}
(OUT/'source-fix.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
print('POD_PARTITION_REPAIR',json.dumps(r))
