"""Production readback of the saved symmetric-paint candidate."""
import bpy,json,hashlib
import numpy as np
from pathlib import Path
ROOT=Path('D:/UE5.7/test1/ArtSource/Mechs/StyleUnification_20260919');OUT=ROOT/'Production_v4_Accents'
assert Path(bpy.data.filepath).resolve()==(OUT/'Mechs_SymmetricAccents_v4.blend').resolve()
before=json.loads((ROOT/'Production_v3_InkCel/production_report.json').read_text())
report=json.loads((ROOT/'Production_v3_InkCel/saved_readback.json').read_text())
report.pop('review_images');report.pop('render_delivery_complete');report['source']=bpy.data.filepath
def bhash(data,prop,n,dtype=np.float32):
    a=np.empty(n,dtype);data.foreach_get(prop,a);return hashlib.sha256(a.tobytes()).hexdigest()
def digest(me):
    h=hashlib.sha256()
    for data,prop,n,dtype in [(me.vertices,'co',len(me.vertices)*3,np.float32),(me.loops,'vertex_index',len(me.loops),np.int32)]:
        a=np.empty(n,dtype);data.foreach_get(prop,a);h.update(a.tobytes())
    return h.hexdigest()
report['accent_readback']={}
for key,row in before['assets'].items():
    for part in row['meshes']:
        ob=bpy.data.objects[part['object']];assert digest(ob.data)==part['geometry_before']
        if 'StyleRestPosition' not in ob.data.attributes:continue
        coordinates_equal=bhash(ob.data.vertices,'co',len(ob.data.vertices)*3)==bhash(ob.data.attributes['StyleRestPosition'].data,'vector',len(ob.data.vertices)*3)
        symmetric=True
        for mat in ob.data.materials:
            n=mat.node_tree.nodes;origin=n['Rest_Local_Paint_Coordinates'];separate=origin.outputs['Vector'].links[0].to_node
            xlinks=separate.outputs['X'].links
            symmetric &= len(xlinks)==1 and xlinks[0].to_node.operation=='ABSOLUTE'
            assert len(n['THREE_TONE_Shadow_Mid_Light'].color_ramp.elements)==3
        assert coordinates_equal and symmetric
        report['accent_readback'][ob.name]={'geometry_hash_matches_v3':True,'rest_position_equals_mesh_position':coordinates_equal,'all_X_mask_routes_through_absolute':bool(symmetric)}
sp=bpy.data.objects['WORK_SpiderMech__SpiderMech.002'];src=bpy.data.objects['SRC_SpiderMech__SpiderMech.001']
assert digest(sp.data)==digest(src.data)
assert bhash(sp.data.corner_normals,'vector',len(sp.data.corner_normals)*3)==bhash(src.data.corner_normals,'vector',len(src.data.corner_normals)*3)
assert all(bhash(a.data,'uv',len(a.data)*2)==bhash(b.data,'uv',len(b.data)*2) for a,b in zip(sp.data.uv_layers,src.data.uv_layers))
def weights(o):
    names={g.index:g.name for g in o.vertex_groups}
    return [[(names[g.group],round(g.weight,7)) for g in v.groups] for v in o.data.vertices]
assert weights(sp)==weights(src)
assert len(sp.find_armature().data.bones)==299 and len(sp.data.materials)==10
report['v4_source_contract_rechecked']=True
report['review_images']=sorted(p.name for p in (OUT/'Previews').glob('*.png'))
report['symmetry_note']='v3 base paint readback retained; v4 mask X dependency verified to pass exclusively through ABS, with front/rear actual renders for visual review.'
(OUT/'saved_readback.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
result={'saved_readback':str(OUT/'saved_readback.json'),'unchanged_body_geometry':True,'spider_triangles':len(sp.data.polygons),'symmetry_objects':len(report['accent_readback'])}
