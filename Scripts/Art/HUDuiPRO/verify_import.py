"""Read only imported HUDuiPRO asset data; no playback or visual inspection."""
import collections
import json
from pathlib import Path
import unreal

PROJECT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
AUDIT = PROJECT / 'ArtSource/UI/HUDuiPRO/Import20261030'
MANIFEST = json.loads((AUDIT / 'import-manifest.json').read_text(encoding='utf-8'))
ROOT = '/Game/Assets/UI/HUDuiPRO'
LIB = unreal.EditorAssetLibrary
REPORT = {'textures': [], 'media': [], 'materials': [], 'errors': [], 'visualVerification': 'not_run', 'playbackVerification': 'not_run'}

def require(condition, message):
    if not condition:
        REPORT['errors'].append(message)

for item in MANIFEST['textures']:
    path = item['destination'] + '/' + item['assetName']
    texture = LIB.load_asset(path)
    if not isinstance(texture, unreal.Texture2D):
        REPORT['errors'].append('Missing Texture2D: ' + path)
        continue
    size = [texture.blueprint_get_size_x(), texture.blueprint_get_size_y()]
    require(size == [item['width'], item['height']], 'Texture size differs: ' + path)
    require(texture.get_editor_property('lod_group') == unreal.TextureGroup.TEXTUREGROUP_UI, 'Texture group differs: ' + path)
    require(texture.get_editor_property('compression_settings') == unreal.TextureCompressionSettings.TC_EDITOR_ICON, 'Texture compression differs: ' + path)
    require(texture.get_editor_property('mip_gen_settings') == unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS, 'Mip policy differs: ' + path)
    require(texture.get_editor_property('srgb'), 'sRGB disabled: ' + path)
    require(not texture.get_editor_property('compression_no_alpha'), 'Alpha discarded: ' + path)
    source = texture.get_editor_property('asset_import_data').get_first_filename()
    require(Path(source).resolve() == Path(item['png']).resolve(), 'Import source differs: ' + path)
    REPORT['textures'].append({'path': path, 'size': size, 'source': source})

def check_material(name, texture_path):
    path = ROOT + '/Materials/' + name
    material = LIB.load_asset(path)
    if not isinstance(material, unreal.Material):
        REPORT['errors'].append('Missing UI material: ' + path)
        return
    require(material.get_editor_property('material_domain') == unreal.MaterialDomain.MD_UI, 'Material domain differs: ' + path)
    require(material.get_editor_property('blend_mode') == unreal.BlendMode.BLEND_TRANSLUCENT, 'Material blend differs: ' + path)
    rgb = unreal.MaterialEditingLibrary.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    alpha = unreal.MaterialEditingLibrary.get_material_property_input_node(material, unreal.MaterialProperty.MP_OPACITY)
    require(rgb is not None and rgb == alpha, 'RGB/alpha inputs differ: ' + path)
    if rgb:
        texture = rgb.get_editor_property('texture')
        require(texture is not None and texture.get_path_name().split('.')[0] == texture_path, 'Material texture differs: ' + path)
        if texture:
            expected = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if texture.get_editor_property('srgb') else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
            require(rgb.get_editor_property('sampler_type') == expected, 'Sampler/sRGB mismatch: ' + path)
    REPORT['materials'].append({'path': path, 'texture': texture_path, 'rgbAndAlphaConnected': bool(rgb and rgb == alpha)})

for video in MANIFEST['videos']:
    name = video['name']
    source = LIB.load_asset(ROOT + '/Media/IMS_HUD_' + name)
    player = LIB.load_asset(ROOT + '/Media/MP_HUD_' + name)
    playlist = LIB.load_asset(ROOT + '/Media/MPL_HUD_' + name)
    texture = LIB.load_asset(ROOT + '/Media/MT_HUD_' + name)
    require(isinstance(source, unreal.ImgMediaSource), 'Missing image media source: ' + name)
    require(isinstance(player, unreal.MediaPlayer), 'Missing player: ' + name)
    require(isinstance(playlist, unreal.MediaPlaylist), 'Missing playlist: ' + name)
    require(isinstance(texture, unreal.MediaTexture), 'Missing media texture: ' + name)
    if not all((source, player, playlist, texture)):
        continue
    rate = source.get_editor_property('frame_rate_override')
    require(rate.numerator == video['fps'] and rate.denominator == 1, 'Frame rate differs: ' + name)
    relative = source.get_sequence_path()
    require(not Path(relative).is_absolute(), 'Absolute media path: ' + name)
    require((PROJECT / relative).resolve() == Path(video['sequenceDirectory']).resolve(), 'Sequence directory differs: ' + name)
    expected = round(video['fps'] * video['duration'])
    actual_names = sorted(p.name for p in Path(video['sequenceDirectory']).iterdir() if p.suffix.lower() == '.png')
    require(actual_names == [f'Frame_{i:06d}.png' for i in range(expected)], 'Missing/extra sequence frames: ' + name)
    require(playlist.num() == 1 and playlist.get(0) == source, 'Playlist source differs: ' + name)
    require(texture.get_editor_property('media_player') == player, 'MediaTexture player differs: ' + name)
    require(texture.get_editor_property('new_style_output'), 'MediaTexture regular 2D sampling disabled: ' + name)
    require(player.get_editor_property('play_on_open') and player.get_editor_property('loop'), 'Playback defaults differ: ' + name)
    REPORT['media'].append({'name': name, 'source': source.get_path_name(), 'relativePath': relative, 'frames': len(actual_names),
                            'expectedFrames': expected, 'fps': video['fps'], 'player': player.get_path_name(), 'texture': texture.get_path_name()})
    check_material('M_UI_HUD_' + name, ROOT + '/Media/MT_HUD_' + name)

still = next(item for item in MANIFEST['textures'] if item['compName'] == 'Display Screen Ship Cockpit 2 HD')
check_material('M_UI_HUD_Cockpit2_Still', still['destination'] + '/' + still['assetName'])
assets = unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path(ROOT, recursive=True)
REPORT['registryCounts'] = dict(collections.Counter(str(asset.asset_class_path.asset_name) for asset in assets))
REPORT['registryTotal'] = len(assets)
REPORT['ok'] = not REPORT['errors']
(AUDIT / 'ue-verification.json').write_text(json.dumps(REPORT, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'ok': REPORT['ok'], 'counts': REPORT['registryCounts'], 'errors': REPORT['errors']}, ensure_ascii=False))
