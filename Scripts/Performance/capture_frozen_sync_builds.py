"""Freeze synchronization builds and compare different DLLs in fresh owned editors.

Never installs a build while an Unreal/Coordinator process is alive. Source
snapshots and binaries are immutable evidence; the candidate is restored after
the comparison. This is the replacement for same-build mask 31/223 A/B.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

from capture_live_pie import ROOT, editor, capture_hotspot, status, stop_owned_pie

ENGINE = Path('D:/UnrealEngine-5.7/Engine/Binaries/Win64')
BINARIES = ['Binaries/Win64/UnrealEditor-' + name + '.dll'
            for name in ('GuLiStrike', 'GuLiNetCore', 'GuLiStrikeEditor')]
BINARIES += ['Binaries/Win64/UnrealEditor.modules']
IDENTITY = {'game_dll': BINARIES[0], 'core_dll': BINARIES[1], 'editor_dll': BINARIES[2],
            'map': 'Content/Maps/LVL_CommanderMassPrototype.umap',
            'manifest': 'Config/Hybrid/RuntimeManifest-600.json',
            'coordinator': 'Tools/GuLiCoordinator/build/windows/Release/GuLiCoordinator.exe'}
COORDINATOR_RUNTIME = ['Tools/GuLiCoordinator/build/windows/Release/' + name
                       for name in ('boost_json-vc143-mt-x64-1_92.dll', 'sqlite3.dll')]
EXCLUDED = {'Intermediate', 'Saved', 'Binaries', 'DerivedDataCache', 'Downloads', '__pycache__'}


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding='utf-8')


def engine_identity():
    """Check the known engine manifest entries without scanning Binaries.

    A source-engine incremental link can retain BuildId. Record each manifest
    entry's file identity as well, so such a rebuild cannot silently cross A/B.
    Hash the executable and primary runtime modules in addition to metadata.
    """
    manifest = ENGINE / 'UnrealEditor.modules'
    modules = json.loads(manifest.read_text())
    files = {}
    primary = {'Core', 'CoreUObject', 'Engine', 'UnrealEd', 'Slate', 'SlateCore',
               'RenderCore', 'Renderer', 'RHI', 'D3D12RHI', 'NavigationSystem'}
    for name, relative in modules['Modules'].items():
        path = ENGINE / relative
        if not path.is_file():
            files[name] = {'path': relative, 'exists': False}
            continue
        info = path.stat()
        row = {'path': relative, 'size': info.st_size, 'modified_ns': info.st_mtime_ns}
        if name in primary:
            row['sha256'] = digest(path)
        files[name] = row
    return {'manifest_sha256': digest(manifest), 'build_id': modules['BuildId'],
            'executable_sha256': digest(ENGINE / 'UnrealEditor.exe'), 'modules': files}


def inactive():
    result = subprocess.run(['powershell', '-NoProfile', '-Command',
        "@(Get-Process -Name UnrealEditor,UnrealEditor-Cmd,GuLiCoordinator -ErrorAction SilentlyContinue).Count"],
        check=True, text=True, capture_output=True)
    if int(result.stdout.strip()):
        raise RuntimeError('An existing UE or Coordinator process prevents build replacement')


def freeze(out, implementation, mask):
    inactive()
    out.mkdir(parents=True, exist_ok=False)
    sources = []
    for folder in ('Source', 'Scripts', 'Config', 'Tools/GuLiCoordinator/src', 'Tools/GuLiCoordinator/tests'):
        for parent, directories, files in os.walk(ROOT / folder):
            directories[:] = [d for d in directories if d not in EXCLUDED]
            sources.extend(Path(parent) / name for name in files
                           if Path(name).suffix not in ('.pyc', '.uasset', '.umap', '.uexp', '.ubulk', '.pak'))
    sources += [ROOT / relative for relative in ('GuLiStrike.uproject', 'Tools/GuLiCoordinator/CMakeLists.txt',
                'Tools/GuLiCoordinator/vcpkg.json', 'Tools/GuLiCoordinator/build.ps1')]
    source_hashes = {}
    for source in sources:
        relative = source.relative_to(ROOT)
        target = out / 'source' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        source_hashes[relative.as_posix()] = digest(target)
    save(out / 'source-hashes.json', source_hashes)
    with (out / 'working-tree.patch').open('wb') as stream:
        subprocess.run(['git', 'diff', '--binary', 'HEAD', '--', 'Source', 'Scripts', 'Config',
                        'Tools/GuLiCoordinator/src', 'Tools/GuLiCoordinator/tests'], cwd=ROOT, stdout=stream, check=True)
    files = {}
    for relative in dict.fromkeys(BINARIES + list(IDENTITY.values()) + COORDINATOR_RUNTIME):
        target = out / 'artifacts' / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / relative, target)
        files[relative] = digest(target)
    build_id = json.loads((ROOT / BINARIES[-1]).read_text())['BuildId']
    if build_id != json.loads((ENGINE / 'UnrealEditor.modules').read_text())['BuildId']:
        raise RuntimeError('Project and source engine BuildId differ')
    data = {'schema': 'hybrid-frozen-build/v1', 'implementation': implementation, 'mask': mask,
            'captured_utc': datetime.now(timezone.utc).isoformat(), 'build_id': build_id,
            'files': files, 'identity': {key: files[path] for key, path in IDENTITY.items()},
            'coordinator_runtime': {path: files[path] for path in COORDINATOR_RUNTIME},
            'engine': engine_identity(),
            'config': {key: value for key, value in source_hashes.items() if key.startswith('Config/')}}
    save(out / 'build.json', data)
    print(json.dumps({'frozen': str(out), 'implementation': implementation, 'source_files': len(sources)}), flush=True)


def install(directory, data):
    inactive()
    if data.get('engine') != engine_identity():
        raise RuntimeError('Frozen engine files changed; matching BuildId alone is insufficient')
    if not data.get('coordinator_runtime'):
        raise RuntimeError('Frozen Coordinator dependency identity is missing')
    for relative, expected in data['coordinator_runtime'].items():
        if digest(ROOT / relative) != expected:
            raise RuntimeError('Frozen Coordinator runtime dependency changed: ' + relative)
    for key in ('map', 'manifest', 'coordinator'):
        if digest(ROOT / IDENTITY[key]) != data['identity'][key]:
            raise RuntimeError('Frozen nonimplementation input changed: ' + key)
    for relative, expected in data['config'].items():
        if digest(ROOT / relative) != expected:
            raise RuntimeError('Frozen configuration changed: ' + relative)
    for relative in BINARIES:
        source = directory / 'artifacts' / relative
        if digest(source) != data['files'][relative]:
            raise RuntimeError('Frozen artifact was modified: ' + str(source))
        shutil.copy2(source, ROOT / relative)
    if data['build_id'] != json.loads((ENGINE / 'UnrealEditor.modules').read_text())['BuildId']:
        raise RuntimeError('Frozen build cannot load on the current source engine')


def one_window(directory, data, output, scenario, recipe, high_load, continuous=0, quick=False, window_messages=False):
    install(directory, data)
    # Existing pre-PIE navigation preparation rewrites the map package, including
    # its save metadata. Both processes must load the identical starting package.
    # Preserve the saved result for inspection and restore our input after exit.
    map_input = output.parent / (output.name + '-map-input.umap')
    shutil.copy2(ROOT / IDENTITY['map'], map_input)
    command = [str(ENGINE / 'UnrealEditor.exe'), str(ROOT / 'GuLiStrike.uproject'),
               '/Game/Maps/LVL_CommanderMassPrototype', '-NoSplash', '-NoLiveCoding',
               '-ExecCmds=gs.SceneUI.IndexedGeometry 0,Slate.EnableGlobalInvalidation 0,t.IdleWhenNotForeground 0,Slate.bAllowThrottling 0']
    if window_messages:
        command[-1] += ',WindowsApplication.EnableWindowMessageProfilerScopes 1'
    process = subprocess.Popen(command, cwd=ROOT)
    ready = False
    try:
        deadline = time.monotonic() + 120
        while time.monotonic() < deadline:
            if process.poll() is not None:
                raise RuntimeError('Owned editor exited before readiness')
            try:
                current = status()
                if current.get('editor_pid') == process.pid and not current.get('active'):
                    ready = True
                    break
            except (OSError, RuntimeError):
                pass
            time.sleep(2)
        if not ready:
            raise RuntimeError('Owned editor bridge did not become ready')
        # The bridge can answer while startup is still pumping game-thread
        # tasks. Queue PIE after the editor has finished that startup tail.
        time.sleep(5)
        capture_hotspot(output, scenario, data['mask'], recipe, 5 if quick else 15, workers=2,
                        high_load=high_load, continuous_seconds=continuous, start_immediately=quick,
                        quick_preparation=quick, slate_windows=quick)
        capture = json.loads((output / 'capture.json').read_text())
        capture['windows_message_diagnostics'] = window_messages
        save(output / 'capture.json', capture)
        if capture['identity'] != data['identity']:
            raise RuntimeError('Loaded capture differs from the frozen build inputs')
        if capture['sync_identity']['implementation'] != data['implementation']:
            raise RuntimeError('Unexpected loaded synchronization implementation')
    finally:
        cleanup_errors = []
        forced = False
        if ready:
            try:
                stop_owned_pie()
            except Exception as error:
                cleanup_errors.append('PIE stop: ' + str(error))
            try:
                editor("r={'success':True}\nunreal.MCPythonHelper.submit_result(json.dumps(r))\nunreal.SystemLibrary.quit_editor()")
            except Exception as error:
                cleanup_errors.append('Editor quit request: ' + str(error))
            try:
                process.wait(timeout=60)
            except subprocess.TimeoutExpired:
                cleanup_errors.append('Owned editor exceeded its normal exit deadline')
                forced = True
                process.terminate()
                process.wait(timeout=15)
        elif process.poll() is None:
            # Readiness failed before this runner could create a PIE room.
            # Retain the failure and clean only the process started above;
            # this forced startup cleanup is never a normal-exit pass.
            forced = True
            cleanup_errors.append('Owned editor failed startup readiness')
            process.terminate()
            process.wait(timeout=15)
        output.mkdir(parents=True, exist_ok=True)
        save(output / 'editor-exit.json', {'pid': process.pid, 'exit_code': process.returncode,
             'forced': forced, 'cleanup_errors': cleanup_errors})
        if process.poll() is not None:
            rewritten = output / 'map-after-editor-save.umap'
            shutil.copy2(ROOT / IDENTITY['map'], rewritten)
            shutil.copy2(map_input, ROOT / IDENTITY['map'])
        if forced or cleanup_errors or process.returncode != 0:
            raise RuntimeError('Owned editor shutdown failed; see editor-exit.json')


def compare(args):
    builds = {name: json.loads((path / 'build.json').read_text())
              for name, path in (('A', args.baseline), ('B', args.candidate))}
    if builds['A']['identity']['game_dll'] == builds['B']['identity']['game_dll']:
        raise ValueError('A frozen-build comparison requires distinct game DLLs')
    if builds['A']['implementation'] != 'hybrid-sync-selectable-v1' or builds['A']['mask'] != 31:
        raise ValueError('The frozen control must actually execute the old synchronous paths')
    if builds['B']['implementation'] != 'hybrid-sync-unique-v2' or builds['B']['mask'] != 31:
        raise ValueError('Final comparison requires the unique optimized implementation')
    for key in ('map', 'manifest', 'coordinator'):
        if builds['A']['identity'][key] != builds['B']['identity'][key]:
            raise ValueError('Nonimplementation inputs differ: ' + key)
    if builds['A']['config'] != builds['B']['config']:
        raise ValueError('A/B configurations differ')
    if not builds['A'].get('engine') or builds['A']['engine'] != builds['B'].get('engine'):
        raise ValueError('A/B source engine files differ')
    if not builds['A'].get('coordinator_runtime') or builds['A']['coordinator_runtime'] != builds['B'].get('coordinator_runtime'):
        raise ValueError('A/B Coordinator runtime dependencies differ')
    args.output.mkdir(parents=True, exist_ok=False)
    common_map = None
    if args.prepared_map:
        prepared = args.prepared_map.resolve()
        if prepared != (ROOT / IDENTITY['map']).resolve():
            raise ValueError('Prepared map must be the exact project map loaded by both builds')
        common_map = {'original_frozen_sha256': builds['A']['identity']['map'],
                      'prepared_sha256': digest(prepared),
                      'reason': 'Common map after the existing editor navigation preparation/save; original frozen builds remain intact'}
        target = args.output / 'common-inputs' / prepared.name
        target.parent.mkdir()
        shutil.copy2(prepared, target)
        for build in builds.values():
            build['identity']['map'] = common_map['prepared_sha256']
    scenarios = args.scenarios or (['static'] if args.continuous_seconds else ['interaction'] if args.regression_600 else ['static', 'interaction', 'combat'])
    report = {'schema': 'hybrid-frozen-pairs/v1', 'builds': builds, 'baseline_mask': 31, 'enabled_mask': 31,
              'workers': 2, 'high_load': not args.regression_600, 'scenarios': scenarios,
              'purpose': '600-regression' if args.regression_600 else 'high-load-promotion',
              'pair_count': 1 if args.regression_600 or args.quick else 3, 'order': ['A/B'] if args.quick else ['A/B', 'B/A', 'A/B'],
              'quick_comparison': args.quick, 'long_soak_required': not args.quick,
              'common_prepared_map': common_map,
              'windows': [], 'complete': False}
    report_path = args.output / ('continuous-paired.json' if args.continuous_seconds else 'paired.json')
    try:
        for scenario in scenarios:
            recipe = None
            orders = [('A', 'B')] if args.continuous_seconds or args.regression_600 or args.quick else [('A', 'B'), ('B', 'A'), ('A', 'B')]
            for pair, order in enumerate(orders, 1):
                for variant in order:
                    name = f'{scenario}-{pair:02d}-{variant}'
                    output = args.output / name
                    row = {'directory': name, 'variant': variant, 'pair': pair, 'scenario': scenario, 'valid': False}
                    report['windows'].append(row)
                    save(report_path, report)
                    one_window(args.baseline if variant == 'A' else args.candidate,
                               builds[variant], output, scenario, recipe, not args.regression_600, args.continuous_seconds, args.quick)
                    row['valid'] = True
                    if recipe is None:
                        recipe = output / 'recipe.json'
                    save(report_path, report)
        report['complete'] = True
    finally:
        save(report_path, report)
        # An active editor blocks this operation instead of overwriting a DLL
        # beneath a user or failed process. Preserve that failure for diagnosis.
        install(args.candidate, builds['B'])


def sample(args):
    """Measure a current frozen implementation without inventing an A/B control."""
    data=json.loads((args.candidate/'build.json').read_text())
    args.output.mkdir(parents=True,exist_ok=False)
    report={'schema':'hybrid-current-build/v1','build':data,'high_load':not args.regression_600,
            'windows_message_diagnostics':args.window_messages,
            'windows':[],'complete':False,'purpose':'current performance and responsiveness; no old-build speedup claim'}
    try:
        for scenario in args.scenarios:
            row={'directory':scenario,'scenario':scenario,'valid':False};report['windows'].append(row)
            save(args.output/'sample-plan.json',report)
            one_window(args.candidate,data,args.output/scenario,scenario,None,not args.regression_600,
                       quick=True,window_messages=args.window_messages)
            row['valid']=True
        report['complete']=True
    finally:
        save(args.output/'sample-plan.json',report)
        install(args.candidate,data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='action', required=True)
    snapshot = commands.add_parser('freeze')
    snapshot.add_argument('--output', required=True, type=Path)
    snapshot.add_argument('--implementation', required=True,
                          choices=('hybrid-sync-selectable-v1', 'hybrid-sync-unique-v2'))
    snapshot.add_argument('--mask', type=int, required=True, choices=(31, 223))
    run = commands.add_parser('compare')
    run.add_argument('--output', required=True, type=Path)
    run.add_argument('--baseline', required=True, type=Path)
    run.add_argument('--candidate', required=True, type=Path)
    run.add_argument('--quick', action='store_true', help='User-requested short test: one pair per scenario, no soak or fixed logical-age wait')
    run.add_argument('--scenarios', nargs='+', choices=('static', 'interaction', 'combat'))
    run.add_argument('--prepared-map', type=Path, help='Explicit common project map prepared and saved by the existing editor navigation hook')
    mode = run.add_mutually_exclusive_group()
    mode.add_argument('--continuous-seconds', type=int, default=0, choices=(0, 2700))
    mode.add_argument('--regression-600', action='store_true')
    current=commands.add_parser('sample',help='Short current-build capture; no paired comparison')
    current.add_argument('--candidate',required=True,type=Path)
    current.add_argument('--output',required=True,type=Path)
    current.add_argument('--scenarios',nargs='+',choices=('static','interaction','combat'),default=['interaction'])
    current.add_argument('--regression-600',action='store_true')
    current.add_argument('--window-messages',action='store_true',help='Diagnostic only: existing engine per-message CPU scopes; no game or quality changes')
    args = parser.parse_args()
    if args.action == 'freeze':
        freeze(args.output.resolve(), args.implementation, args.mask)
    elif args.action=='compare':
        compare(args)
    else:
        sample(args)


if __name__ == '__main__':
    main()
