"""Apply the approved machine-gun scope after the updated native module is loaded.

Only the laser pool, combat catalog, and VFX DataTable are saved. No PIE or
unrelated full laser/material rebuild is performed by this script.
"""
import contextlib
import io
import json
import re
from pathlib import Path
import sys
import traceback
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'ArtSource/MachineGunEffects_20260930'
sys.path.insert(0, str(ROOT / 'Scripts/Vfx'))
from ground_machine_gun_lighting import SYSTEM, configure, readback, require, prop
from vfx_registry import definition, vfx_id

CATALOG = '/Game/GuLiStrike/FX/CommanderWeapons/DA_CommanderCombatEffects'
TABLE = '/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects'
report = {'success': False, 'saved': [], 'runtime_verified': False}
try:
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(), 'Do not modify effects during PIE')
    # Stale native code must fail before importing the shared hit scale: it still uses that ID for muzzles.
    require(hasattr(unreal.GuLiCombatEffectVisualCounters(), 'ground_machine_gun_lights'),
            'Compile and load the updated native module first')
    impact, muzzle = definition('MachineGunImpact'), definition('GroundMachineGunMuzzle')
    require(impact['Scale'] == dict.fromkeys('XYZ', 2.0), 'Expected hit scale 2')
    require(muzzle['Scale'] == dict.fromkeys('XYZ', 1.0), 'Expected independent muzzle scale 1')
    require(impact['ResourcePath'] == muzzle['ResourcePath'], 'Keep the existing flash asset')
    report['lighting'] = configure()
    result = unreal.NiagaraService.compile_with_results(SYSTEM)
    report['compile'] = {'success': bool(prop(result, 'success')), 'errors': list(map(str, prop(result, 'errors'))),
                         'warnings': list(map(str, prop(result, 'warnings')))}
    require(report['compile']['success'] and not report['compile']['errors'], 'Laser Niagara compile failed')
    require(unreal.EditorAssetLibrary.save_asset(SYSTEM, only_if_is_dirty=True), 'Save scoped laser pool')
    report['saved'].append(SYSTEM)

    scope = {'__name__': '__main__', 'GULI_TABLE_FILTER': {'DT_GuLiStrikeVfx_Effects'}}
    with contextlib.redirect_stdout(io.StringIO()):
        exec(compile((ROOT / 'Scripts/import_data_to_engine.py').read_text(encoding='utf-8'),
                     'import_data_to_engine.py', 'exec'), scope)
    imported = json.loads((ROOT / 'Data/tmp_import_report.json').read_text(encoding='utf-8'))
    require(not imported.get('errors') and len(imported['tables']) == 1
            and imported['tables'][0].get('imported'), 'Scoped VFX table import failed')
    report['import'] = imported
    report['saved'].append(TABLE)
    table = require(unreal.load_asset(TABLE), 'Load imported VFX table')
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    report['scales'] = [r for r in rows if r['Name'] in ('MachineGunImpact', 'GroundMachineGunMuzzle')]
    require(len(report['scales']) == 2, 'Read back both source and destination VFX IDs')
    for row in report['scales']:
        expected = impact if row['Name'] == 'MachineGunImpact' else muzzle
        scale = row['Scale'] if isinstance(row['Scale'], dict) else {
            axis: float(value) for axis, value in re.findall(r'([XYZ])=([-+\d.eE]+)', row['Scale'])}
        require(row['Id'] == expected['Id'] and scale == expected['Scale']
                and row['ResourcePath'] == expected['ResourcePath'], 'Imported effect differs from source: ' + row['Name'])

    catalog = require(unreal.load_asset(CATALOG), 'Load combat catalog')
    require(prop(prop(catalog, 'machine_gun_impact'), 'vfx_id') == vfx_id('MachineGunImpact'), 'Shared impact binding changed')
    for key, value in {'maximum_tracer_lights_per_frame': 6, 'tracer_light_radius': 800.0,
                       'tracer_light_brightness': 25.0}.items():
        catalog.set_editor_property(key, value)
    require(unreal.EditorAssetLibrary.save_loaded_asset(catalog, only_if_is_dirty=True), 'Save lighting defaults')
    report['saved'].append(CATALOG)
    report['catalog'] = {key: prop(catalog, key) for key in
                         ('maximum_tracer_lights_per_frame', 'tracer_light_radius', 'tracer_light_brightness')}
    report['lighting_after_save'] = readback()
    report['success'] = True
except Exception:
    report['error'] = traceback.format_exc()
finally:
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'asset-application.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({key: report.get(key) for key in ('success', 'saved', 'compile', 'error')}, ensure_ascii=False))
