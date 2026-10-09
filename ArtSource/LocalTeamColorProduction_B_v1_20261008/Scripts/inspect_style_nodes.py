import bpy,json
from pathlib import Path
R=Path('D:/UE5.7/test1');O=R/'ArtSource/LocalTeamColorProduction_B_v1_20261008'
report={}
for key,path,name in [('SSF','ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007/SSF_TeamPalette_B_v2.blend','Blue_AirBase_LOD0_Body'),('BiZhiMao','ArtSource/CommanderLOD_20261005/BiZhiMao/BiZhiMao_3Tier.blend','ControlRigMech_LOD0_Body')]:
    bpy.ops.wm.open_mainfile(filepath=str(R/path),load_ui=False)
    mat=bpy.data.objects[name].data.materials[0];nt=mat.node_tree
    report[key]={'nodes':[],'links':[(l.from_node.name,l.from_socket.name,l.to_node.name,l.to_socket.name) for l in nt.links]}
    for n in nt.nodes:
        r={'name':n.name,'type':n.type}
        if n.type=='VECT_MATH':r.update(operation=n.operation,values=[list(i.default_value) if hasattr(i.default_value,'__len__') else i.default_value for i in n.inputs if hasattr(i,'default_value')])
        if n.type=='VALTORGB':r['ramp']=[(e.position,list(e.color)) for e in n.color_ramp.elements];r['interpolation']=n.color_ramp.interpolation
        report[key]['nodes'].append(r)
(O/'Reports/style-node-inspection.json').write_text(json.dumps(report,indent=2),encoding='utf8')
