"""Read the already validated LOD FBX into explicit, non-baking mesh data."""
import bpy
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
ART=ROOT/'ArtSource/QuadrupedMiner_20261113'
META=json.loads((ART/'Reports/vat_metadata.json').read_text(encoding='utf8'))
READBACK=json.loads((ART/'Reports/vat_readback.json').read_text(encoding='utf8'))
for lod in META['lods']:
    validated=READBACK['lods'][lod['lod']]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=lod['fbx'],use_anim=False)
    obj=next(o for o in bpy.context.scene.objects if o.type=='MESH')
    obj.data.calc_loop_triangles()
    uvs=[]
    for channel in range(3):
        values=[]
        for loop in obj.data.loops:
            values.extend(obj.data.uv_layers[channel].data[loop.index].uv)
        uvs.append(values)
    positions=[]
    for vertex in obj.data.vertices:
        point=obj.matrix_world @ vertex.co
        positions.extend((point.x,-point.y,point.z))
    colors=[]
    layer=obj.data.color_attributes.active_color
    for item in layer.data:
        colors.extend((item.color_srgb[0],item.color_srgb[1],item.color_srgb[2],item.color_srgb[3]))
    normals=[]
    for normal in obj.data.corner_normals:
        normals.extend((normal.vector.x,-normal.vector.y,normal.vector.z))
    triangles=[]
    for tri in obj.data.loop_triangles:
        assert len(tri.loops)==3
        slot=obj.data.polygons[tri.polygon_index].material_index
        triangles.append({'loops':list(tri.loops),'vertices':list(tri.vertices),'material':slot})
    assert len(triangles)==validated['triangles'], (lod['lod'], len(triangles), validated)
    payload={'lod':lod['lod'],'positions':positions,'uvs':uvs,'colors':colors,'normals':normals,'triangles':triangles,
        'evaluated_triangles_before_export':lod['evaluated_triangles'],
        'material_names':[m.name for m in obj.data.materials],'position_ids':lod['position_vertices'],'normal_ids':lod['normal_vertices']}
    out=ART/'Reports'/f'explicit_lod{lod["lod"]}.json'
    out.write_text(json.dumps(payload,ensure_ascii=False,separators=(',',':')),encoding='utf8')
    print('EXPLICIT_LOD',lod['lod'],len(positions)//3,len(triangles),flush=True)
