"""Adopt the unmodified three-missile edit and update only Icon 11 metadata."""
import copy
import hashlib
import json
import shutil
import sys
from pathlib import Path

sys.path.insert(0, 'C:/Users/a/.codex/tmp/enhanced-explosive-card-runtime')
from PIL import Image

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(__file__).resolve().parent
BASE = ROOT/'ArtSource/UI/GroundMech/RogueCardIcons_20261118_v1'


def read(path):
    return json.loads(path.read_text(encoding='utf-8'))


def write(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


before = read(OUT/'source-before.json')
receipt = read(BASE/'generation-package-axis-result.json')
manifest = read(BASE/'manifest.json')
catalog_path = ROOT/'ArtSource/UI/GroundMech/Icons/catalog.json'
catalog = read(catalog_path)
for relative, digest in before['files'].items():
    assert sha(ROOT/relative) == digest, relative
for icon in manifest['icons']:
    assert sha(ROOT/icon['source_png']) == before['icon_hashes'][str(icon['id'])]
assert sha(BASE/'Inputs/11_ExplosivePackage_v2_before_axis.png') == before['icon_hashes']['11']

source = Path(receipt['source_path'])
with Image.open(source) as image:
    assert image.format == 'PNG' and image.mode == 'RGBA'
    assert image.width == image.height
    size = list(image.size)
    alpha = image.getchannel('A')
    assert alpha.getextrema() == (0, 255)
    histogram = alpha.histogram()
    alpha_info = dict(extrema=list(alpha.getextrema()), bounds=list(alpha.getbbox()),
                      transparent_pixels=histogram[0], opaque_pixels=histogram[255])
digest = sha(source)
assert digest != before['icon_hashes']['11']
entry = next(icon for icon in manifest['icons'] if icon['id'] == 11)
old_entry = copy.deepcopy(entry)
entry.update(sha256=digest, source_size=size, alpha=alpha_info,
             version=receipt['version'], prompt_file=receipt['prompt_file'],
             visual_review='pending_user')
shutil.copyfile(source, ROOT/entry['source_png'])
assert sha(ROOT/entry['source_png']) == digest
catalog['icons'] = [copy.deepcopy(entry) if icon['id'] == 11 else icon for icon in catalog['icons']]
catalog['version'] = 'A_v4+'+receipt['version']
manifest['version'] = receipt['version']
manifest['previous_version'] = 'Revisions/v2_before_package_axis/manifest.json'
manifest['generation_results'] = 'generation-results-v2_1.json'
manifest['axis_edit_request'] = receipt['request_file']
manifest['user_feedback'] = '这三个导弹都歪了'
manifest['ue_import'] = dict(status='pending_targeted_import', target_ids=[11],
    report='Artifacts/GroundRogueCardIcons_20261118/PackageAxisRepair/install-report.json',
    card_bindings_unchanged=True)
manifest['preview'].update(url='http://127.0.0.1:8198/?revision=v2-1-package-axis',
    package_axis_before_toggle=True, download_package=receipt['version']+'.zip')
generation = read(BASE/'generation-results-v2.json')
generation['version'] = receipt['version']
generation['previous_results'] = 'generation-results-v2.json'
generation['package_axis_request'] = receipt['request_file']
generation['adopted'] = [dict(receipt, status='adopted', axis_correction=True) if icon['id'] == 11 else icon
                         for icon in generation['adopted']]
write(BASE/'generation-results-v2_1.json', generation)
write(catalog_path, catalog)
write(BASE/'manifest.json', manifest)
for icon in manifest['icons']:
    if icon['id'] != 11:
        assert sha(ROOT/icon['source_png']) == before['icon_hashes'][str(icon['id'])]
catalog_before = read(OUT/'catalog-before.json')
assert [x for x in catalog['icons'] if x['id'] != 11] == [x for x in catalog_before['icons'] if x['id'] != 11]
unchanged = {relative: sha(ROOT/relative) for relative in before['files'] if relative != 'ArtSource/UI/GroundMech/Icons/catalog.json'}
assert all(before['files'][key] == value for key, value in unchanged.items())
report = dict(success=True, changed_icon_ids=[11], before=old_entry, after=entry,
              unchanged_data_files=unchanged, other_icons_unchanged=True,
              original_generated_bytes_preserved=True, assistant_result_visual_test=False)
write(OUT/'source-update.json', report)
print(json.dumps(dict(success=True, sha256=digest, source_size=size, alpha=alpha_info), ensure_ascii=False))
