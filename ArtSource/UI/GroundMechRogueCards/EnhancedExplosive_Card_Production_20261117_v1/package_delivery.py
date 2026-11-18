"""Package this card directory and verify final manifest/layer byte identity."""
from pathlib import Path
import hashlib
import json
import zipfile

B = Path(__file__).resolve().parent
m = json.loads((B / 'manifest.json').read_text(encoding='utf-8'))
destination = B / m['preview']['download_package']
folders = ['', 'Inputs', 'Layers', 'Generated', 'Prompts', 'Registered', 'Assembled_r2', 'Checks']
folders += [path.relative_to(B).as_posix() for path in (B/'Revisions').iterdir() if path.is_dir()]
files = []
for folder in folders:
    for path in (B / folder).iterdir():
        if path.is_file() and path.suffix not in ('.zip', '.pyc') and path != B / 'package.json':
            files.append(path)
files.sort(key=lambda path: path.relative_to(B).as_posix())
with zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as package:
    for path in files:
        package.write(path, path.relative_to(B).as_posix())
with zipfile.ZipFile(destination) as package:
    assert package.read('manifest.json') == (B / 'manifest.json').read_bytes()
    assert package.read('card-data.js') == (B / 'card-data.js').read_bytes()
    for layer in m['layers']:
        payload = package.read(layer['file'])
        assert hashlib.sha256(payload).hexdigest() == layer['sha256']
        assert payload == (B / layer['file']).read_bytes()
report = dict(file=destination.name, revision=m['revision'], bytes=destination.stat().st_size,
    sha256=hashlib.sha256(destination.read_bytes()).hexdigest(), files=len(files),
    final_layer_hashes_verified=True, manifest_and_preview_data_bytes_verified=True)
(B / 'package.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report), flush=True)
