"""Read-only navigation metadata/audio/deployment inventory (no binary file reads)."""
import json
from pathlib import Path
import unreal
ROOT=Path(unreal.Paths.project_dir())
exec((ROOT/'Scripts/Scale020/capture_editor_baseline.py').read_text(encoding='utf-8').split('def main():')[0])
OUT=ROOT/'TestResults/Scale020/auxiliary-baseline.json'

def main():
    if OUT.exists():
        return {'already_captured':True}
    r=unreal.AssetRegistryHelpers.get_asset_registry()
    assets={}
    for a in r.get_assets_by_path('/Game/GuLiStrike',True):
        cls=str(a.asset_class_path.asset_name)
        if cls in ('SoundAttenuation','SoundCue','NiagaraEffectType'):
            assets[str(a.package_name)]={'class':cls,'properties':properties(a.get_asset())}
    result={'assets':assets,'viewport':str(unreal.EditorLevelLibrary.get_level_viewport_camera_info())}
    OUT.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    return {'assets':[(k,v['class']) for k,v in assets.items()],'viewport':result['viewport']}

unreal.MCPythonHelper.submit_result(json.dumps(main(),ensure_ascii=False))
