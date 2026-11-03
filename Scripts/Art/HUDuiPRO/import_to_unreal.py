"""Run in the live editor: import HUDuiPRO textures or create media assets."""
import json
from pathlib import Path
import traceback
import unreal

PROJECT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
AUDIT = PROJECT / 'ArtSource/UI/HUDuiPRO/Import20261030'
MANIFEST = json.loads((AUDIT / 'import-manifest.json').read_text(encoding='utf-8'))
MODE = globals().get('HUD_IMPORT_MODE', 'textures')
ROOT = '/Game/Assets/UI/HUDuiPRO'
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
LIB = unreal.EditorAssetLibrary
REPORT = {'mode': MODE, 'assets': [], 'errors': []}

def mark(asset, key, value):
    LIB.set_metadata_tag(asset, key, str(value))

def save(asset):
    if not LIB.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError('Could not save ' + asset.get_path_name())

def owned(name, directory, cls, factory):
    path = directory + '/' + name
    existing = LIB.load_asset(path) if LIB.does_asset_exist(path) else None
    if existing:
        if not isinstance(existing, cls) or LIB.get_metadata_tag(existing, 'HUDuiPRO.Owner') != 'Import20261030':
            raise RuntimeError('Refusing to replace an unrelated asset: ' + path)
        return existing
    asset = TOOLS.create_asset(name, directory, cls, factory)
    if not asset:
        raise RuntimeError('Asset creation failed: ' + path)
    mark(asset, 'HUDuiPRO.Owner', 'Import20261030')
    return asset

def import_textures():
    tasks = []
    for item in MANIFEST['textures']:
        path = item['destination'] + '/' + item['assetName']
        if LIB.does_asset_exist(path):
            asset = LIB.load_asset(path)
            if LIB.get_metadata_tag(asset, 'HUDuiPRO.Owner') != 'Import20261030':
                raise RuntimeError('Refusing to replace an unrelated texture: ' + path)
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', item['png'])
        task.set_editor_property('destination_path', item['destination'])
        task.set_editor_property('destination_name', item['assetName'])
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', False)
        task.set_editor_property('factory', unreal.TextureFactory())
        tasks.append(task)
    TOOLS.import_asset_tasks(tasks)
    for item, task in zip(MANIFEST['textures'], tasks):
        asset = LIB.load_asset(item['destination'] + '/' + item['assetName'])
        if not isinstance(asset, unreal.Texture2D):
            raise RuntimeError('Texture import failed: ' + item['compName'])
        asset.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
        asset.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        asset.set_editor_property('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        asset.set_editor_property('srgb', True)
        asset.set_editor_property('compression_no_alpha', False)
        asset.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
        asset.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
        mark(asset, 'HUDuiPRO.Owner', 'Import20261030')
        mark(asset, 'HUDuiPRO.SourceComp', item['compName'])
        mark(asset, 'HUDuiPRO.SampleTime', item['exportTime'])
        mark(asset, 'HUDuiPRO.OriginalDuration', item['sourceDuration'])
        save(asset)
        size = [asset.blueprint_get_size_x(), asset.blueprint_get_size_y()]
        if size != [item['width'], item['height']]:
            raise RuntimeError('Imported texture dimensions differ: ' + asset.get_path_name())
        REPORT['assets'].append({'path': asset.get_path_name(), 'class': 'Texture2D', 'size': size,
                                'group': str(asset.get_editor_property('lod_group')),
                                'compression': str(asset.get_editor_property('compression_settings')),
                                'mips': str(asset.get_editor_property('mip_gen_settings')),
                                'srgb': asset.get_editor_property('srgb'), 'source': item['png'], 'saved': True})

def ui_material(name, texture):
    material = owned(name, ROOT + '/Materials', unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    node = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionTextureSample, -220, 0)
    node.set_editor_property('texture', texture)
    sampler = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if texture.get_editor_property('srgb') else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
    node.set_editor_property('sampler_type', sampler)
    if not unreal.MaterialEditingLibrary.connect_material_property(node, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError('Could not connect UI RGB')
    if not unreal.MaterialEditingLibrary.connect_material_property(node, 'A', unreal.MaterialProperty.MP_OPACITY):
        raise RuntimeError('Could not connect UI alpha')
    unreal.MaterialEditingLibrary.recompile_material(material)
    save(material)
    REPORT['assets'].append({'path': material.get_path_name(), 'class': 'Material', 'domain': str(material.get_editor_property('material_domain')),
                            'texture': texture.get_path_name(), 'samplerType': str(node.get_editor_property('sampler_type')), 'saved': True})
    return material

def import_media():
    for video in MANIFEST['videos']:
        directory = Path(video['sequenceDirectory'])
        count = sum(1 for p in directory.iterdir() if p.suffix.lower() == '.png')
        if count != round(video['fps'] * video['duration']):
            raise RuntimeError('Image sequence incomplete: ' + video['name'])
        source = owned('IMS_HUD_' + video['name'], ROOT + '/Media', unreal.ImgMediaSource, unreal.ImgMediaSourceFactoryNew())
        source.set_sequence_path('./Content/Movies/HUDuiPRO/' + video['name'])
        source.set_editor_property('frame_rate_override', unreal.FrameRate(numerator=video['fps'], denominator=1))
        source.set_editor_property('fill_gaps_in_sequence', False)
        save(source)
        player = owned('MP_HUD_' + video['name'], ROOT + '/Media', unreal.MediaPlayer, unreal.MediaPlayerFactoryNew())
        player.set_editor_property('play_on_open', True)
        player.set_editor_property('loop', True)
        save(player)
        playlist = owned('MPL_HUD_' + video['name'], ROOT + '/Media', unreal.MediaPlaylist, unreal.MediaPlaylistFactoryNew())
        while playlist.num() > 0:
            playlist.remove_at(playlist.num() - 1)
        playlist.add(source)
        save(playlist)
        texture = owned('MT_HUD_' + video['name'], ROOT + '/Media', unreal.MediaTexture, unreal.MediaTextureFactoryNew())
        texture.set_editor_property('media_player', player)
        texture.set_media_player(player)
        texture.set_editor_property('new_style_output', True)
        texture.set_editor_property('auto_clear', True)
        texture.set_editor_property('clear_color', unreal.LinearColor(0, 0, 0, 0))
        texture.update_resource()
        save(texture)
        ui_material('M_UI_HUD_' + video['name'], texture)
        for asset in (source, player, playlist, texture):
            REPORT['assets'].append({'path': asset.get_path_name(), 'class': asset.get_class().get_name(), 'saved': True})
        REPORT.setdefault('sequences', []).append({'source': source.get_path_name(), 'sequencePath': source.get_sequence_path(),
                    'frames': count, 'expectedFrames': round(video['fps'] * video['duration']),
                    'complete': count == round(video['fps'] * video['duration']),
                    'fps': video['fps'], 'player': texture.get_editor_property('media_player').get_path_name(),
                    'newStyleOutput': texture.get_editor_property('new_style_output'), 'srgb': texture.get_editor_property('srgb'),
                    'playOnOpen': player.get_editor_property('play_on_open'), 'loop': player.get_editor_property('loop')})
    still = next(item for item in MANIFEST['textures'] if item['compName'] == 'Display Screen Ship Cockpit 2 HD')
    ui_material('M_UI_HUD_Cockpit2_Still', LIB.load_asset(still['destination'] + '/' + still['assetName']))

try:
    if MODE == 'textures':
        import_textures()
    elif MODE == 'media':
        import_media()
    else:
        raise ValueError(MODE)
    REPORT['ok'] = True
except Exception as error:
    REPORT['ok'] = False
    REPORT['errors'].append({'message': str(error), 'traceback': traceback.format_exc()})
finally:
    (AUDIT / ('ue-import-' + MODE + '.json')).write_text(json.dumps(REPORT, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'ok': REPORT.get('ok'), 'mode': MODE, 'savedAssets': len(REPORT['assets']), 'errors': REPORT['errors']}, ensure_ascii=False))
