"""Apply the promoted derived material interfaces to the original named slots."""
import json
from pathlib import Path
import unreal
ROOT=Path(r'D:/UE5.7/test1');OUT=ROOT/'ArtSource/ModelInterface_B_20261008'
staging=json.loads((OUT/'engine-candidate-staging.json').read_text(encoding='utf8'))
promotion=json.loads((OUT/'formal-promotion.json').read_text(encoding='utf8'))
paths={b['id']:b['formal'] for b in promotion['replacements']}
formal='/Game/GuLiStrike/Models/TeamColor_v1'
report=[]
for model in staging['staged']:
    for binding in model['bindings']:
        asset=unreal.load_object(None,paths[binding['id']])
        if not isinstance(asset,(unreal.StaticMesh,unreal.SkeletalMesh)):continue
        skeletal=isinstance(asset,unreal.SkeletalMesh)
        field='materials' if skeletal else 'static_materials'
        slots=asset.get_editor_property(field)
        folder=formal+'/'+model['model']+('/'+binding['part'] if binding['part']!='Root' else '')+'/Materials'
        changed=[]
        for i,slot in enumerate(slots):
            if str(slot.material_slot_name) not in binding['slots']:continue
            source=slot.material_interface
            target=folder+('/Instances/' if isinstance(source,unreal.MaterialInstanceConstant) else '/Parents/')+source.get_name()
            material=unreal.load_asset(target)
            if not material:raise RuntimeError('Missing promoted derived material '+target)
            slot.material_interface=material
            slots[i]=slot # Unreal Array iteration yields a struct copy.
            changed.append(dict(slot=str(slot.material_slot_name),material=material.get_path_name()))
        asset.modify();asset.set_editor_property(field,slots)
        assert unreal.EditorAssetLibrary.save_loaded_asset(asset,False)
        c=unreal.new_object(unreal.SkeletalMeshComponent if skeletal else unreal.StaticMeshComponent)
        if skeletal:c.set_skeletal_mesh_asset(asset)
        else:c.set_static_mesh(asset)
        actual={n:c.get_custom_primitive_data_index_for_vector_parameter(n) for n in ('GuLi_TeamPrimary','GuLi_TeamSecondary')}
        actual.update({n:c.get_custom_primitive_data_index_for_scalar_parameter(n) for n in ('GuLi_TeamEnabled','GuLi_TeamLightStrength')})
        report.append(dict(model=model['model'],part=binding['part'],id=binding['id'],slots=changed,cpd=actual))
(OUT/'formal-material-binding.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
errors=[r for r in report if list(r['cpd'].values())!=[8,12,16,17]]
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=not errors,meshes=len(report),errors=errors)))
