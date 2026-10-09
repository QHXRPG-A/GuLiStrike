"""Read Blender sources for palette-only work; never save the source files."""
import bpy, json, hashlib, traceback
from pathlib import Path
ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'ArtSource/LocalTeamColorProduction_B_v1_20261008'
SOURCES={
 'SSF':'ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/SSF_TeamPalette_B_v2.blend',
 'DefaultSoldier':'ArtSource/CommanderLOD_20261005/DefaultSoldier/DefaultSoldier_3Tier.blend',
 'WM01':'ArtSource/CommanderLOD_20261005/WM01/WM01_3Tier.blend',
 'BiZhiMao':'ArtSource/CommanderLOD_20261005/BiZhiMao/BiZhiMao_3Tier.blend',
 'ShieldGenerator':'ArtSource/Buildings/IndustrialDefenseSet/delivery_hardsurface/ShieldGenerator/ShieldGenerator.blend',
 'ManualOutpost':'ArtSource/Buildings/OutpostMonument/OutpostMonument_v01.blend',
 'ResourceFactory':'ArtSource/Buildings/ResourceProcessingFactory/ResourceProcessingFactory_v01.blend',
}
report={}
for key,rel in SOURCES.items():
    try:
        path=ROOT/rel
        bpy.ops.wm.open_mainfile(filepath=str(path), load_ui=False)
        meshes=[o for o in bpy.data.objects if o.type=='MESH']
        if key=='SSF':
            meshes=[o for o in meshes if o.name.startswith('Blue_') and '_LOD0_Body' in o.name]
        objects=[]
        for o in meshes:
            props={k:str(o[k])[:300] for k in o.keys()}
            me=o.data
            objects.append({'name':o.name,'mesh':me.name,'vertices':len(me.vertices),'polygons':len(me.polygons),'dimensions':list(o.dimensions),'matrix':[list(r) for r in o.matrix_world], 'parent':o.parent.name if o.parent else None,'materials':[m.name if m else None for m in me.materials], 'attributes':[(a.name,a.domain,a.data_type) for a in me.attributes if not a.name.startswith('.')], 'UVs':[u.name for u in me.uv_layers], 'props':props,'modifiers':[(m.name,m.type) for m in o.modifiers]})
        mats={m.name:{'diffuse':list(m.diffuse_color),'props':{k:str(m[k])[:200] for k in m.keys()},'nodes':[{'name':n.name,'type':n.type,'attribute':getattr(n,'attribute_name',None),'layer':getattr(n,'layer_name',None),'image':n.image.name if n.type=='TEX_IMAGE' and n.image else None} for n in m.node_tree.nodes] if m.use_nodes else []} for o in meshes for m in o.data.materials if m}
        report[key]={'path':str(path),'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'active_scene':bpy.context.scene.name,'scenes':[s.name for s in bpy.data.scenes],'collections':[c.name for c in bpy.data.collections],'objects':objects,'materials':mats,'actions':[a.name for a in bpy.data.actions],'mesh_props':{o.name:{k:str(o.data[k])[:600] for k in o.data.keys()} for o in meshes}, 'texts':[t.name for t in bpy.data.texts]}
        print('INSPECTED',key,'meshes',len(objects),flush=True)
    except Exception:
        report[key]={'error':traceback.format_exc()}
(OUT/'Reports/source-inspection.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print('INSPECTION_COMPLETE',flush=True)
