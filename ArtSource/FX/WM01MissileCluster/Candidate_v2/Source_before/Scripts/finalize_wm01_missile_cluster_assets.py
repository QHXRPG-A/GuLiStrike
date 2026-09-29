"""Finish the occupied-capacity contract after the new native editor module loads.

Compiles/saves only task-owned assets and reads rendered mesh LODs. No PIE,
benchmark, gameplay launch or change to the production projectile switch.
"""
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
DEST = '/Game/GuLiStrike/FX/WM01Missiles'
LIB = unreal.EditorAssetLibrary
NATIVE = unreal.GuLiCombatEffectAuthoringLibrary
report = {'success': False, 'systems': [], 'mesh_lods': None}
try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assert hasattr(NATIVE, 'get_missile_pod_mesh_diagnostics'), 'New native module is not loaded; obtain build/restart authorization first.'
    for prefix in ['', 'Preview']:
        for quality, lanes in [('Full',24), ('Lite',8), ('Minimal',0)]:
            path = DEST+'/NS_WM01MissileCluster_'+prefix+quality
            system = unreal.load_asset(path)
            if not system and prefix:
                continue
            assert system, path
            error = NATIVE.configure_missile_cluster_system(system)
            assert error is not None, path+' configuration failed'
            result = unreal.NiagaraService.compile_with_results(path)
            diagnostics = NATIVE.get_war_machine_hover_compile_diagnostics(system)
            contract = unreal.NiagaraService.get_parameter(path, 'User.MissileContractVersion')
            entry = {'path':path, 'lanes':lanes, 'contract':str(contract), 'diagnostics':diagnostics,
                     'service_success':bool(result.success), 'errors':[str(e) for e in result.errors]}
            report['systems'].append(entry)
            assert contract is not None, entry
            assert result.success and not result.errors, entry
            assert 'VM ERROR:' not in diagnostics and 'GPU ERROR:' not in diagnostics, entry
            assert diagnostics.count('GPU finished=1 complete=1') == (3 if lanes else 2), entry
            LIB.set_metadata_tag(system, 'GuLi.WM01Missiles.Contract',
                f'v2;GPU;occupied capacity<=64;{lanes} world-history lanes;1.2s tail;.15s handoff;generation-isolated')
            if prefix:
                system.set_editor_property('fixed_bounds',unreal.Box(unreal.Vector(-600,-1600,-600),unreal.Vector(3000,1600,2000)))
            assert LIB.save_loaded_asset(system, False)
            entry['saved'] = True
    mesh = unreal.load_asset('/Game/Commander/Units/Tactical/Cel/WarMachine/Meshes/SM_WarMachine_Rigid')
    report['mesh_lods'] = json.loads(NATIVE.get_missile_pod_mesh_diagnostics(mesh))
    lods = report['mesh_lods'].get('lods', [])
    assert lods and lods[0]['left_vertices'] > 0 and lods[0]['right_vertices'] > 0, report['mesh_lods']
    assert all(l['uv_channels'] >= 3 and l['mixed_pod_triangles'] == 0 and l['nonintegral_parts'] == 0 for l in lods), report['mesh_lods']
    definition = unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_Missile')
    report['production_cluster_enabled'] = bool(definition.get_editor_property('use_missile_cluster_rendering'))
    report['success'] = True
except Exception:
    report['error'] = traceback.format_exc()
output = ROOT/'Artifacts/WM01MissileCards/cluster-finalize.json'
output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'error':report.get('error'),'report':str(output)}))
