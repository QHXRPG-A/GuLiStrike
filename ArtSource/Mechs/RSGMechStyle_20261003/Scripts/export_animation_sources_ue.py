"""Read-only FBX extraction of the seven original animations for Blender review."""
import hashlib
import json
from pathlib import Path
import unreal

root = Path('D:/UE5.7/test1/ArtSource/Mechs/RSGMechStyle_20261003')
source = json.loads((root / 'Source/source_manifest.json').read_text(encoding='utf-8'))
out = root / 'Source/Animations'
out.mkdir(parents=True, exist_ok=True)
records = []
options = unreal.FbxExportOption()
options.ascii = False
options.collision = False
options.level_of_detail = False
option_notes = {}
for key, value in [('export_preview_mesh', False), ('map_skeletal_motion_to_root', False), ('export_morph_targets', False)]:
    try:
        options.set_editor_property(key, value)
        option_notes[key] = value
    except Exception:
        option_notes[key] = 'property_unavailable'
for row in source['animations']:
    asset = unreal.load_asset(row['path'])
    assert isinstance(asset, unreal.AnimSequence)
    path = out / (asset.get_name() + '.fbx')
    if not path.exists():
        task = unreal.AssetExportTask()
        task.object = asset
        task.filename = str(path)
        task.automated = True
        task.prompt = False
        task.replace_identical = False
        task.exporter = unreal.AnimSequenceExporterFBX()
        task.options = options
        assert unreal.Exporter.run_asset_export_task(task), row['path']
    assert path.is_file() and path.stat().st_size > 0
    records.append({**row, 'file': str(path.relative_to(root)), 'bytes': path.stat().st_size,
                    'sha256': hashlib.sha256(path.read_bytes()).hexdigest()})
assert len(records) == 7
report = {'animations': records, 'options': option_notes, 'ue_assets_saved': False,
          'source_modified': False, 'stage': 'Blender_B_animation_source_extraction'}
(out / 'animation_source_manifest.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'success': True, 'animations': len(records),
    'bytes_total': sum(r['bytes'] for r in records), 'options': option_notes, 'ue_assets_saved': False}))
