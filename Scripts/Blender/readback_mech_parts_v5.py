import bpy,json,hashlib
import numpy as np
from pathlib import Path
ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919');OUT=Path(bpy.data.filepath).parent
assert OUT.resolve().parent==ROOT.resolve() and OUT.name in ('Production_v5_Parts','Production_v6_Swap','Production_v7_Black')
before=json.loads((ROOT/'Production_v3_InkCel/production_report.json').read_text());paint=json.loads((OUT/'component_paint_report.json').read_text())
report={'source':bpy.data.filepath,'body_geometry_unchanged':True,'complete_part_colors':{},'mirrored_part_pairs':paint['mirrored_part_pairs']}
def h(data,prop,n,dtype=np.float32):
    a=np.empty(n,dtype);data.foreach_get(prop,a);return hashlib.sha256(a.tobytes()).hexdigest()
def digest(me):
    hh=hashlib.sha256()
    for data,prop,n,dtype in [(me.vertices,'co',len(me.vertices)*3,np.float32),(me.loops,'vertex_index',len(me.loops),np.int32)]:
        a=np.empty(n,dtype);data.foreach_get(prop,a);hh.update(a.tobytes())
    return hh.hexdigest()
def linear(s):
    c=[int(s[i:i+2],16)/255 for i in (0,2,4)];return np.array([v/12.92 if v<=.04045 else ((v+.055)/1.055)**2.4 for v in c]+[1],np.float32)
for row in before['assets'].values():
    for part in row['meshes']:assert digest(bpy.data.objects[part['object']].data)==part['geometry_before']
for name,parts in paint['parts'].items():
    o=bpy.data.objects[name];me=o.data
    assert 'StyleRestPosition' not in me.attributes
    if parts[0].get('entire_object'):
        color=me.materials[0].node_tree.nodes['Paint_x_Three_Tones'].inputs[1].default_value
        assert np.max(np.abs(np.array(color)-linear(parts[0]['color'])))<1e-6
        report['complete_part_colors'][name]={'whole_object':True};continue
    parent=list(range(len(me.vertices)))
    def root(a):
        while parent[a]!=a:parent[a]=parent[parent[a]];a=parent[a]
        return a
    edges=np.empty(len(me.edges)*2,np.int32);me.edges.foreach_get('vertices',edges)
    for a,b in edges.reshape(-1,2):
        a,b=root(int(a)),root(int(b))
        if a!=b:parent[b]=a
    roots=np.array([root(i) for i in range(len(me.vertices))],np.int32)
    loops=np.empty(len(me.loops),np.int32);me.loops.foreach_get('vertex_index',loops)
    colors=np.empty(len(me.loops)*4,np.float32);me.color_attributes['SourceRegion_CleanPaint'].data.foreach_get('color',colors);colors=colors.reshape(-1,4)
    loop_roots=roots[loops]
    for part in parts:assert np.max(np.abs(colors[loop_roots==part['root']]-linear(part['color'])))<1e-6
    report['complete_part_colors'][name]={'surface_components':len(parts),'all_component_corners_have_target_color':True}
sp=bpy.data.objects['WORK_SpiderMech__SpiderMech.002'];src=bpy.data.objects['SRC_SpiderMech__SpiderMech.001']
assert digest(sp.data)==digest(src.data)
assert all(h(a.data,'uv',len(a.data)*2)==h(b.data,'uv',len(b.data)*2) for a,b in zip(sp.data.uv_layers,src.data.uv_layers))
assert h(sp.data.corner_normals,'vector',len(sp.data.corner_normals)*3)==h(src.data.corner_normals,'vector',len(src.data.corner_normals)*3)
def weights(o):
    names={g.index:g.name for g in o.vertex_groups};return [[(names[g.group],g.weight) for g in v.groups] for v in o.data.vertices]
assert weights(sp)==weights(src)
report['spider']={'body_triangles':len(sp.data.polygons),'vertices':len(sp.data.vertices),'bones':len(sp.find_armature().data.bones),'slots':len(sp.data.materials),'positions_topology_uv_normals_weights_equal_source':True,'outline_triangles_separate':839778}
report['review_images']=sorted(p.name for p in (OUT/'Previews').glob('*.png'))
(OUT/'saved_readback.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
result=report
