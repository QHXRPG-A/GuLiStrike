"""Inspect the vertex-only interchange export, never a gameplay test."""
import bpy,json,numpy as np,argparse,sys
from pathlib import Path
ART=Path('D:/UE5.7/test1/ArtSource/CommanderLOD_20261005/BiZhiMao')
parser=argparse.ArgumentParser();parser.add_argument('--art-dir',type=Path,default=ART)
args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []);ART=args.art_dir
META=json.loads((ART/'vertex_metadata.json').read_text(encoding='utf8'))
assert len(META['lods'])==3 and [x['lod'] for x in META['lods']]==[0,1,2]
report=dict(success=False,version=META['version'],lods=[])
for lod in META['lods']:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=lod['fbx'],use_custom_normals=True,use_anim=False)
    meshes=[o.data for o in bpy.data.objects if o.type=='MESH']
    assert len(meshes)==1 and not any(o.type=='ARMATURE' for o in bpy.data.objects)
    mesh=meshes[0]
    names=[u.name for u in mesh.uv_layers]
    uv=[np.asarray([tuple(v.uv) for v in layer.data]) for layer in mesh.uv_layers]
    assert len(names)==6,(lod['lod'],names)
    assert np.all(np.abs(uv[5][:,0]-lod['lod'])<.001)
    ids=np.floor(uv[3][:,0]*lod['vertex_count']).astype(np.int32)
    assert ids.min()>=0 and ids.max()<lod['vertex_count']
    assert np.all((uv[4]>=-.0001)&(uv[4]<=1.0001))
    mesh.calc_loop_triangles()
    assert len(mesh.loop_triangles)==sum(s['triangles'] for s in lod['sections'])
    assert len(mesh.materials)==len(lod['sections'])
    assert all(p.area>1e-11 for p in mesh.polygons),'Degenerate export polygon'
    report['lods'].append(dict(lod=lod['lod'],uv_channels=names,vertices=len(mesh.vertices),
        triangles=len(mesh.loop_triangles),material_sections=len(mesh.materials),runtime_bones=0,
        animation_vertex_min=int(ids.min()),animation_vertex_max=int(ids.max())))
report['success']=True
(ART/'Reports/vertex_export_readback.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report),flush=True)
