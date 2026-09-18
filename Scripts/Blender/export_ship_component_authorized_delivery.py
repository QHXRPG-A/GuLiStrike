"""Export Batch02/04 under the explicit formal-release instruction.

Reuse the existing first-batch exporter and FBX/portable Blender readers. The
legacy exporter function names retain 'approved'; metadata is explicitly changed
to release authorization, never claiming a separate unseen visual B approval.
"""
import ast
import collections
import importlib.util
import json
import math
import os
import shutil
import sys
import time
from pathlib import Path
import bpy
import numpy as np
from mathutils import Vector, Matrix
from mathutils.kdtree import KDTree

PROJECT=Path('D:/UE5.7/test1')
BASE=PROJECT/'ArtSource/Ships/ShipComponentStyle_20260917'
batch_number='02' if '--batch02' in sys.argv else '04'
os.environ['SHIP_BATCH02_VERSION']='v2'
spec=importlib.util.spec_from_file_location('authorized_surface',PROJECT/f'Scripts/Blender/style_ship_component_batch{batch_number}_materials.py')
batch=importlib.util.module_from_spec(spec);spec.loader.exec_module(batch)
s=batch.s
DELIVERY=s.ROOT/'Delivery'/batch.VERSION
AUTH=BASE/'UE_Integration/release_authorization_20260917.json'
assert json.loads(AUTH.read_text(encoding='utf-8'))['decision']=='implementation_and_formal_UE_replacement_authorized'
for folder in ('Blender','FBX','Textures','Previews','Parameters','Validation'):
    (DELIVERY/folder).mkdir(parents=True,exist_ok=True)
APPROVAL={'schema':'gulistrike-authorized-delivery-source-lock/v1','authorization':str(AUTH),
    'authorization_sha256':s.filehash(AUTH),'visual_B':'no_separate_explicit_visual_pass','parts':{}}
for key in batch.KEYS:
    source=s.OUT/f'{key}_OriginalMesh_MaterialCandidate.blend'
    report=json.loads((s.OUT/f'{key}_material_report.json').read_text(encoding='utf-8'))
    assert report['body_geometry_unchanged'] and report['blend_sha256']==s.filehash(source)
    APPROVAL['parts'][key]={'approved_blend':str(source.relative_to(s.ROOT)),
        'approved_blend_sha256':s.filehash(source),'original_geometry_sha256':report['original_geometry_sha256']}

reader_path=PROJECT/'ArtSource/Buildings/IndustrialDefenseSet/scripts/validate_exports.py'
exporter_path=PROJECT/'Scripts/Blender/export_ship_component_material_delivery.py'
def reuse(path,names,replacements=None):
    tree=ast.parse(path.read_text(encoding='utf-8'))
    nodes=[n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name in names]
    assert {n.name for n in nodes}==set(names)
    class Adapt(ast.NodeTransformer):
        def visit_Constant(self,node):
            if replacements and isinstance(node.value,str) and node.value in replacements:
                return ast.copy_location(ast.Constant(replacements[node.value]),node)
            return node
    module=ast.Module(body=nodes,type_ignores=[])
    module=Adapt().visit(module);ast.fix_missing_locations(module)
    # Generalize the old three-scene reader to this batch's exact component count.
    for node in ast.walk(module):
        if isinstance(node,ast.Assert) and ast.unparse(node.test)=='len(bpy.data.scenes) == 3':
            node.test=ast.parse('len(bpy.data.scenes) == len(s.SOURCE_FILES)',mode='eval').body
    ast.fix_missing_locations(module)
    exec(compile(module,str(path),'exec'),globals())

reuse(reader_path,('import_file','world_points'))
reuse(exporter_path,('uv_order_for_export','export_material','vertex_owners','portable_image_paths',
    'save_delivery_blend','export_one','max_closest','validate','overview','verify_portable_blends'),{
    'approved: 三件均通过 B，导出 FBX':'direct formal-release authorized by user; separate B visual pass not asserted',
    '//../Parameters/approval_B_20260917.json':'//../Parameters/release_authorization_20260917.json',
    'B approved v4; original geometry retained':'User-authorized formal release; original geometry retained',
    '../Parameters/MaterialParameters_v4.json':'../Parameters/MaterialParameters.json',
    'Use original names/transforms when formal UE integration is separately scheduled':'Restore and verify original socket transforms during authorized formal UE integration'})

def main():
    shutil.copy2(AUTH,DELIVERY/'Parameters'/AUTH.name)
    shutil.copy2(s.ROOT/'approval_A_20260917.json',DELIVERY/'Parameters/approval_A_20260917.json')
    s.dump(DELIVERY/'Parameters/delivery_source_lock.json',APPROVAL)
    parameters={}
    for key in batch.KEYS:
        for role in ('BaseColor','ORM','LineMask'):
            image=s.TEX/f'T_SC_{key}_{role}_2K.png'
            shutil.copy2(image,DELIVERY/'Textures'/image.name)
        source=s.OUT/f'{key}_material_parameters.json'
        parameters[key]=json.loads(source.read_text(encoding='utf-8'))
        shutil.copy2(source,DELIVERY/'Parameters'/source.name)
    s.dump(DELIVERY/'Parameters/MaterialParameters.json',parameters)
    for source in s.PRE.glob('*'):
        if source.suffix in ('.png','.mp4'):
            shutil.copy2(source,DELIVERY/'Previews'/source.name)
    for source in s.OUT.glob('*flame*'):
        shutil.copy2(source,DELIVERY/'Parameters'/source.name)
    expected={key:export_one(key) for key in batch.KEYS}
    reports=[validate(key,row) for key,row in expected.items()]
    s.dump(DELIVERY/'Validation/FBX_Readback_Summary.json',{'passed':all(r['passed'] for r in reports),
        'checks':reports,'reader':str(reader_path),'readback_implementation':str(exporter_path),
        'readback_implementation_sha256':s.filehash(exporter_path),'release_authorization':str(AUTH)})
    overview()
    verify_portable_blends()
    print('AUTHORIZED_SHIP_DELIVERY_COMPLETE',batch_number,batch.VERSION,flush=True)

if __name__=='__main__':
    main()
