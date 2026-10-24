"""Explicit assistant/operator bridge. Preview is the default; browser requests never reach UE."""
import argparse
import json
from pathlib import Path
import sys

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'Scripts'))
from commander_editor_python import call_editor


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--directory',type=Path,required=True)
    parser.add_argument('--apply',action='store_true')
    parser.add_argument('--population',type=int,default=600)
    parser.add_argument('--skip-navigation',action='store_true')
    args=parser.parse_args();directory=args.directory.resolve()
    if not (directory/'manifest.json').is_file():raise SystemExit('导出目录缺少manifest.json')
    code='''
import json,shutil
from pathlib import Path
import unreal
def run():
    if not hasattr(unreal,'GuLiMapDesignAuthoringLibrary'):
        return {'success':False,'message':'尚未加载地图设计器原生适配。请按项目许可流程编译并重开源码版UE。'}
    directory=__DIRECTORY__
    preview=unreal.GuLiMapDesignAuthoringLibrary.preview_design_import(directory)
    if not preview.success:return {'success':False,'phase':'preflight','message':preview.message}
    if not __APPLY__:return {'success':True,'phase':'preview','report':json.loads(preview.json)}
    applied=unreal.GuLiMapDesignAuthoringLibrary.apply_design_import(directory,__NAV__,True)
    if not applied.success:return {'success':False,'phase':'apply','rolled_back':applied.rolled_back,'message':applied.message}
    # The gameplay contract is unchanged. Refresh the selected installed production profile.
    target=Path(unreal.Paths.project_dir()).resolve()/('Config/Hybrid/RuntimeManifest-'+str(__POPULATION__)+'.json')
    if target.exists():shutil.copy2(target,Path(directory)/'runtime-manifest.previous.json')
    exported=json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(__POPULATION__,str(target)))
    if not exported.get('success',False):return {'success':False,'world_applied':True,'phase':'runtime_manifest','message':exported}
    return {'success':True,'phase':'applied','report':json.loads(applied.json),'runtime_manifest':str(target),'player_acceptance':'pending'}
unreal.MCPythonHelper.submit_result(json.dumps(run(),ensure_ascii=False))
'''
    code=code.replace('__DIRECTORY__',repr(str(directory))).replace('__APPLY__',repr(args.apply)).replace('__NAV__',repr(not args.skip_navigation)).replace('__POPULATION__',repr(args.population))
    result=call_editor(code,timeout=300 if args.apply else 60)
    print(json.dumps(result,ensure_ascii=False,indent=2))
    result_file=directory/('ue-apply-result.json' if args.apply else 'ue-preview-result.json')
    result_file.write_bytes((json.dumps(result,ensure_ascii=False,indent=2)+'\n').encode('utf-8'))
    if not result.get('success') or isinstance(result.get('result'),dict) and result['result'].get('success') is False:raise SystemExit(1)


if __name__=='__main__':main()
