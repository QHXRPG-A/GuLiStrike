"""Inspect the real UE re-export, never read UE binary package content."""
import bpy,json,hashlib
import numpy as np
from pathlib import Path
O=Path('D:/UE5.7/test1/ArtSource/Mechs/ControlRigMechStyle_20261004/UE_Delivery_v1')
meta=json.loads((O/'blender_export_readback.json').read_text(encoding='utf-8'))
file=O/'FBX/UE_Readback_AllLODs.fbx';assert file.exists()
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.fbx(filepath=str(file),use_custom_normals=True,use_anim=False)
rows=[]
meshes=[o for o in bpy.data.objects if o.type=='MESH'];assert len(meshes)==4,[o.name for o in meshes]
meshes.sort(key=lambda o:len(o.data.polygons),reverse=True)
for i,ob in enumerate(meshes):
    me=ob.data;me.calc_loop_triangles()
    sections={}
    for p in me.polygons:sections[me.materials[p.material_index].name]=sections.get(me.materials[p.material_index].name,0)+len(p.vertices)-2
    points=np.asarray([tuple(ob.matrix_world@v.co) for v in me.vertices]);size=np.max(points,axis=0)-np.min(points,axis=0)
    colors=me.color_attributes.active_color;values=np.asarray([tuple(x.color_srgb) for x in colors.data]);unique=np.unique(np.round(values[:,:3]*255).astype(np.int32),axis=0).tolist()
    UV=[]
    for layer in me.uv_layers:
        a=np.asarray([tuple(x.uv) for x in layer.data]);UV.append({'name':layer.name,'min':a.min(axis=0).tolist(),'max':a.max(axis=0).tolist(),'positive_selected_edge_distance_values':int(np.sum((a[:,0]>.001)&(a[:,0]<8)))})
    row={'lod':i,'object':ob.name,'actual_UE_triangles':len(me.loop_triangles),'actual_UE_material_sections':sections,'actual_UE_uv_channels':len(UV),'UV':UV,'dimensions_m':size.tolist(),'fbx_palette_srgb_bytes':unique,'fbx_vertex_count':len(me.vertices)}
    assert row['actual_UE_triangles']==meta['lods'][i]['triangles']
    assert len(UV)==3 and len(sections)==(2 if i<3 else 1)
    assert max(abs(a-b) for a,b in zip(size,meta['lods'][i]['dimensions_m']))<.0001,(i,size,meta['lods'][i]['dimensions_m'])
    assert all(any(max(abs(a-b) for a,b in zip(c,p))<=1 for c in unique) for p in [[85,123,120],[142,58,42],[44,55,53],[213,192,156]])
    rows.append(row)
report={'success':True,'source':'Actual saved UE SkeletalMesh re-export through SkeletalMeshExporterFBX','sha256':hashlib.sha256(file.read_bytes()).hexdigest(),'lods':rows,'palette_max_quantization_error_srgb_bytes':1,'bone_and_animation_validation':'Native UE API reports, not inferred from this FBX import'}
(O/'native_ue_fbx_readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(report,ensure_ascii=False),flush=True)
