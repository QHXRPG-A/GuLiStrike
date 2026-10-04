import bpy,json,hashlib
from pathlib import Path
from mathutils import Vector

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'Delivery_UE_v1'
metadata=json.loads((OUT/'vat_metadata.json').read_text(encoding='utf8'))
reports=[]
for lod in range(4):
    for kind in ['SK','SM']:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        filename=f'SK_Pioneer_LOD{lod}.fbx' if kind=='SK' else f'SM_Pioneer_VAT_LOD{lod}.fbx'
        bpy.ops.import_scene.fbx(filepath=str(OUT/'FBX'/filename),use_custom_normals=True)
        meshes=[o for o in bpy.data.objects if o.type=='MESH']
        tri=sum(sum(len(p.vertices)-2 for p in o.data.polygons) for o in meshes)
        expected=metadata['lods'][lod]
        expected_tri=expected['body_triangles']+expected['outline_triangles']
        # Blender Mesh.validate merges 2 identical body faces and 1 contour face in LOD2,
        # and one duplicate body face in LOD3.
        # The FBX and frozen source retain them. Report the reader's cleanup explicitly.
        removed={2:3,3:1}.get(lod,0)
        assert tri==expected_tri-removed, (filename,tri)
        item={'file':filename,'triangles':tri,'material_sections':sum(len(o.data.materials) for o in meshes),'sha256':hashlib.sha256((OUT/'FBX'/filename).read_bytes()).hexdigest()}
        item['authored_triangles']=expected_tri
        item['blender_reader_duplicate_faces_removed']=removed
        if kind=='SK':
            rigs=[o for o in bpy.data.objects if o.type=='ARMATURE']
            assert len(rigs)==1 and len(rigs[0].data.bones)==44, (filename,len(rigs[0].data.bones))
            bones=rigs[0].data.bones
            for b in metadata['bones']:
                assert b['name'] in bones
                parent=metadata['bones'][b['parent_index']]['name'] if b['parent_index']>=0 else None
                assert (bones[b['name']].parent.name if bones[b['name']].parent else None)==parent
            for o in meshes:
                for v in o.data.vertices:
                    assigned=[g.weight for g in v.groups if g.weight>1e-6]
                    assert len(assigned)==1 and abs(assigned[0]-1)<1e-6
            item['bones']=44; item['rigid_weights']=True
        else:
            assert all(len(o.data.uv_layers)==3 for o in meshes), [(o.name,[u.name for u in o.data.uv_layers]) for o in meshes]
            for o in meshes:
                for uv in o.data.uv_layers[2].data:
                    bi=round(uv.uv.x*44-.5)
                    assert 0<=bi<44 and abs(uv.uv.x-(bi+.5)/44)<1e-5
                    assert uv.uv.y in [0,1,2,3]
            points=[o.matrix_world@v.co for o in meshes for v in o.data.vertices]
            width=max(p.y for p in points)-min(p.y for p in points)
            item['width_cm']=width*100
            assert lod!=0 or abs(width*100-625)<0.02
            item['uv_channels']=3
        reports.append(item)
for clip in metadata['clips']:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    filename=clip['source_action']+'.fbx'
    bpy.ops.import_scene.fbx(filepath=str(OUT/'FBX'/filename))
    rigs=[o for o in bpy.data.objects if o.type=='ARMATURE']
    assert len(rigs)==1 and len(rigs[0].data.bones)==44
    assert len(bpy.data.actions)==1
    action=bpy.data.actions[0]
    frames=round(action.frame_range[1]-action.frame_range[0])+1
    assert frames==clip['frame_count'], (filename,frames,clip['frame_count'])
    reports.append({'file':filename,'bones':44,'frames':frames,'duration_seconds':(frames-1)/30,'sha256':hashlib.sha256((OUT/'FBX'/filename).read_bytes()).hexdigest()})
manifest=json.loads((ROOT/'Production_B_v1/review_manifest.json').read_text(encoding='utf8'))
assert all(hashlib.sha256((ROOT/a['path']).read_bytes()).hexdigest()==a['sha256'] for a in manifest['artifacts'])
report={'passed':True,'approved_artifacts_unchanged':len(manifest['artifacts']),'fbx':reports}
(OUT/'Reports/fbx_readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('PIONEER_FBX_READBACK '+json.dumps(report))
