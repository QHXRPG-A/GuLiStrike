"""Read-only editor inventory for the approved burning task."""
import json
import traceback
from pathlib import Path
import unreal
root=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
out=root/'Artifacts/BurningRogue_20261029'
out.mkdir(parents=True,exist_ok=True)
report={'read_only':True,'assets':{}}
bp=unreal.load_asset('/Game/GuLiStrike/GroundMech/BP_GroundMech_Light')
cls=bp.generated_class() if bp else None
cdo=unreal.get_default_object(cls) if cls else None
report['ground_character']=bp.get_path_name() if bp else None
report['ground_mesh']=None
try:
    mesh=cdo.get_component_by_class(unreal.SkeletalMeshComponent) if cdo else None
    asset=mesh.get_editor_property('skeletal_mesh_asset') if mesh else None
    report['ground_mesh']=asset.get_path_name() if asset else None
except Exception:
    report['ground_mesh_error']=traceback.format_exc()
for path in ['/Game/GuLiStrike/Cards/GroundMech/Ricochet/Materials/M_Ricochet_RegisteredLayers_Builtin_v1',
             '/Game/Assets/VFX/Movement/AdvancedDashVFX/NiagaraVFX/NS_Dash_06',
             '/Game/Assets/Niagara_Toon_Projectiles_2/Effects/NS_Hit_15',
             '/Game/Assets/Niagara_Toon_Projectiles_2/Effects/NS_Hit_1',
             '/Game/GuLiStrike/FX/BulletTrails/NS_GroundMachineGunTracer']:
    obj=unreal.load_asset(path)
    info={'exists':obj is not None,'class':obj.get_class().get_name() if obj else ''}
    if isinstance(obj,unreal.Material):
        info['customs']=[{'desc':e.get_editor_property('desc'),'code':e.get_editor_property('code'),'inputs':[str(i.get_editor_property('input_name')) for i in e.get_editor_property('inputs')]} for e in unreal.ObjectIterator(unreal.MaterialExpressionCustom) if e.get_outer()==obj]
        info['scalars']=[str(p) for p in unreal.MaterialEditingLibrary.get_scalar_parameter_names(obj)]
        info['vectors']=[str(p) for p in unreal.MaterialEditingLibrary.get_vector_parameter_names(obj)]
    if isinstance(obj,unreal.NiagaraSystem):
        try: info['summary']=str(unreal.NiagaraService.summarize(path))
        except Exception: info['summary_error']=traceback.format_exc()
    report['assets'][path]=info
path=out/'editor_input_inventory.json'
path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({'report':str(path),'ground_mesh':report['ground_mesh'],'assets':{k:v['exists'] for k,v in report['assets'].items()}},ensure_ascii=False))
