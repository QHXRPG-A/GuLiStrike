"""Package the two v7 cards, their generated sources and numeric records."""
import hashlib,json,zipfile
from pathlib import Path
from PIL import Image

root=Path(__file__).resolve().parent
manifest=json.loads((root/'manifest_v7.json').read_text(encoding='utf8'))
required=[
 'index_v7_pair.html','preview_v7.js','preview_v6.css','card-data_v7.js','README.md','manifest_v7.json',
 'card-data_v6.js','manifest_v6.json','generation_v6_attempts_all.json',
 'generation_v7.json','generation_v7_patches_all.json','generation_v7_jobs.json',
 'outpaint_registration_v7.json','outpaint_patch_registration_v7.json',
 'outpaint_patch_specs_v7.json','outpaint_more_specs_v7.json','assembled_outpaint_v7.json',
 'assemble_outpaint_v7.py','publish_outpaint_v7.py','register_outpaint_v7.py',
 'verify_generated_v7.cjs','technical_checks_v7.json','package_outpaint_v7.py',
 'cleanup_20261117.json','requirements-artwork.txt',
]
files={root/name for name in required}
for card,c in manifest['cards'].items():
 files.update(root/card/name for name in ['Master.png','browser_measurement_v6.json','browser_measurement_v7.json'])
 # Deliberately limited to this task's two version directories.
 files.update((root/card/'Generation_v7').rglob('*'))
 files.update((root/card/'Generation_v6').rglob('*'))
 for layer in c['layers']:
  file=root/layer['file']
  assert hashlib.sha256(file.read_bytes()).hexdigest()==layer['export_sha256']
  with Image.open(file) as image:
   assert image.mode=='RGBA' and image.size==(1088,1632)
files={p for p in files if not p.is_dir()}
assert all(p.is_file() and p.is_relative_to(root) for p in files)
archive=root/'ExplosiveCards_v7_edge_review.zip'
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as out:
 out.write(root/'index_v7_pair.html','index.html')
 for file in sorted(files):out.write(file,file.relative_to(root).as_posix())
with zipfile.ZipFile(archive) as package:
 assert package.testzip() is None
 for card,c in manifest['cards'].items():
  for layer in c['layers']:
   assert hashlib.sha256(package.read(layer['file'])).hexdigest()==layer['export_sha256']
report={'archive':archive.name,'file_count':len(files),'bytes':archive.stat().st_size,
 'sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),
 'crc_test':'passed','all_12_png_hashes':'passed','all_12_rgba_and_size':'passed'}
(root/'package_v7.json').write_text(json.dumps(report,indent=2),encoding='utf8')
print(json.dumps(report))
