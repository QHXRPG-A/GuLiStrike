"""Read-only native Niagara inspection; writes evidence, not packages."""
import json
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir())
exec((ROOT/'Scripts/Scale020/capture_editor_baseline.py').read_text(encoding='utf-8').split('def main():')[0])
registry=unreal.AssetRegistryHelpers.get_asset_registry()
report={}
for a in registry.get_assets_by_path('/Game/GuLiStrike/FX',True):
    path=str(a.package_name)
    cls=str(a.asset_class_path.asset_name)
    if cls=='NiagaraEffectType':
        obj=a.get_asset()
        report[path]={'settings':value(obj.get_editor_property('system_scalability_settings'))}
    if cls=='NiagaraSystem' and ('MiningLaser' in path or 'Explosion' in path):
        emitters=unreal.NiagaraService.list_emitters(path)
        report[path]={'emitters':[],'editable_settings':value(unreal.NiagaraService.get_all_editable_settings(path))}
        for e in emitters:
            name=e.get_editor_property('emitter_name')
            report[path]['emitters'].append(value(unreal.NiagaraEmitterService.get_emitter_properties(path,name)))
(ROOT/'TestResults/Scale020/effect-space-inspection.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'assets':list(report)}))
