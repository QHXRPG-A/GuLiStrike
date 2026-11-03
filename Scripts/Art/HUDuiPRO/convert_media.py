"""Convert production exports to UI PNGs and packaged ImgMedia sequences."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from PIL import Image

PROJECT = Path(__file__).resolve().parents[3]
AUDIT = PROJECT / 'ArtSource/UI/HUDuiPRO/Import20261030'
sys.path.insert(0, r'C:\Users\a\.codex\mcp-servers\mcp-aftereffects\media-tools')
import imageio_ffmpeg

def normalize_png(path):
    path = Path(path)
    try:
        with Image.open(path) as image:
            normalized = image.convert('RGBA')
            normalized.load()
        temporary = path.with_suffix('.tmp')
        normalized.save(temporary, format='PNG', compress_level=3)
        temporary.replace(path)
        return list(normalized.size)
    except Exception as error:
        raise RuntimeError(f'PNG conversion failed for {path}: {error}') from error

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--textures', action='store_true')
    parser.add_argument('--videos', nargs='*', default=[])
    args = parser.parse_args()
    manifest = json.loads((AUDIT / 'import-manifest.json').read_text(encoding='utf-8'))
    report = {'textures': [], 'videos': []}
    if args.textures:
        for entry in manifest['textures']:
            path = Path(entry['png'])
            size = normalize_png(path)
            if size != [entry['width'], entry['height']]:
                raise ValueError(f'Unexpected export dimensions: {path}: {size}')
            report['textures'].append({'file': str(path), 'size': [entry['width'], entry['height']], 'mode': 'RGBA', 'bitDepth': 8})
    for video in manifest['videos']:
        if video['name'] not in args.videos:
            continue
        output = Path(video['sequenceDirectory']) / 'Frame_%06d.png'
        # Preserve AE's source-footage alpha interpretation during conversion.
        vf = 'format=rgba'
        if video.get('premulMatte8'):
            channels = []
            for channel, matte in zip(('r', 'g', 'b'), video['premulMatte8']):
                channels.append(f"{channel}='({channel}(X,Y)*255-{matte}*(255-alpha(X,Y)))/max(alpha(X,Y),1)'")
            vf = 'format=gbrap,geq=' + ':'.join(channels) + ":a='alpha(X,Y)',format=rgba"
        log = AUDIT / ('convert-' + video['name'] + '.log')
        command = [imageio_ffmpeg.get_ffmpeg_exe(), '-hide_banner', '-nostdin', '-y', '-i', video['source'],
                   '-map', '0:v:0', '-an', '-vf', vf, '-fps_mode', 'passthrough', '-start_number', '0', '-c:v', 'png',
                   '-threads', '4', str(output)]
        if video.get('directPNG'):
            paths = [p for p in Path(video['sequenceDirectory']).iterdir() if p.suffix.lower() == '.png']
            with ThreadPoolExecutor(max_workers=6) as pool:
                list(pool.map(normalize_png, paths))
            log.write_text('Exported directly from the corrected AE composition with saveFrameToPng.\n', encoding='utf-8')
        else:
            with log.open('w', encoding='utf-8') as stream:
                subprocess.run(command, stdout=stream, stderr=subprocess.STDOUT, check=True, creationflags=0x08000000)
        frames = sorted(Path(video['sequenceDirectory']).iterdir())
        frame_files = [p for p in frames if p.suffix.lower() == '.png']
        expected = round(video['fps'] * video['duration'])
        if len(frame_files) != expected:
            raise ValueError(f'{video["name"]}: expected {expected} frames, got {len(frame_files)}')
        with Image.open(frame_files[0]) as image:
            size, mode = list(image.size), image.mode
        item = {'name': video['name'], 'frames': len(frame_files), 'fps': video['fps'], 'size': size,
                'mode': mode, 'bytes': sum(p.stat().st_size for p in frame_files), 'log': str(log)}
        report['videos'].append(item)
        print(json.dumps(item, ensure_ascii=False), flush=True)
    suffix = 'textures' if args.textures and not args.videos else '-'.join(args.videos)
    (AUDIT / ('conversion-' + suffix + '.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    if args.textures:
        print(json.dumps({'normalizedTextures': len(report['textures'])}))

if __name__ == '__main__':
    main()
