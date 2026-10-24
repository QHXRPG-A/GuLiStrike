import unreal,json
from pathlib import Path
path='/Game/MechaDestroyer/Blueprints/BPanim_Mecha'
bp=unreal.load_asset(path)
report={'nodes':[], 'blendspaces':{}}
for cls in [unreal.AnimGraphNode_ModifyBone,unreal.AnimGraphNode_LayeredBoneBlend]:
    for node in unreal.ObjectIterator(cls):
        if node.get_path_name().startswith(path+'.'):
            report['nodes'].append({'name':node.get_name(),'data':str(node.get_editor_property('node'))})
for suffix in ['Walk','Run']:
    bs=unreal.load_asset('/Game/MechaDestroyer/Animation/BlendSpace_Loco_'+suffix)
    report['blendspaces'][suffix]={'axes':[str(a) for a in bs.get_editor_property('blend_parameters')],'samples':[{'animation':str(s.get_editor_property('animation').get_name()),'value':str(s.get_editor_property('sample_value'))} for s in bs.get_editor_property('sample_data')]}
report['docs']={n:getattr(unreal.AnimGraphService,n).__doc__ for n in ['add_modify_bone_node','get_anim_node_info'] if hasattr(unreal.AnimGraphService,n)}
Path('D:/UE5.7/test1/ArtSource/Characters/GroundMech_MechaDestroyer_20261023/animation_contract.json').write_text(json.dumps(report,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report))
