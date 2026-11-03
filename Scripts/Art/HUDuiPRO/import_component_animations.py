"""Create independently callable media chains for exported HUDuiPRO components."""
import collections
import json
from pathlib import Path
import traceback
import unreal

PROJECT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
ROOT = PROJECT / 'ArtSource/UI/HUDuiPRO/Components20261030'
MANIFEST = json.loads((ROOT / 'manifest.json').read_text(encoding='utf-8'))
OWNER = 'Components20261030'
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
REPORT = {'imported': [], 'pending': [], 'errors': [], 'visualVerification': 'pending_user', 'playbackVerification': 'not_run'}
PREVIOUS = ROOT / 'ue-import.json'
COMPLETE = {item['key']: item for item in json.loads(PREVIOUS.read_text(encoding='utf-8')).get('imported', [])} if PREVIOUS.exists() else {}

def save(asset):
    if not LIB.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError('Save failed: ' + asset.get_path_name())

def get_asset(component, kind, cls, factory):
    path = component['assets'][kind]
    if LIB.does_asset_exist(path):
        asset = LIB.load_asset(path)
        if not isinstance(asset, cls) or LIB.get_metadata_tag(asset, 'HUDuiPRO.Owner') != OWNER:
            raise RuntimeError('Asset belongs to another resource: ' + path)
        return asset
    asset = TOOLS.create_asset(component['names'][kind], component['assetDirectory'], cls, factory)
    if not asset:
        raise RuntimeError('Create failed: ' + path)
    LIB.set_metadata_tag(asset, 'HUDuiPRO.Owner', OWNER)
    LIB.set_metadata_tag(asset, 'HUDuiPRO.ComponentKey', component['key'])
    LIB.set_metadata_tag(asset, 'HUDuiPRO.SourceComp', component['name'])
    return asset

def import_component(component):
    source = get_asset(component, 'source', unreal.ImgMediaSource, unreal.ImgMediaSourceFactoryNew())
    source.set_sequence_path(component['sequenceRelative'])
    source.set_editor_property('frame_rate_override', unreal.FrameRate(numerator=component['fps'], denominator=1))
    source.set_editor_property('fill_gaps_in_sequence', False)
    save(source)
    player = get_asset(component, 'player', unreal.MediaPlayer, unreal.MediaPlayerFactoryNew())
    player.set_editor_property('play_on_open', True)
    player.set_editor_property('loop', True)
    save(player)
    playlist = get_asset(component, 'playlist', unreal.MediaPlaylist, unreal.MediaPlaylistFactoryNew())
    while playlist.num():
        playlist.remove_at(playlist.num() - 1)
    playlist.add(source)
    save(playlist)
    texture = get_asset(component, 'texture', unreal.MediaTexture, unreal.MediaTextureFactoryNew())
    texture.set_editor_property('media_player', player)
    texture.set_media_player(player)
    texture.set_editor_property('new_style_output', True)
    texture.set_editor_property('auto_clear', True)
    texture.set_editor_property('clear_color', unreal.LinearColor(0, 0, 0, 0))
    texture.update_resource()
    save(texture)
    material = get_asset(component, 'material', unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    unreal.MaterialEditingLibrary.delete_all_material_expressions(material)
    node = unreal.MaterialEditingLibrary.create_material_expression(material, unreal.MaterialExpressionTextureSample, -200, 0)
    node.set_editor_property('texture', texture)
    sampler = unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if texture.get_editor_property('srgb') else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
    node.set_editor_property('sampler_type', sampler)
    if not unreal.MaterialEditingLibrary.connect_material_property(node, 'RGB', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError('RGB connection failed: ' + component['name'])
    if not unreal.MaterialEditingLibrary.connect_material_property(node, 'A', unreal.MaterialProperty.MP_OPACITY):
        raise RuntimeError('Alpha connection failed: ' + component['name'])
    unreal.MaterialEditingLibrary.recompile_material(material)
    save(material)
    # Read back the saved chain without opening or playing the source.
    rate = source.get_editor_property('frame_rate_override')
    assert rate.numerator == component['fps'] and rate.denominator == 1
    assert source.get_sequence_path() == component['sequenceRelative']
    assert playlist.num() == 1 and playlist.get(0) == source
    assert texture.get_editor_property('media_player') == player
    assert texture.get_editor_property('new_style_output')
    rgb = unreal.MaterialEditingLibrary.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    alpha = unreal.MaterialEditingLibrary.get_material_property_input_node(material, unreal.MaterialProperty.MP_OPACITY)
    assert rgb and rgb == alpha and rgb.get_editor_property('texture') == texture
    return {'key': component['key'], 'name': component['name'], 'assets': component['assets'],
            'frames': component['frames'], 'fps': component['fps'], 'size': [component['width'], component['height']],
            'relativePath': source.get_sequence_path(), 'saved': True}

try:
    for component in MANIFEST['components']:
        if component['key'] in COMPLETE and all(LIB.does_asset_exist(path) for path in component['assets'].values()):
            REPORT['imported'].append(COMPLETE[component['key']])
            continue
        directory = Path(component['sequenceDirectory'])
        files = sorted(p.name for p in directory.iterdir() if p.suffix.lower() == '.png')
        expected = [f'Frame_{i:06d}.png' for i in range(component['frames'])]
        if files != expected:
            REPORT['pending'].append({'key': component['key'], 'files': len(files), 'expected': component['frames']})
            continue
        REPORT['imported'].append(import_component(component))
    REPORT['ok'] = not REPORT['pending']
except Exception as error:
    REPORT['ok'] = False
    REPORT['errors'].append({'message': str(error), 'traceback': traceback.format_exc()})
registry = unreal.AssetRegistryHelpers.get_asset_registry().get_assets_by_path('/Game/Assets/UI/HUDuiPRO/Animations', recursive=True)
REPORT['registryCounts'] = dict(collections.Counter(str(a.asset_class_path.asset_name) for a in registry))
REPORT['registryTotal'] = len(registry)
(ROOT / 'ue-import.json').write_text(json.dumps(REPORT, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'ok': REPORT.get('ok'), 'imported': len(REPORT['imported']), 'pending': len(REPORT['pending']),
                  'registryCounts': REPORT['registryCounts'], 'errors': REPORT['errors']}, ensure_ascii=False))
