"""Capture the live Wingman action/weapon contract without modifying assets."""
import json
from pathlib import Path
import unreal


def capture(label):
    result = {}
    for path in unreal.EditorAssetLibrary.list_assets('/Game/GuLiStrike/Ship/Abilities', recursive=True, include_folder=False):
        asset = unreal.load_asset(path)
        if asset.get_class().get_name() != 'GuLiShipAbilitySet':
            continue
        entries = []
        for grant in asset.get_editor_property('grants'):
            weapon = grant.get_editor_property('weapon_definition')
            formation = grant.get_editor_property('formation_definition')
            entries.append({
                'id': str(unreal.GameplayTagLibrary.get_tag_name(grant.get_editor_property('ability_id'))),
                'input': str(unreal.GameplayTagLibrary.get_tag_name(grant.get_editor_property('input_tag'))),
                'slot': str(grant.get_editor_property('weapon_slot_id')),
                'skill_id': str(grant.get_editor_property('skill_id')),
                'profile_revision': grant.get_editor_property('profile_revision'),
                'cooldown_group': str(grant.get_editor_property('cooldown_group_id')),
                'weapon': weapon.get_path_name() if weapon else None,
                'formation': formation.get_path_name() if formation else None,
                'resolved_weapon': weapon.get_resolved_weapon_config().export_text() if weapon else None,
            })
        result[path] = entries
    destination = Path(unreal.Paths.project_dir()) / 'outputs/ship_capability_migration'
    destination.mkdir(parents=True, exist_ok=True)
    (destination / ('wingman_contract_' + label + '.json')).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({'label': label, 'catalogs': len(result)}))
    return result


if __name__ == '__main__':
    capture('before')
