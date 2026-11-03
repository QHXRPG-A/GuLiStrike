"""Import unchanged OFL font faces and derive Ground card styles from the shared Excel export.

Run in an idle editor after building GuLiStrikeEditor. No gameplay or Commander styles change.
"""
from pathlib import Path
import json, re
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
DEST = '/Game/GuLiStrike/GroundMech/UI/Fonts'
FONT = DEST + '/F_GroundHUD'
STYLE = '/Game/GuLiStrike/GroundMech/UI/DT_GroundCardTextStyles'


def derive_styles(font):
    assert not unreal.WidgetService.is_pie_running()
    shared = unreal.load_asset('/Game/GuLiStrike/CardSystem/WarMachineTarot/UI/DT_CardTextStyles')
    assert shared
    source = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(shared))
    # Retain every shared style's name, text size and functional colour. Ground's
    # real Chinese weights replace synthetic same-colour outlines locally.
    rows = []
    for row in source:
        text, font_count = re.subn(r'FontObject="[^"]+"', 'FontObject="/Script/Engine.Font\'' + font.get_path_name() + '\'"', row['TextStyle'], count=1)
        text, outline_count = re.subn(r'OutlineSize=\d+', 'OutlineSize=0', text, count=1)
        assert font_count == 1 and outline_count == 1
        rows.append({'Name': row['Name'], 'TextStyle': text})
    table = unreal.load_asset(STYLE)
    if table is None:
        factory = unreal.DataTableFactory()
        factory.set_editor_property('struct', unreal.load_object(None, '/Script/UMG.RichTextStyleRow'))
        table = unreal.AssetToolsHelpers.get_asset_tools().create_asset('DT_GroundCardTextStyles', STYLE.rsplit('/', 1)[0], unreal.DataTable, factory)
    assert table
    problems = unreal.GuLiDataTableImportLibrary.update_from_json(table, json.dumps(rows))
    assert not problems, list(problems)
    assert unreal.EditorAssetLibrary.save_loaded_asset(table, False)
    actual = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    assert actual == rows, 'Ground style readback must match the two-field derivation exactly'
    return actual


def author():
    assert not unreal.WidgetService.is_pie_running()
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    faces = []
    for weight in ('Medium', 'Bold'):
        name = 'FF_GroundHUD_' + weight
        asset = unreal.load_asset(DEST + '/' + name)
        if asset is None:
            task = unreal.AssetImportTask()
            task.set_editor_property('filename', str(ROOT / 'ArtSource/UI/GroundMech/HUD_Reference_20261102/Fonts' / ('NotoSansCJKsc-' + weight + '.otf')))
            task.set_editor_property('destination_path', DEST)
            task.set_editor_property('destination_name', name)
            task.set_editor_property('automated', True)
            task.set_editor_property('save', True)
            task.set_editor_property('factory', unreal.FontFileImportFactory())
            asset_tools.import_asset_tasks([task])
            asset = unreal.load_asset(DEST + '/' + name)
        assert isinstance(asset, unreal.FontFace), name
        asset.set_editor_property('loading_policy', unreal.FontLoadingPolicy.STREAM)
        assert unreal.EditorAssetLibrary.save_loaded_asset(asset, False)
        faces.append(asset)
    font = unreal.load_asset(FONT)
    if font is None:
        font = asset_tools.create_asset('F_GroundHUD', DEST, unreal.Font, unreal.FontFactory())
    error = unreal.GuLiGroundHUDAssetLibrary.configure_font(font, faces[0], faces[1])
    assert not error, error
    assert unreal.EditorAssetLibrary.save_loaded_asset(font, False)
    composite = font.get_editor_property('composite_font').export_text()
    assert all(face.get_name() in composite for face in faces), composite
    styles = derive_styles(font)
    return {'font': font.get_path_name(), 'composite': composite,
            'faces': [{'path': face.get_path_name(), 'loading_policy': str(face.get_editor_property('loading_policy'))} for face in faces],
            'styles': styles, 'saved': True}


if __name__ == '__main__':
    unreal.MCPythonHelper.submit_result(json.dumps(author()))
