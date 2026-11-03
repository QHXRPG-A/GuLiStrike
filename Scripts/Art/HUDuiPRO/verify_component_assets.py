"""Read back saved HUDuiPRO component asset chains in the live UE editor."""
import collections
import json
from pathlib import Path
import traceback
import unreal

PROJECT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
ROOT = PROJECT / 'ArtSource/UI/HUDuiPRO/Components20261030'
MANIFEST = json.loads((ROOT / 'manifest.json').read_text(encoding='utf-8'))
LIB = unreal.EditorAssetLibrary
REPORT = {'components': [], 'errors': [], 'visualVerification': 'pending_user', 'playbackVerification': 'not_run'}
CLASSES = {'source': unreal.ImgMediaSource, 'player': unreal.MediaPlayer, 'playlist': unreal.MediaPlaylist,
           'texture': unreal.MediaTexture, 'material': unreal.Material}

for component in MANIFEST['components']:
    try:
        assets = {kind: LIB.load_asset(path) for kind, path in component['assets'].items()}
        for kind, asset in assets.items():
            assert isinstance(asset, CLASSES[kind]), kind + ':wrong_class'
            assert LIB.get_metadata_tag(asset, 'HUDuiPRO.Owner') == 'Components20261030', kind + ':owner'
            assert LIB.get_metadata_tag(asset, 'HUDuiPRO.ComponentKey') == component['key'], kind + ':key'
        source, player, playlist, texture, material = (assets[k] for k in ['source', 'player', 'playlist', 'texture', 'material'])
        assert source.get_sequence_path() == component['sequenceRelative'], 'sequence_path'
        rate = source.get_editor_property('frame_rate_override')
        assert rate.numerator == component['fps'] and rate.denominator == 1, 'frame_rate'
        assert not source.get_editor_property('fill_gaps_in_sequence'), 'fill_gaps'
        assert player.get_editor_property('loop') and player.get_editor_property('play_on_open'), 'player_settings'
        assert playlist.num() == 1 and playlist.get(0) == source, 'playlist'
        assert texture.get_editor_property('media_player') == player, 'texture_player'
        assert texture.get_editor_property('new_style_output') and texture.get_editor_property('auto_clear'), 'texture_settings'
        clear = texture.get_editor_property('clear_color')
        assert clear.r == 0 and clear.g == 0 and clear.b == 0 and clear.a == 0, 'clear_color'
        assert material.get_editor_property('material_domain') == unreal.MaterialDomain.MD_UI, 'domain'
        assert material.get_editor_property('blend_mode') == unreal.BlendMode.BLEND_TRANSLUCENT, 'blend'
        rgb = unreal.MaterialEditingLibrary.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        alpha = unreal.MaterialEditingLibrary.get_material_property_input_node(material, unreal.MaterialProperty.MP_OPACITY)
        assert rgb and rgb == alpha and rgb.get_editor_property('texture') == texture, 'material_texture'
        assert unreal.MaterialEditingLibrary.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR) == 'RGB', 'rgb_pin'
        assert unreal.MaterialEditingLibrary.get_material_property_input_node_output_name(material, unreal.MaterialProperty.MP_OPACITY) == 'A', 'alpha_pin'
        expected_sampler = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if texture.get_editor_property('srgb') else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
        assert rgb.get_editor_property('sampler_type') == expected_sampler, 'sampler'
        REPORT['components'].append({'key': component['key'], 'assets': component['assets'], 'ok': True})
    except Exception as error:
        REPORT['errors'].append({'key': component['key'], 'message': str(error), 'traceback': traceback.format_exc()})

registry = unreal.AssetRegistryHelpers.get_asset_registry()
new_assets = registry.get_assets_by_path('/Game/Assets/UI/HUDuiPRO/Animations', recursive=True)
REPORT['registryCounts'] = dict(collections.Counter(str(a.asset_class_path.asset_name) for a in new_assets))
REPORT['registryTotal'] = len(new_assets)
REPORT['rootAssetTotal'] = len(registry.get_assets_by_path('/Game/Assets/UI/HUDuiPRO', recursive=True))
REPORT['ok'] = not REPORT['errors'] and len(REPORT['components']) == MANIFEST['componentCount'] and len(new_assets) == 5 * MANIFEST['componentCount']
(ROOT / 'ue-verification.json').write_text(json.dumps(REPORT, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'ok': REPORT['ok'], 'components': len(REPORT['components']), 'assets': REPORT['registryTotal'],
                  'rootAssets': REPORT['rootAssetTotal'], 'errors': REPORT['errors'][:3]}, ensure_ascii=False))
