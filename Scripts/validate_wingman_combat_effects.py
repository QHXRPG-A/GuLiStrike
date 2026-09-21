"""Static check of the current Commander/Wingman ID-backed visual definitions. No PIE."""
import json
import sys
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/Vfx'))
from vfx_registry import definition, resource, scale

def main():
    definitions={key:unreal.load_asset(path) for key,path in {
        'commander':'/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_Missile',
        'wingman':'/Game/GuLiStrike/FX/WingmanWeapons/DA_WingmanGroundMissile'}.items()}
    fields={r['Name']:r for r in json.loads((ROOT/'Data/Json/DT_GuLiStrikeSpellFields_Fields.json').read_text(encoding='utf-8'))}
    report={'success':False,'projectiles':{},'systems':[]}
    paths=set()
    for key,projectile in definitions.items():
        if not projectile:raise RuntimeError('Missing '+key+' projectile')
        id=projectile.flight_vfx_id
        system=unreal.load_asset(resource(id))
        if not isinstance(system,unreal.NiagaraSystem):raise RuntimeError('Invalid flight VfxId')
        paths.add(resource(id))
        field=projectile.impact_field
        row=fields[str(field.config_id)]
        dynamic=row['RadiusCentimeters']/field.visual_reference_radius
        variants=[]
        for variant in field.activation_variants:
            if not isinstance(unreal.load_asset(resource(variant.vfx_id)),unreal.NiagaraSystem):raise RuntimeError('Invalid explosion VfxId')
            paths.add(resource(variant.vfx_id))
            variants.append({'vfx_id':variant.vfx_id,'base_scale':scale(variant.vfx_id),
                             'final_scale':[v*dynamic for v in scale(variant.vfx_id)],
                             'scale_parameter':str(variant.scale_parameter_name),'lifetime':variant.maximum_lifetime})
            for layer in variant.additional_layers:paths.add(resource(layer.vfx_id))
        report['projectiles'][key]={'flight_vfx_id':id,'flight_scale':scale(id),'variants':variants}
    for path in sorted(paths):
        result=unreal.NiagaraService.compile_with_results(path)
        if not result.success or result.errors:raise RuntimeError('Niagara compile failed: '+path)
        report['systems'].append({'path':path,'compiled':True})
    report['success']=True
    return report

result=main()
out=ROOT/'TestResults/WingmanAttack/vfx-validation.json'
out.parent.mkdir(parents=True,exist_ok=True)
out.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(result))
