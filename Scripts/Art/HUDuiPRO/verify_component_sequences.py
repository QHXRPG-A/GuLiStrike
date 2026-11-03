"""Check file continuity and PNG container metadata; no visual inspection."""
import hashlib
import json
from pathlib import Path
import struct
import zlib

PROJECT = Path(__file__).resolve().parents[3]
ROOT = PROJECT / 'ArtSource/UI/HUDuiPRO/Components20261030'
MANIFEST = json.loads((ROOT / 'manifest.json').read_text(encoding='utf-8'))
REPORT = {'components': [], 'errors': [], 'frames': 0, 'bytes': 0,
          'visualVerification': 'pending_user', 'playbackVerification': 'not_run'}
SIGNATURE = b'\x89PNG\r\n\x1a\n'
IEND = b'\x00\x00\x00\x00IEND\xaeB`\x82'

for component in MANIFEST['components']:
    directory = Path(component['sequenceDirectory'])
    files = sorted(p for p in directory.iterdir() if p.suffix.lower() == '.png')
    expected = [f'Frame_{i:06d}.png' for i in range(component['frames'])]
    problems = []
    if [p.name for p in files] != expected:
        problems.append('frame_names_or_count')
    total_bytes = 0
    expected_metadata = (component['width'], component['height'], 8, 6, 0, 0, 0)
    for path in files:
        try:
            size = path.stat().st_size
            total_bytes += size
            with path.open('rb') as stream:
                header = stream.read(33)
                stream.seek(-12, 2)
                end = stream.read(12)
            valid_header = len(header) == 33 and header[:8] == SIGNATURE and header[8:16] == b'\x00\x00\x00\rIHDR'
            metadata = struct.unpack('>IIBBBBB', header[16:29]) if valid_header else None
            crc = struct.unpack('>I', header[29:33])[0] if valid_header else None
            if metadata != expected_metadata or crc != zlib.crc32(header[12:29]) or end != IEND:
                problems.append(path.name + ':invalid_png_container')
        except Exception as error:
            problems.append(path.name + ':' + str(error))
    REPORT['components'].append({'key': component['key'], 'name': component['name'], 'frames': len(files),
                                  'expectedFrames': component['frames'], 'bytes': total_bytes,
                                  'size': [component['width'], component['height']], 'rgbaBits': 8,
                                  'complete': not problems})
    if problems:
        REPORT['errors'].append({'key': component['key'], 'problems': problems[:12], 'problemCount': len(problems)})
    REPORT['frames'] += len(files)
    REPORT['bytes'] += total_bytes

REPORT['sourceSha256'] = hashlib.sha256(Path(MANIFEST['sourceProject']).read_bytes()).hexdigest()
REPORT['sourceUnchanged'] = REPORT['sourceSha256'] == MANIFEST['sourceSha256']
REPORT['renderJobSha256'] = hashlib.sha256(Path(MANIFEST['renderJob']).read_bytes()).hexdigest()
REPORT['ok'] = not REPORT['errors'] and REPORT['sourceUnchanged'] and REPORT['frames'] == MANIFEST['frameCount']
(ROOT / 'sequence-verification.json').write_text(json.dumps(REPORT, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({key: REPORT[key] for key in ['ok', 'frames', 'bytes', 'sourceUnchanged', 'errors']}, ensure_ascii=False))
raise SystemExit(0 if REPORT['ok'] else 1)
