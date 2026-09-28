"""Run in editor, without PIE: rename through AssetTools, keep compatibility redirectors.

The report lists every source/destination. Never edit or delete uasset files directly.
"""
import json
from pathlib import Path
import unreal
from card_asset_layout import destination


def migrate():
    editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if editor.is_in_play_in_editor():
        raise RuntimeError('Do not migrate assets during play')
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    entries = []
    renames = []
    for root in ['/Game/GuLiStrike/Cards/RevealDemo', '/Game/GuLiStrike/Cards/WarMachineTarot']:
        for data in registry.get_assets_by_path(root, recursive=True):
            if str(data.asset_class_path.asset_name) == 'ObjectRedirector':
                continue
            source = str(data.package_name)
            target = destination(source)
            if source == target:
                continue
            if unreal.EditorAssetLibrary.does_asset_exist(target):
                raise RuntimeError(f'Destination already exists; inspect before merging: {target}')
            obj = data.get_asset()
            if not obj:
                raise RuntimeError(f'Cannot load {source}')
            parent, name = target.rsplit('/', 1)
            renames.append(unreal.AssetRenameData(obj, parent, name))
            entries.append({'source': source, 'destination': target})
    if renames and not unreal.AssetToolsHelpers.get_asset_tools().rename_assets(renames):
        raise RuntimeError('AssetTools did not finish the requested renames')
    for entry in entries:
        if not unreal.EditorAssetLibrary.save_asset(entry['destination'], only_if_is_dirty=False):
            raise RuntimeError(f"Failed to save {entry['destination']}")
    out = Path(unreal.Paths.project_dir()) / 'Artifacts/RogueCards'
    out.mkdir(parents=True, exist_ok=True)
    journal = out / 'asset-layout.json'
    prior = json.loads(journal.read_text(encoding='utf-8')) if journal.exists() else []
    merged = {entry['source']: entry for entry in prior}
    merged.update({entry['source']: entry for entry in entries})
    history = list(merged.values())
    if entries or not journal.exists():
        journal.write_text(json.dumps(history, ensure_ascii=False, indent=2), encoding='utf-8')
    return history


if __name__ == '__main__':
    print(json.dumps(migrate(), ensure_ascii=False))
