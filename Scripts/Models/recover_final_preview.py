"""Bounded recovery after the long preview session; no persistent gameplay edits."""
import json
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT / 'Artifacts/ModelRegistryRuntime20261008'


def run():
    previous = json.loads((OUT / 'dark-surface-adjustment.json').read_text(encoding='utf8'))
    rows = []
    for entry in previous['parents']:
        path = entry['path']
        parent = unreal.load_asset(path)
        codes = [e.get_editor_property('code') for e in unreal.ObjectIterator(unreal.MaterialExpressionCustom)
                 if e.get_outer() == parent]
        rows.append({'path': path, 'saved_lighter_code': any('GuLiDarkSurfaceFill_v1' in code for code in codes)})
    result = {'persisted_parents': len(rows), 'parents': rows,
              'success': len(rows) == 22 and all(r['saved_lighter_code'] for r in rows)}
    (OUT / 'recovered-shading-readback.json').write_text(json.dumps(result, indent=2), encoding='utf8')
    if not result['success']:
        raise RuntimeError('Saved shade revision missing; do not launch preview.')
    result['pie_configuration'] = unreal.GuLiTeleportQALibrary.configure_pie(2, 2)
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_begin_play()
    return {'success': result['success'], 'persisted_parents': len(rows), 'pie_configuration': result['pie_configuration']}


unreal.MCPythonHelper.submit_result(json.dumps(run()))
