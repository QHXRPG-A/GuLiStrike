"""After an authorized native rebuild/reload, update only Ground rogue dependencies in place.
Never builds, restarts the editor, starts PIE, or replaces already referenced DataTable objects.
"""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
OUT = ROOT / 'Artifacts/GroundRogue_20261028'
TABLES = ['DT_GuLiStrikeRogueCards_GroundRicochet', 'DT_GuLiStrikeMech_Skills',
          'DT_GuLiStrikeGameTexts_Texts', 'DT_GuLiStrikeRogueCards_Cards']

def main():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if level.is_in_play_in_editor():
        raise RuntimeError('Do not import during an active play session')
    manifest = json.loads((ROOT / 'data/Json/manifest.json').read_text(encoding='utf-8'))['tables']
    effect = unreal.load_class(None, '/Script/GuLiStrike.GuLiRogueCardGroundRicochetEffect')
    missing = []
    if not effect:
        missing.append('GuLiRogueCardGroundRicochetEffect')
    for name in TABLES:
        config = manifest[name]
        struct = unreal.find_object(None, config['struct'])
        cls = getattr(unreal, config['struct'].rsplit('.', 1)[-1], None)
        if not struct or not cls:
            missing.append(config['struct'])
    if not missing:
        # Schema presence, not an old module's ability to silently ignore extra JSON fields.
        checks = [(unreal.GuLiStrikeRogueCardsCardsRow(), 'effect_config_id'),
                  (unreal.GuLiStrikeMechSkillsRow(), 'impact_vfx_id'),
                  (unreal.GuLiStrikeRogueCardsGroundRicochetRow(), 'minimum_hops')]
        for value, prop in checks:
            try:
                value.get_editor_property(prop)
            except Exception:
                missing.append(prop)
    if missing:
        return {'applied': False, 'reason': 'native_schema_not_loaded', 'missing': missing,
                'asset_mutations': False, 'compile_started': False, 'play_started': False}
    imported = []
    assets = unreal.AssetToolsHelpers.get_asset_tools()
    for name in TABLES:
        path = '/Game/GuLiStrike/Data/' + name
        struct = unreal.find_object(None, manifest[name]['struct'])
        table = unreal.load_asset(path)
        if table is None:
            factory = unreal.DataTableFactory()
            factory.set_editor_property('struct', struct)
            table = assets.create_asset(name, '/Game/GuLiStrike/Data', unreal.DataTable, factory)
        if table.get_editor_property('row_struct') != struct:
            raise RuntimeError(f'Unexpected native row type: {name}')
        source = (ROOT / 'data/Json' / (name + '.json')).read_text(encoding='utf-8-sig')
        if not unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, source):
            raise RuntimeError(f'In-place table import failed: {name}')
        expected = json.loads(source)
        names = list(map(str, unreal.DataTableFunctionLibrary.get_data_table_row_names(table)))
        if set(names) != {row['Name'] for row in expected}:
            raise RuntimeError(f'Row identity mismatch after import: {name}')
        readback = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
        for row in expected:
            actual = next(value for value in readback if value['Name'] == row['Name'])
            for field, wanted in row.items():
                value = actual.get(field)
                if isinstance(wanted, str) and (wanted.startswith(('/Game/', '/Script/')) or wanted == ''):
                    if value == 'None':
                        value = ''
                    elif isinstance(value, str) and "'" in value:
                        value = value.split("'", 1)[1].rstrip("'")
                if isinstance(wanted, float):
                    if abs(float(value) - wanted) > max(1e-5, abs(wanted)*1e-6):
                        raise RuntimeError(f'Numeric readback mismatch: {name}.{field}')
                elif value != wanted:
                    raise RuntimeError(f'Readback mismatch: {name}.{field}: {value!r} != {wanted!r}')
        if not unreal.EditorAssetLibrary.save_loaded_asset(table, False):
            raise RuntimeError(f'Could not save {path}')
        imported.append({'asset': path, 'rows': len(names), 'readback': True})
    strings = unreal.load_asset('/Game/GuLiStrike/Data/ST_GuLiStrikeGameTexts')
    texts = unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeGameTexts_Texts')
    if not unreal.GuLiRogueCardPresentationLibrary.rebuild_game_text_string_table(strings, texts):
        raise RuntimeError('StringTable rebuild failed')
    if not unreal.EditorAssetLibrary.save_loaded_asset(strings, False):
        raise RuntimeError('StringTable save failed')
    for key in ['Card.GroundMech.Ricochet.Title', 'Card.GroundMech.Ricochet.Description']:
        text = unreal.TextLibrary.text_from_string_table(unreal.Name(strings.get_path_name()), key)
        table_id, actual_key = unreal.TextLibrary.string_table_id_and_key_from_text(text)
        if actual_key != key or not unreal.TextLibrary.text_is_from_string_table(text):
            raise RuntimeError('StringTable key readback failed: ' + key)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    exported = []
    if world.get_path_name().split('.')[0] == '/Game/Maps/LVL_CommanderMassPrototype':
        for population in (200, 600):
            path = ROOT / f'Config/Hybrid/RuntimeManifest-{population}.json'
            result = json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population, str(path)))
            if not result.get('success'):
                raise RuntimeError('Manifest export failed: ' + json.dumps(result))
            exported.append(result)
    else:
        raise RuntimeError('Open LVL_CommanderMassPrototype to export its current manifest')
    return {'applied': True, 'tables': imported, 'manifests': exported,
            'compile_started': False, 'play_started': False, 'visual_acceptance': 'pending_user'}

if (ROOT/'data/Excel/GuLiStrikeBuffs.xlsx').exists():
    current=ROOT/'Scripts/GroundMech/Rogue/install_burning_cards.py'
    exec(compile(current.read_text(encoding='utf8'),str(current),'exec'),{'__name__':'ground_rogue_current_import'})
else:
    report = main()
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / 'asset_install.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.MCPythonHelper.submit_result(json.dumps(report, ensure_ascii=False))
