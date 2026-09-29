"""Shared single-line card text authoring. Call only in an idle editor."""
import json
import uuid
from pathlib import Path
import unreal
from card_reveal_graph import B, Graph, require, compile_save

WIDGET = '/Game/GuLiStrike/CardSystem/WarMachineTarot/UI/WBP_CardText'
STYLE = '/Game/GuLiStrike/CardSystem/WarMachineTarot/UI/DT_CardTextStyles'
SOURCE_STYLE = '/Game/GuLiStrike/Data/DT_GuLiStrikeRogueCardUI_TextStyles'
ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))


def linear_color(hex_value):
    def channel(value):
        value = int(value, 16) / 255.0
        return value / 12.92 if value <= .04045 else ((value + .055) / 1.055) ** 2.4
    return dict(zip(('R', 'G', 'B', 'A'),
                    [channel(hex_value[i:i+2]) for i in (1, 3, 5)] + [1.0]))


def import_style_source():
    source = (ROOT / 'Data/Json/DT_GuLiStrikeRogueCardUI_TextStyles.json').read_text(encoding='utf-8')
    row_struct = unreal.load_object(None, '/Script/GuLiStrike.GuLiStrikeRogueCardUITextStylesRow')
    require(row_struct is not None, 'Load the generated Excel text-style row type before import')
    table = unreal.load_asset(SOURCE_STYLE)
    if table is None:
        factory = unreal.DataTableFactory()
        factory.set_editor_property('struct', row_struct)
        table = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            SOURCE_STYLE.rsplit('/', 1)[1], SOURCE_STYLE.rsplit('/', 1)[0], unreal.DataTable, factory)
    require(table is not None, 'Create source text-style table')
    require(unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(table, source, row_struct),
            'Fill exported style rows in place')
    require(unreal.EditorAssetLibrary.save_loaded_asset(table, False), 'Save exported style rows')
    actual = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    require(actual == json.loads(source), 'Style DataTable matches Excel export')
    return actual


def source_style_rows():
    table = unreal.load_asset(SOURCE_STYLE)
    require(table is not None, 'Import Excel-owned source styles first')
    return json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))


def style_rows():
    # This adapter has no design values: colors, font, size, weight and outline
    # all come from the normal Excel -> JSON -> generated native DataTable path.
    return [{'Name': row['Name'], 'TextStyle': {
        'Font': {'FontObject': row['FontAsset'], 'TypefaceFontName': row['Typeface'], 'Size': row['FontSize'],
                 'OutlineSettings': {'OutlineSize': row['OutlineSize'],
                                     'OutlineColor': linear_color(row['OutlineColorSRGB']), 'bSeparateFillAlpha': False}},
        'ColorAndOpacity': {'SpecifiedColor': linear_color(row['ColorSRGB']), 'ColorUseRule': 'UseColor_Specified'},
        'ShadowOffset': {'X': 0, 'Y': 0},
        'ShadowColorAndOpacity': {'R': 0, 'G': 0, 'B': 0, 'A': 0}}} for row in source_style_rows()]


def configure_single_line_widget():
    require(not unreal.WidgetService.is_pie_running(), 'Do not edit card widgets during gameplay')
    W = unreal.WidgetService
    require(W.widget_exists(WIDGET, 'DescriptionFit'), 'Existing description ScaleBox')
    def title_state():
        return {name: {key: W.get_property(WIDGET, name, key) for key in keys}
                for name, keys in {
                    'CardTitle': ('Text', 'Font', 'ColorAndOpacity', 'Justification', 'Visibility'),
                    'TitleFit': ('Stretch', 'StretchDirection', 'Position X', 'Position Y',
                                 'Size X', 'Size Y', 'Alignment X', 'Alignment Y')}.items()}
    title_before = title_state()
    # Clear only the owned setter before replacing its typed widget variable.
    Graph(WIDGET, 'SetContent', True)
    if W.widget_exists(WIDGET, 'CardDescription'):
        snapshot = W.get_component_snapshot(WIDGET, 'CardDescription')
        if 'RichTextBlock' not in snapshot.widget_class:
            retired = 'PreviousDescription_' + uuid.uuid4().hex[:8]
            require(W.rename_widget(WIDGET, 'CardDescription', retired), 'Retire old widget object name')
            require(W.remove_component(WIDGET, retired, True).success, 'Remove old plain text widget')
    if not W.widget_exists(WIDGET, 'CardDescription'):
        require(W.add_component(WIDGET, 'RichTextBlock', 'CardDescription', 'DescriptionFit', True).success,
                'Create rich description')
    rows = style_rows()
    table = unreal.load_asset(STYLE)
    row_struct = unreal.load_object(None, '/Script/UMG.RichTextStyleRow')
    require(row_struct is not None, 'Built-in rich text style row')
    if table is None:
        factory = unreal.DataTableFactory()
        factory.set_editor_property('struct', row_struct)
        table = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            STYLE.rsplit('/', 1)[1], STYLE.rsplit('/', 1)[0], unreal.DataTable, factory)
    require(table is not None, 'Create card style table')
    require(unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(
        table, json.dumps(rows), row_struct), 'Populate Default/Unit/Gain styles')
    require(unreal.EditorAssetLibrary.save_loaded_asset(table, False), 'Save shared card text styles')
    for key, value in {'Text': '', 'TextStyleSet': table.get_path_name(),
                       'bOverrideDefaultStyle': 'false', 'AutoWrapText': 'false', 'WrapTextAt': '0',
                       'Justification': 'Center', 'Visibility': 'HitTestInvisible', 'ToolTipText': ''}.items():
        require(W.set_property(WIDGET, 'CardDescription', key, value), 'Description ' + key)
    for key, value in {'Stretch': 'ScaleToFit', 'StretchDirection': 'DownOnly'}.items():
        require(W.set_property(WIDGET, 'DescriptionFit', key, value), 'DescriptionFit ' + key)
    # Compile once with an empty setter so generated widget variable types are current.
    result = B.compile_blueprint(WIDGET)
    require(result.success, 'Refresh generated RichTextBlock variable: ' + str(list(result.errors)))
    graph = Graph(WIDGET, 'SetContent', True)
    graph.method('TextBlock', 'SetText', graph.get('CardTitle'), InText=graph.arg('Title'))
    graph.method('RichTextBlock', 'SetText', graph.get('CardDescription'), InText=graph.arg('Description'))
    graph.layout()
    compiled = compile_save(WIDGET)
    require(title_state() == title_before, 'Preserve card title and layout')
    return {'widget': WIDGET, 'style': STYLE, 'source_style_table': SOURCE_STYLE, 'style_source': rows,
            'compile': compiled, 'title_preserved': True}


def readback():
    W = unreal.WidgetService
    snapshot = W.get_component_snapshot(WIDGET, 'CardDescription')
    require('RichTextBlock' in snapshot.widget_class, 'Saved description is RichTextBlock')
    properties = {key: W.get_property(WIDGET, 'CardDescription', key)
                  for key in ('TextStyleSet', 'AutoWrapText', 'WrapTextAt', 'Justification', 'Visibility', 'ToolTipText')}
    require(properties['AutoWrapText'].lower() == 'false', 'No automatic wrapping')
    require(float(properties['WrapTextAt']) == 0, 'No fixed-width wrapping')
    require(STYLE in properties['TextStyleSet'], 'Shared style reference')
    table = unreal.load_asset(STYLE)
    rows = json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(table))
    by_name = {row['Name']: row['TextStyle'] for row in rows}
    expected_rows = {row['Name']: row for row in source_style_rows()}
    require(set(by_name) == set(expected_rows), 'Derived styles match all source rows')
    decoded = {}
    for name, expected_row in expected_rows.items():
        # DataTable exports Slate style structs as Unreal property text.
        style = unreal.TextBlockStyle()
        style.import_text(by_name[name])
        font = style.get_editor_property('font')
        outline = font.get_editor_property('outline_settings')
        font_object = font.get_editor_property('font_object')
        require(str(font.get_editor_property('typeface_font_name')) == expected_row['Typeface']
                and outline.get_editor_property('outline_size') == expected_row['OutlineSize']
                and font.get_editor_property('size') == expected_row['FontSize']
                and font_object.get_path_name() == expected_row['FontAsset'], 'Font matches Excel style ' + name)
        expected = linear_color(expected_row['ColorSRGB'])
        color = style.get_editor_property('color_and_opacity').get_editor_property('specified_color')
        actual = {key: getattr(color, key.lower()) for key in ('R', 'G', 'B', 'A')}
        require(all(abs(actual[key] - value) < .00001 for key, value in expected.items()),
                'Color matches Excel style ' + name)
        outline_color = outline.get_editor_property('outline_color')
        expected_outline = linear_color(expected_row['OutlineColorSRGB'])
        require(all(abs(getattr(outline_color, key.lower()) - value) < .00001
                    for key, value in expected_outline.items()), 'Outline color matches Excel ' + name)
        decoded[name] = {'font': font_object.get_path_name(), 'size': font.get_editor_property('size'),
                         'typeface': str(font.get_editor_property('typeface_font_name')),
                         'outline_size': outline.get_editor_property('outline_size'), 'color_linear': actual}
    return {'widget_class': snapshot.widget_class, 'properties': properties, 'style_rows': decoded,
            'fit': {key: W.get_property(WIDGET, 'DescriptionFit', key)
                    for key in ('Stretch', 'StretchDirection', 'Position Y', 'Size X', 'Size Y')}}
