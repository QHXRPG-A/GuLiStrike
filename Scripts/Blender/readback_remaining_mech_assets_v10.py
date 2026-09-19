"""Read the five delivery FBXs before they are written to Unreal packages."""
import bpy,json,math
from pathlib import Path
from collections import Counter
OUT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919/UE_AllAssets_v10')
export=json.loads((OUT/'export_report.json').read_text());assert export['success']
result={'success':False,'parts':{}}
for name,row in export['parts'].items():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=row['fbx'],use_custom_normals=True)
    meshes=[o for o in bpy.context.scene.objects if o.type=='MESH'];assert len(meshes)==1,name
    o=meshes[0];me=o.data;me.calc_loop_triangles();counts=Counter(t.material_index for t in me.loop_triangles)
    assert len(me.materials)==row['body_material_slots']+int(row['outline_triangles']>0),(name,len(me.materials))
    if row['outline_triangles']:assert counts[row['ink_slot']]==row['outline_triangles'],(name,dict(counts))
    assert sum(v for k,v in counts.items() if k!=row['ink_slot'])==row['body_triangles'],name
    points=[o.matrix_world@v.co for v in me.vertices]
    actual={'min':[min(v[i] for v in points)*100 for i in range(3)],'max':[max(v[i] for v in points)*100 for i in range(3)]}
    error=max(abs(actual[k][i]-row['bounds_cm'][k][i]) for k in ('min','max') for i in range(3))
    assert error<.02,(name,error)
    rigs=[x for x in bpy.context.scene.objects if x.type=='ARMATURE']
    assert sum(len(r.data.bones) for r in rigs)==row['bones'],name
    if rigs:assert all(v.groups for v in me.vertices),name
    result['parts'][name]={'body_triangles':row['body_triangles'],'outline_triangles':row['outline_triangles'],
        'section_triangles':dict(counts),'bones':row['bones'],'bounds_error_cm':error,'all_skeletal_vertices_weighted':bool(rigs),
        'uv_layers':[u.name for u in me.uv_layers],'fbx_sha256':row['fbx_sha256']}
    (OUT/'fbx_readback.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
    print('FBX_READBACK_OK',name,flush=True)
result['success']=True
(OUT/'fbx_readback.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
print('MECH_STYLE_FBX_READBACK_READY',flush=True)
