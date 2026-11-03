"""Derive short, padded RGBA atlases from the existing HUDuiPRO PNG sequences.

No new artwork is drawn. Source pixels are resampled with premultiplied alpha.
The manifest records each original frame hash, dimensions and uncompressed budget.
"""
import hashlib
import json
import math
import subprocess
import tempfile
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'ArtSource/UI/GroundMech/HUD_Motion_20261102'
ASSET = '/Game/GuLiStrike/GroundMech/UI/Motion'
CLIPS = (
    ('Intro', 'Panel_1', 0, 18, 1024, 576, .45),
    ('Cast', 'Box_1', 0, 12, 256, 256, .18),
    ('Ready', 'Circle_Elements_03', 0, 18, 256, 256, .30),
    ('Cooldown', 'Circle_Elements_Bars_01', 12, 12, 256, 256, 0),
)
PARAMETERS = {
    'MotionIntroSeconds': .45, 'MotionModalOpenSeconds': .18, 'MotionModalCloseSeconds': .12,
    'MotionCastSeconds': .18, 'MotionReadySeconds': .30, 'MotionReadyFadeSeconds': .10,
    'MotionAimSeconds': .12, 'MotionSlidePixels': 18,
    'MotionShotSeconds': .18, 'MotionShotKickPixels': 4, 'MotionShotMaxPixels': 12,
    'MotionShotJitterPixels': 1.5, 'MotionHitSeconds': .16, 'MotionKillSeconds': .28,
}

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    OUT.mkdir(parents=True, exist_ok=True)
    catalog_path = ROOT / 'ArtSource/UI/HUDuiPRO/Components20261030/manifest.json'
    catalog = {c['key']: c for c in json.loads(catalog_path.read_text(encoding='utf-8-sig'))['components']}
    clips = []
    for key, source, first, last, width, height, duration in CLIPS:
        definition = catalog[source]
        directory = Path(definition['sequenceDirectory'])
        start = 0 if (directory / 'Frame_000000.png').is_file() else 1
        count = last - first + 1
        columns = min(4, count)
        rows = math.ceil(count / columns)
        padding = 2
        cell_w, cell_h = width + 2 * padding, height + 2 * padding
        atlas = Image.new('RGBA', (cell_w * columns, cell_h * rows))
        originals = []
        for index, frame in enumerate(range(first, last + 1)):
            path = directory / f'Frame_{frame + start:06d}.png'
            with Image.open(path) as im:
                assert im.mode == 'RGBA' and im.size == (definition['width'], definition['height'])
                tile = im.convert('RGBa').resize((width, height), Image.Resampling.LANCZOS).convert('RGBA')
                x, y = index % columns * cell_w + padding, index // columns * cell_h + padding
                atlas.paste(tile, (x, y))
                # Edge extrusion prevents adjacent frames bleeding under bilinear filtering.
                atlas.paste(tile.crop((0, 0, width, 1)).resize((width, padding)), (x, y-padding))
                atlas.paste(tile.crop((0, height-1, width, height)).resize((width, padding)), (x, y+height))
                atlas.paste(tile.crop((0, 0, 1, height)).resize((padding, height)), (x-padding, y))
                atlas.paste(tile.crop((width-1, 0, width, height)).resize((padding, height)), (x+width, y))
            originals.append({'frame': frame, 'path': str(path.relative_to(ROOT)), 'sha256': sha(path)})
        dest = OUT / f'T_GroundHUD_{key}.png'
        atlas.save(dest)
        clips.append({'key': key, 'source_component': source, 'source_fps': definition['fps'],
                      'first': first, 'last': last, 'frames': count, 'columns': columns, 'rows': rows,
                      'frame_size': [width, height], 'cell_size': [cell_w, cell_h], 'padding': padding,
                      'atlas_size': list(atlas.size), 'rgba_bytes': atlas.width*atlas.height*4,
                      'duration': duration, 'png': str(dest.relative_to(ROOT)), 'sha256': sha(dest),
                      'asset': f'{ASSET}/Textures/T_GroundHUD_{key}', 'source_frames': originals})
    total = sum(c['rgba_bytes'] for c in clips)
    assert total <= 64*1024*1024, total
    manifest = {'source_manifest': str(catalog_path.relative_to(ROOT)), 'source_manifest_sha256': sha(catalog_path),
                'clips': clips, 'rgba_bytes': total, 'rgba_mib': total/1024**2,
                'budget_mib': 64, 'parameters': PARAMETERS, 'visual_review': 'not_performed'}
    (OUT/'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')

    xlsx = r'D:/UE5.7/excelize-cli/bin/xlsx.exe'
    workbook = ROOT/'data/Excel/GuLiStrikeGroundHUD.xlsx'
    def run(*args):
        return subprocess.check_output([xlsx, *map(str,args)], text=True, encoding='utf-8')
    old = json.loads(run('read', workbook, '--sheet', 'Elements', '--format', 'json'))['rows']
    by_name = {r[1]: i+1 for i,r in enumerate(old) if i>=3 and len(r)>1}
    last_id = max(int(r[0]) for r in old[3:] if r and r[0])
    tail = len(old)+1
    entries = [(name, '', -1, value, 'HUD短动效参数') for name,value in PARAMETERS.items()]
    for clip in clips:
        entries.extend([(f"Motion{clip['key']}Atlas", clip['asset']+'.'+clip['asset'].rsplit('/',1)[1], clip['frames'], clip['duration'], 'HUDuiPRO短序列派生图集'),
                        (f"Motion{clip['key']}Columns", '', -1, clip['columns'], '图集列数'),
                        (f"Motion{clip['key']}Rows", '', -1, clip['rows'], '图集行数'),
                        (f"Motion{clip['key']}PaddingX", '', -1, clip['padding']/clip['cell_size'][0], '单格UV留边'),
                        (f"Motion{clip['key']}PaddingY", '', -1, clip['padding']/clip['cell_size'][1], '单格UV留边')])
    for name in ('Atlas', 'Cooldown'):
        path = f'{ASSET}/Materials/M_GroundHUD_{name}'
        entries.append((f'Motion{name}Material', path+'.'+path.rsplit('/',1)[1], -1, 0, '透明UI材质'))
    cells=[]
    for name, texture, sprite, value, note in entries:
        row = by_name.get(name)
        if row is None:
            row=tail; tail+=1; last_id+=1; ident=last_id
        else:
            ident=int(old[row-1][0])
        values=[ident,name,note,texture,'',sprite,value,'']
        for col,val in enumerate(values):
            cells.append({'cell':f'{chr(65+col)}{row}', 'value':val, 'type':'int' if col in (0,5) else 'float' if col==6 else 'string'})
    with tempfile.TemporaryDirectory(prefix='ground-hud-motion-') as tmp:
        file=Path(tmp)/'cells.json';file.write_text(json.dumps(cells,ensure_ascii=False),encoding='utf-8')
        run('write',workbook,'--sheet','Elements','--data-file',file)
    print(json.dumps({'clips':len(clips),'rgba_mib':manifest['rgba_mib'],'configuration_rows':len(entries)}))

if __name__ == '__main__':
    main()
