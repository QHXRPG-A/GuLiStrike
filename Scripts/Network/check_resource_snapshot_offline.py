"""Build and exercise real Hybrid core contracts without UE, PIE or sockets.

The simulation uses deterministic in-memory delivery and bounded virtual time.
Static checks protect the intended storage and single-path structure; the C++
contracts check state changes, rejection and callback order. This does not
claim a live Unreal or wall-clock performance result.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
DEFAULT_CMAKE = Path('C:/Program Files/Microsoft Visual Studio/2022/Professional/'
                     'Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')


def audit():
    files = {
        'replicas': 'Source/GuLiNetCore/Public/GuLiNetClient.h',
        'store': 'Source/GuLiNetCore/Private/GuLiNetResourceSnapshot.cpp',
        'recovery': 'Source/GuLiNetCore/Private/GuLiNetResourceRecovery.cpp',
        'current': 'Source/GuLiNetCore/Private/GuLiNetCurrentState.cpp',
        'async': 'Source/GuLiNetCore/Private/GuLiNetAsyncClient.cpp',
        'options': 'Source/GuLiNetCore/Public/GuLiNetAsyncClient.h',
        'game': 'Source/GuLiStrike/Network/GuLiHybridWorldSubsystem.cpp',
        'mask': 'Source/GuLiStrike/Gameplay/Performance/GuLiHotspotOptions.cpp',
    }
    source = {name: (ROOT / path).read_text(encoding='utf-8-sig') for name, path in files.items()}
    checks = {
        'sending_and_latest_are_single_slots': 'std::optional<ResourceSnapshot> Sending,LatestPending,Acknowledged;' in source['replicas'],
        'latest_pending_replaces_obsolete_version': 'R.LatestPending=std::move(Snapshot)' in source['recovery'],
        'backup_keeps_two_complete_cuts': 'while (Complete.size()>2) Complete.pop_front()' in source['store'],
        'integrity_before_storage': source['store'].index('ResourceSnapshotDigest(Candidate)!=Candidate.Digest')
            < source['store'].index('Complete.push_back'),
        'bounded_snapshot_size': 'Size>MaxPrivateStateBytes' in source['store'],
        'request_epoch_cleanup': 'Numbers.SealResourceEpoch(Group,Epoch)' in source['recovery']
            and 'RequestBodies.erase(Event)' in source['current'],
        'sync_switches_removed': all(word not in source[name] for name in ('replicas', 'async', 'options', 'game')
            for word in ('ReducedWork', 'IncrementalDelivery')),
        'polling_projection_removed': 'RefreshQueryState' not in source['game'],
        'unique_build_identity': 'hybrid-sync-unique-v2' in source['mask'] and 'return Value&~RetiredSyncBits' in source['mask'],
        'result_rings_no_front_erase': all(word not in source['game'] for word in
            ('TransactionResultOrder.RemoveAt', 'DamageResultOrder.RemoveAt')),
    }
    return {'checks': checks, 'passed': all(checks.values()),
            'source_sha256': {files[name]: hashlib.sha256(text.encode()).hexdigest() for name, text in source.items()}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--build-dir', type=Path, help='Reuse an existing GuLiNetCoreAcceptance CMake build')
    parser.add_argument('--cmake', type=Path, default=DEFAULT_CMAKE if DEFAULT_CMAKE.is_file() else Path(shutil.which('cmake') or 'cmake'))
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    report = {'schema': 'hybrid-resource-offline/v1', 'scope': 'production C++ core, in-memory transport; no UE/PIE',
              'passed': False, 'static': audit(), 'commands': []}

    def run(command, name, timeout=180):
        with (out / name).open('w', encoding='utf-8') as log:
            completed = subprocess.run([str(v) for v in command], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
        report['commands'].append({'args': [str(v) for v in command], 'exit_code': completed.returncode, 'log': name})
        if completed.returncode:
            raise RuntimeError(f'{name} failed with exit {completed.returncode}')

    try:
        if not report['static']['passed']:
            raise RuntimeError('Static structural checks failed')
        build = args.build_dir.resolve() if args.build_dir else out / 'build'
        if not args.build_dir:
            project = out / 'cmake'
            project.mkdir()
            (project / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.24)\nproject(ResourceOffline LANGUAGES CXX)\n'
                f'add_subdirectory("{(ROOT / "Source/GuLiNetCore").as_posix()}" GuLiNetCore)\n'
                f'add_executable(GuLiNetCoreAcceptance "{(ROOT / "Tools/GuLiCoordinator/tests/ClientAcceptance.cpp").as_posix()}")\n'
                'target_compile_features(GuLiNetCoreAcceptance PRIVATE cxx_std_20)\n'
                'target_link_libraries(GuLiNetCoreAcceptance PRIVATE "$<LINK_LIBRARY:WHOLE_ARCHIVE,GuLiNetCore>")\n', encoding='utf-8')
            run([args.cmake, '-S', project, '-B', build], 'configure.log')
        run([args.cmake, '--build', build, '--config', 'Release', '--target', 'GuLiNetCoreAcceptance', '--parallel', '4'], 'build.log')
        executable = build / 'Release/GuLiNetCoreAcceptance.exe'
        if not executable.is_file():
            executable = build / 'GuLiNetCoreAcceptance'
        run([executable], 'contracts.log', 90)
        log = (out / 'contracts.log').read_text(encoding='utf-8', errors='replace')
        markers = ['Resource snapshot full/delta/integrity/retention contracts passed',
                   'Async snapshot capture clock retention and orphan release contracts passed',
                   'Resource interruption and complete-install activation contracts passed',
                   *[f'Resource interruption cutpoint passed phase={n}' for n in range(4)],
                   'Resource disconnected backup bounds passed',
                   'Abrupt rollback and consecutive resource recovery contracts passed lethal=0',
                   'Abrupt rollback and consecutive resource recovery contracts passed lethal=1',
                   'Resource history scaling:',
                   'Current mining lease restore, exclusivity, continuation and release contracts passed',
                   'Async current mining snapshot readback and callback order contracts passed',
                   'Client ownership contracts passed; protocol=32 manifest=24 handoff=3']
        report['contracts'] = {marker: marker in log for marker in markers}
        with executable.open('rb') as binary:
            report['binary_sha256'] = hashlib.file_digest(binary, 'sha256').hexdigest()
        report['passed'] = all(report['contracts'].values())
        if not report['passed']:
            raise RuntimeError('Expected contract completion evidence is missing')
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        (out / 'result.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
        print(json.dumps({'passed': report['passed'], 'result': str(out / 'result.json')}, ensure_ascii=False), flush=True)


if __name__ == '__main__':
    main()
