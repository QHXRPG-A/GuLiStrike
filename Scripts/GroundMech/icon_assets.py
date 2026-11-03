"""Import the current Excel-owned HUD icon artwork without changing source pixels."""
import hashlib
import json
from pathlib import Path
import unreal


def import_catalog(root):
    root = Path(root)
    catalog = json.loads((root / 'ArtSource/UI/GroundMech/Icons/catalog.json').read_text(encoding='utf-8'))
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    dirty = {package.get_name() for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    for entry in catalog['icons']:
        assert entry['resource_path'].split('.')[0] not in dirty, 'Preserve unsaved icon edits'
        png = root / entry['source_png']
        assert hashlib.sha256(png.read_bytes()).hexdigest() == entry['sha256'], str(png)
    report = []
    for entry in catalog['icons']:
        path = entry['resource_path'].split('.')[0]
        task = unreal.AssetImportTask()
        task.filename = str(root / entry['source_png'])
        task.destination_path, task.destination_name = path.rsplit('/', 1)
        task.automated = True
        task.replace_existing = True
        task.save = False
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        texture = assets.load_asset(path)
        assert isinstance(texture, unreal.Texture2D), path
        width, height = entry['source_size']
        scale = min(1.0, entry['max_texture_size'] / max(width, height))
        built_size = [max(1, round(width * scale)), max(1, round(height * scale))]
        for prop, value in [
            ('compression_settings', unreal.TextureCompressionSettings.TC_EDITOR_ICON),
            ('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI),
            ('mip_gen_settings', unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS),
            ('srgb', True), ('filter', unreal.TextureFilter.TF_BILINEAR),
            ('address_x', unreal.TextureAddress.TA_CLAMP), ('address_y', unreal.TextureAddress.TA_CLAMP),
            ('never_stream', True), ('max_texture_size', entry['max_texture_size']),
            ('lod_bias', 0), ('compression_no_alpha', False),
            ('power_of_two_mode', unreal.TexturePowerOfTwoSetting.RESIZE_TO_SPECIFIC_RESOLUTION),
            ('resize_during_build_x', built_size[0]), ('resize_during_build_y', built_size[1]),
        ]:
            texture.set_editor_property(prop, value)
        unreal.EditorAssetLibrary.set_metadata_tag(texture, 'GroundIconSourceSHA256', entry['sha256'])
        unreal.EditorAssetLibrary.set_metadata_tag(texture, 'GroundIconVersion', catalog['version'])
        assert assets.save_loaded_asset(texture)
        loaded = assets.load_asset(path)
        assert loaded.get_editor_property('max_texture_size') == 256
        assert not loaded.get_editor_property('compression_no_alpha')
        assert unreal.EditorAssetLibrary.get_metadata_tag(loaded, 'GroundIconSourceSHA256') == entry['sha256']
        assert [loaded.blueprint_get_size_x(), loaded.blueprint_get_size_y()] == built_size
        report.append({'id': entry['id'], 'path': loaded.get_path_name(),
                       'source': entry['source_png'], 'sha256': entry['sha256'],
                       'size': [loaded.blueprint_get_size_x(), loaded.blueprint_get_size_y()],
                       'max_texture_size': loaded.get_editor_property('max_texture_size'),
                       'compression': str(loaded.get_editor_property('compression_settings')),
                       'lod_group': str(loaded.get_editor_property('lod_group')),
                       'alpha_preserved': not loaded.get_editor_property('compression_no_alpha')})
    return report
