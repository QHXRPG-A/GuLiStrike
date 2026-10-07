"""Read the frozen B_v1 file before creating independent faction candidates."""
import bpy,json
from pathlib import Path
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
O=R/'TeamPalette_B_v2_20261007'
construction=json.loads((R/'Production_B_v1/construction_report.json').read_text(encoding='utf8'))
result={'version':bpy.app.version_string,'binary':bpy.app.binary_path,
        'scene_names':[s.name for s in bpy.data.scenes],'assets':[],'material_info':[]}
for a in construction['assets']:
    b=bpy.data.objects[a['lods'][0]['body']]
    result['assets'].append({'key':a['key'],'scene':a['scene'],'rig':a['rig'],'body':b.name,
        'attributes':[(x.name,x.domain,x.data_type) for x in b.data.attributes],
        'palette_sample':list({tuple(round(v,6) for v in c.color) for c in b.data.color_attributes['SSF_PaletteLinear'].data})[:15] if 'SSF_PaletteLinear' in b.data.color_attributes else [],
        'materials':[m.name for m in b.data.materials]})
    if a['key'] in ('AirBase','CommandCenter'):
        for m in b.data.materials:
            result['material_info'].append({'name':m.name,'diffuse':list(m.diffuse_color),
                'nodes':[{'name':n.name,'type':n.type,'attribute':getattr(n,'attribute_name',None),
                    'operation':getattr(n,'operation',None),'layer_name':getattr(n,'layer_name',None),
                    'inputs':[(i.name,list(i.default_value) if hasattr(i.default_value,'__len__') else i.default_value) for i in n.inputs if not i.is_linked and hasattr(i,'default_value')],
                    'outputs':[(i.name,list(i.default_value) if hasattr(i.default_value,'__len__') else i.default_value) for i in n.outputs if n.type=='RGB' and hasattr(i,'default_value')],
                    'image':n.image.name if n.type=='TEX_IMAGE' and n.image else None} for n in m.node_tree.nodes] if m.use_nodes else []})
(O/'source_inspection.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
print('SSF_TEAM_PALETTE_INSPECTED',bpy.app.version_string,flush=True)
