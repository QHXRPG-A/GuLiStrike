"""Build the source-to-Unreal import manifest without reading UE binaries."""
import hashlib
import json
from pathlib import Path
import re

PROJECT = Path(__file__).resolve().parents[3]
ROOT = PROJECT / 'ArtSource/UI/HUDuiPRO'
AUDIT = ROOT / 'Import20261030'

def safe(value):
    value = re.sub(r'^\d+[. _]+', '', value)
    return re.sub(r'[^A-Za-z0-9_]+', '_', value).strip('_') or 'Item'

def main():
    project = json.loads((AUDIT / 'project-info.json').read_text(encoding='utf-8'))
    by_id = {item['id']: item for item in project['items']}
    entries = []
    for comp in project['items']:
        if comp['type'] != 'CompItem' or comp['name'].upper().startswith('COLOR GLOBAL'):
            continue
        parents = []
        current = comp['parentFolderId']
        while current in by_id:
            folder = by_id[current]
            parents.append(safe(folder['name']))
            current = folder.get('parentFolderId')
        parents.reverse()
        name = 'T_HUD_' + safe(comp['name'])
        png = ROOT / 'Exports/Textures' / '/'.join(parents) / (name + '.png')
        png.parent.mkdir(parents=True, exist_ok=True)
        entries.append({
            'compId': comp['id'], 'compName': comp['name'], 'sourceFolders': parents,
            'width': comp['width'], 'height': comp['height'], 'fps': comp['frameRate'],
            'sourceDuration': comp['duration'], 'exportTime': min(5.0, comp['duration'] - 1 / comp['frameRate']),
            'png': png.as_posix(), 'assetName': name,
            'destination': '/Game/Assets/UI/HUDuiPRO/Textures/' + '/'.join(parents),
        })
    movies = PROJECT / 'Content/Movies/HUDuiPRO'
    movies.mkdir(parents=True, exist_ok=True)
    videos = [
        {'name': 'Cockpit2_HD', 'comp': 'Display Screen Ship Cockpit 2 HD', 'width': 1920, 'height': 1080, 'fps': 24, 'duration': 50, 'directPNG': True, 'source': (ROOT / 'HUDuiPRO_UE2025.aep').as_posix()},
        {'name': 'Helicopter', 'fps': 24, 'duration': 101/24, 'premulMatte8': [39, 39, 39], 'source': 'D:/UE5.7/test1/Content/Assets/UI/HUDuiPRO/Source/(Footage)/03. Others/3d Objects/Military/Renders/helicopter.mov'},
        {'name': 'Satellite', 'fps': 24, 'duration': 101/24, 'premulMatte8': [32, 32, 32], 'source': 'D:/UE5.7/test1/Content/Assets/UI/HUDuiPRO/Source/(Footage)/03. Others/3d Objects/Military/Renders/satellite.mov'},
    ]
    for video in videos:
        directory = movies / video['name']
        directory.mkdir(exist_ok=True)
        video['sequenceDirectory'] = directory.as_posix()
    manifest = {'sourceProject': 'D:/UE5.7/test1/Content/Assets/UI/HUDuiPRO/Source/HUDuiPRO.aep',
                'adaptedProject': (ROOT / 'HUDuiPRO_UE2025.aep').as_posix(),
                'sourceSha256': hashlib.sha256(Path('D:/UE5.7/test1/Content/Assets/UI/HUDuiPRO/Source/HUDuiPRO.aep').read_bytes()).hexdigest(),
                'textureSampling': 'One transparent still at 5 seconds per non-control composition; component animation remains editable in AE.',
                'fontAliases': {'Rajdhani-Semibold': 'Rajdhani-SemiBold', 'LetterGothicStd': 'Consolas'},
                'textures': entries, 'videos': videos}
    (AUDIT / 'import-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'textures': len(entries), 'videos': videos}, ensure_ascii=False, indent=2))

if __name__ == '__main__':
    main()
