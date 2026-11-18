"""Package icon source files and verify original PNG byte identity."""
import hashlib
import json
import zipfile
from pathlib import Path

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[3]
manifest = json.loads((BASE/'manifest.json').read_text(encoding='utf-8'))
destination = BASE/(manifest['version']+'.zip')
files = [p for p in BASE.iterdir() if p.is_file() and p.suffix != '.zip' and p.name != 'package.json']
for folder in ['Inputs', 'Prompts', 'Icons', 'Research', 'Revisions/v1/Icons']:
    files.extend(p for p in (BASE/folder).iterdir() if p.is_file())
files.extend(p for p in (BASE/'Revisions/v1').iterdir() if p.is_file() and p.suffix != '.zip')
files.extend(p for p in (BASE/'Revisions/v2_before_package_axis').iterdir() if p.is_file() and p.suffix != '.zip')
with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for path in sorted(files):
        archive.write(path, path.relative_to(BASE).as_posix())
with zipfile.ZipFile(destination) as archive:
    assert archive.read('manifest.json') == (BASE/'manifest.json').read_bytes()
    for icon in manifest['icons']:
        relative = (ROOT/icon['source_png']).relative_to(BASE).as_posix()
        assert hashlib.sha256(archive.read(relative)).hexdigest() == icon['sha256']
    previous = json.loads((BASE/'Revisions/v1/manifest.json').read_text(encoding='utf-8'))
    for icon in previous['icons']:
        relative = 'Revisions/v1/Icons/'+Path(icon['source_png']).name
        assert hashlib.sha256(archive.read(relative)).hexdigest() == icon['sha256']
report = dict(file=destination.name, bytes=destination.stat().st_size,
              sha256=hashlib.sha256(destination.read_bytes()).hexdigest(), files=len(files),
              original_png_hashes_verified=True)
(BASE/'package.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
print(json.dumps(report))
