"""Prepare full-length, native-size animation exports for HUDuiPRO components."""
import hashlib
import json
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[3]
ROOT = PROJECT / 'ArtSource/UI/HUDuiPRO/Components20261030'

def main():
    original = json.loads((PROJECT / 'ArtSource/UI/HUDuiPRO/Import20261030/import-manifest.json').read_text(encoding='utf-8'))
    ROOT.mkdir(parents=True, exist_ok=True)
    components = []
    for item in original['textures']:
        if item['sourceFolders'][0] == 'Final':
            continue
        slug = item['assetName'].removeprefix('T_HUD_')
        relative = '/'.join(item['sourceFolders'][1:] + [slug])
        directory = PROJECT / 'Content/Movies/HUDuiPRO/Components' / relative
        directory.mkdir(parents=True, exist_ok=True)
        frames = round(item['sourceDuration'] * item['fps'])
        asset_dir = '/Game/Assets/UI/HUDuiPRO/Animations/' + relative
        names = {kind: prefix + slug for kind, prefix in {
            'source': 'IMS_HUDC_', 'player': 'MP_HUDC_', 'playlist': 'MPL_HUDC_',
            'texture': 'MT_HUDC_', 'material': 'M_UI_HUDC_',
        }.items()}
        components.append({
            'key': slug, 'name': item['compName'], 'compId': item['compId'],
            'categories': item['sourceFolders'][1:], 'width': item['width'], 'height': item['height'],
            'fps': item['fps'], 'frames': frames, 'duration': frames / item['fps'],
            'sequenceDirectory': directory.as_posix(),
            'sequenceRelative': './Content/Movies/HUDuiPRO/Components/' + relative,
            'renderOutput': (directory / 'Frame_[######].png').as_posix(),
            'assetDirectory': asset_dir, 'names': names,
            'assets': {kind: asset_dir + '/' + name for kind, name in names.items()},
        })
    assert len({x['assets']['material'] for x in components}) == len(components)
    source = Path(original['adaptedProject'])
    # Small components complete first; the catalog keeps the original source ordering.
    render_order = sorted(components, key=lambda x: x['width'] * x['height'] * x['frames'])
    manifest = {
        'sourceProject': source.as_posix(), 'sourceSha256': hashlib.sha256(source.read_bytes()).hexdigest(),
        'renderJob': (ROOT / 'HUDuiPRO_ComponentRender.aep').as_posix(),
        'componentCount': len(components), 'frameCount': sum(x['frames'] for x in components),
        'renderOrder': [x['key'] for x in render_order], 'components': components,
        'format': {'name': 'PNG Sequence', 'bitDepth': 8, 'channels': 'RGBA', 'alpha': 'straight'},
    }
    (ROOT / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'components': len(components), 'frames': manifest['frameCount'],
                      'job': manifest['renderJob'], 'first': render_order[0]['name'], 'last': render_order[-1]['name']}, ensure_ascii=False))

if __name__ == '__main__':
    main()
