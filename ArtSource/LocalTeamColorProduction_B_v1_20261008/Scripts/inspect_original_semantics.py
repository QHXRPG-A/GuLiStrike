import bpy,json
from pathlib import Path
O=Path('D:/UE5.7/test1/ArtSource/LocalTeamColorProduction_B_v1_20261008')
report={}
for key,path in [('DefaultSoldier','ArtSource/Mechs/RSGMechStyle_20261003/Delivery_UE_v1/RSGMech_Delivery_UE_v1.blend'),('WM01','ArtSource/MechanicalAnimation_20260929/WarMachine/WarMachine_RigidEditable.blend')]:
    path=Path('D:/UE5.7/test1')/path
    bpy.ops.wm.open_mainfile(filepath=str(path),load_ui=False)
    report[key]={'objects':[{'name':o.name,'materials':[m.name if m else None for m in o.data.materials],'attributes':[(a.name,a.domain,a.data_type) for a in o.data.attributes if a.name not in ('position','sharp_edge','sharp_face','custom_normal')],'groups':[g.name for g in o.vertex_groups][:40]} for o in bpy.data.objects if o.type=='MESH'],'images':[{'name':i.name,'file':i.filepath,'size':list(i.size),'packed':bool(i.packed_file)} for i in bpy.data.images], 'materials':[{'name':m.name,'diffuse':list(m.diffuse_color),'nodes':[{'name':n.name,'type':n.type,'image':n.image.name if n.type=='TEX_IMAGE' and n.image else None,'layer':getattr(n,'layer_name',None)} for n in m.node_tree.nodes] if m.use_nodes else []} for m in bpy.data.materials]}
(O/'Reports/original-semantics.json').write_text(json.dumps(report,indent=2),encoding='utf8')
