"""Normalize 重防号 nomenclature without changing stable technical identifiers.

Scoped text discovery uses rg; excluded asset/build directories are never walked.
Excel access is exclusively through the project's excelize CLI. Default is audit.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PERSONAL = Path('C:/Users/a/.codex/skills/guli-rogue-card-production')
OLD = ''.join(chr(v) for v in (25112, 20105, 26426, 22120))
NEW = '重防号'
XLSX = Path('D:/UE5.7/excelize-cli/bin/xlsx.exe')
OUT = ROOT / 'Artifacts/WM01NameMigration'
TEXT_EXTENSIONS = ('md', 'txt', 'json', 'jsonl', 'py', 'h', 'cpp', 'ini', 'csv', 'yaml', 'yml', 'toml', 'html', 'js', 'ts', 'ps1')
EXCLUDES = ('Intermediate/**', 'Saved/**', 'Binaries/**', 'DerivedDataCache/**', 'Content/Assets/**', 'Downloads/**',
            '**/Intermediate/**', '**/Saved/**', '**/Binaries/**', '**/DerivedDataCache/**', '**/Downloads/**',
            '**/node_modules/**', '**/__pycache__/**', '**/.git/**', '**/.venv/**')

def sha(data):
    return hashlib.sha256(data).hexdigest()

def run(args):
    result = subprocess.run([str(x) for x in args], cwd=ROOT, capture_output=True, encoding='utf-8')
    if result.returncode:
        raise RuntimeError(result.stderr or result.stdout)
    return result.stdout

def scope():
    paths = [ROOT / p for p in ('Source', 'Config', 'Scripts', 'Tools', 'Progress', '.agents', '.github', '.zcode',
        'ArtSource', 'Artifacts', 'TestResults', 'data/Json', 'README.txt', 'AGENTS.md', '相关指令.txt')]
    paths.extend((ROOT / 'Plugins').glob('*/Source'))
    paths.append(PERSONAL)
    return [p for p in paths if p.exists()]

def text_paths():
    args = ['rg', '--files', '--hidden', '--no-ignore', '-0']
    for ext in TEXT_EXTENSIONS:
        args += ['--glob', '*.' + ext]
    for pattern in EXCLUDES:
        args += ['--glob', '!' + pattern]
    args += [str(p) for p in scope()]
    names = {Path(p) for p in run(args).split('\0') if p and OLD in Path(p).name}
    matches = ['rg', '--files-with-matches', '--hidden', '--no-ignore', '--null', '--fixed-strings', '-e', OLD]
    for ext in TEXT_EXTENSIONS:
        matches += ['--glob', '*.' + ext]
    for pattern in EXCLUDES:
        matches += ['--glob', '!' + pattern]
    matches += [str(p) for p in scope()]
    found = subprocess.run(matches, cwd=ROOT, capture_output=True, encoding='utf-8')
    if found.returncode not in (0, 1):
        raise RuntimeError(found.stderr)
    return sorted(names | {Path(p) for p in found.stdout.split('\0') if p})

def ground_context(path):
    p = path.as_posix()
    return ('GuLiWarMachinePlaceholderPawn.h' in p or 'TheRiftbreaker' in p or
        ('/Progress/' in p and any(part.startswith(('202608', '20260901-', '20260902-', '20260903-', '20260905-')) for part in path.parts)))

def normalize(text, path):
    text = text.replace('地面' + OLD, '地面机甲').replace('Ground ' + OLD, 'Ground 地面机甲').replace('Ground' + OLD, 'Ground地面机甲')
    if path.name == '战斗.md':
        return ''.join(line.replace(OLD, NEW if '300.5' in line else '地面机甲') for line in text.splitlines(keepends=True))
    return text.replace(OLD, '地面机甲' if ground_context(path) else NEW)

def migrate_prompt_records(original, normalized):
    """Preserve original hashes while labeling normalized prompts as revised text."""
    if isinstance(original, list):
        return [migrate_prompt_records(a, b) for a, b in zip(original, normalized)]
    if not isinstance(original, dict):
        return normalized
    result = {k: migrate_prompt_records(v, normalized[k]) for k, v in original.items()}
    prompt = original.get('prompt')
    if isinstance(prompt, str) and OLD in prompt:
        result['original_prompt_utf8_sha256'] = sha(prompt.encode('utf-8'))
        result['normalized_prompt_utf8_sha256'] = sha(result['prompt'].encode('utf-8'))
        result['prompt_record_kind'] = 'terminology_normalized_on_2026-09-29; not the original generation payload'
        if 'prompt_sha256' in result:
            result['original_recorded_prompt_sha256'] = original['prompt_sha256']
            result['prompt_sha256'] = result['normalized_prompt_utf8_sha256']
        # sha256 in image-generation manifests may refer to the image, not the prompt.
        if 'unchanged' in result:
            result['original_unchanged'] = original['unchanged']
            result['unchanged'] = False
            if 'sha256' in result:
                result['original_recorded_sha256'] = original['sha256']
                result['sha256'] = result['normalized_prompt_utf8_sha256']
    return result

def migrate_text(apply):
    records = []
    for path in text_paths():
        if path.is_relative_to(OUT) or path.is_relative_to(ROOT / 'Progress/_Index'):
            continue
        before = path.read_bytes()
        try:
            text = before.decode('utf-8-sig')
        except UnicodeDecodeError:
            # Legacy Chinese project instructions may use GB18030.
            try:
                text = before.decode('gb18030')
                encoding = 'gb18030'
            except UnicodeDecodeError:
                continue
        else:
            encoding = 'utf-8-sig' if before.startswith(b'\xef\xbb\xbf') else 'utf-8'
        new_text = normalize(text, path)
        target = path.with_name(normalize(path.name, path))
        if text == new_text and target == path:
            continue
        if path.suffix.lower() == '.json' and '"prompt"' in text:
            new_text = json.dumps(migrate_prompt_records(json.loads(text), json.loads(new_text)), ensure_ascii=False, indent=2) + '\n'
        after = new_text.encode(encoding)
        allowed_root = PERSONAL.resolve() if path.is_relative_to(PERSONAL) else ROOT.resolve()
        assert path.resolve().is_relative_to(allowed_root) and target.resolve().is_relative_to(allowed_root)
        assert target == path or not target.exists(), target
        record = {'path': str(target), 'previous_path_sha256': sha(str(path).encode()),
                  'original_sha256': sha(before), 'normalized_sha256': sha(after), 'renamed': target != path,
                  'replaced_occurrences': text.count(OLD), 'ground_context': ground_context(path)}
        if apply:
            assert sha(path.read_bytes()) == record['original_sha256'], f'Concurrent edit: {path}'
            if before != after:
                path.write_bytes(after)
            if target != path:
                path.rename(target)
        records.append(record)
    return records

def col_name(index):
    result = ''
    while index:
        index, n = divmod(index - 1, 26)
        result = chr(65 + n) + result
    return result

def migrate_excel(apply):
    records = []
    for path in sorted((ROOT / 'data/Excel').rglob('*.xlsx')):
        if path.name.startswith('~$'):
            continue
        sheets = json.loads(run([XLSX, 'sheets', path, '--json']))['sheets']
        for sheet in sheets:
            rows = json.loads(run([XLSX, 'read', path, '--sheet', sheet, '--format', 'json']))['rows']
            changes = []
            for ri, row in enumerate(rows, 1):
                for ci, value in enumerate(row or [], 1):
                    if isinstance(value, str) and OLD in value:
                        changes.append({'cell': col_name(ci) + str(ri), 'value': normalize(value, path), 'type': 'string'})
            if not changes:
                continue
            before = sha(path.read_bytes())
            if apply:
                with tempfile.TemporaryDirectory(prefix='guli-name-cells-') as tmp:
                    cells = Path(tmp) / 'cells.json'
                    cells.write_text(json.dumps(changes, ensure_ascii=False), encoding='utf-8')
                    assert sha(path.read_bytes()) == before, f'Concurrent workbook edit: {path}'
                    run([XLSX, 'write', path, '--sheet', sheet, '--data-file', cells])
                actual = json.loads(run([XLSX, 'read', path, '--sheet', sheet, '--format', 'json']))['rows']
                assert all(OLD not in str(value) for row in actual for value in (row or [])), (path, sheet)
            records.append({'path': str(path), 'sheet': sheet, 'cells': changes, 'original_sha256': before,
                            'normalized_sha256': sha(path.read_bytes()) if apply else None})
    return records

def refresh_skill_provenance(apply):
    for base in (ROOT / '.agents/skills/guli-rogue-card-production', PERSONAL):
        path = base / 'references/prompts/prompt-provenance.json'
        record = json.loads(path.read_text(encoding='utf-8'))
        if record.get('kind') == 'terminology_normalized_archive':
            continue
        record['kind'] = 'terminology_normalized_archive'
        record['normalized_on'] = '2026-09-29'
        record['subject'] = '重防号 WM01 classic comic prompt templates; revised nomenclature, original hashes retained'
        for item in record['files']:
            payload = (path.parent / item['file']).read_bytes()
            item['original_sha256'] = item['sha256']
            item['original_bytes'] = item['bytes']
            item['sha256'] = sha(payload)
            item['bytes'] = len(payload)
            item['copied_byte_for_byte'] = False
            item['matches_normalized_project_source'] = payload == Path(item['source']).read_bytes()
        record['note'] = '重防号名称迁移后的可复用文本；不是历史实际调用原文。原始哈希保留，原文由 Git 历史追溯。LF/CRLF保持原文件约定，后续实际生成另存完整prompt与payload哈希。'
        if apply:
            path.write_text(json.dumps(record, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

def main():
    sys.stdout.reconfigure(encoding='utf-8')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    if args.apply and (OUT / 'applied.json').exists():
        raise RuntimeError('Existing migration report: inspect before applying again')
    text_records = migrate_text(args.apply)
    excel_records = migrate_excel(args.apply)
    if args.apply:
        refresh_skill_provenance(True)
    report = {'date': '2026-09-29', 'applied': args.apply, 'canonical_unit': NEW,
              'stable_identifiers': ['WM01', 'UnitTypeId=2', 'WarMachine asset/class paths'],
              'text_files': text_records, 'excel_sheets': excel_records,
              'history_note': 'Human-readable nomenclature normalized by explicit user request; original content hashes retained. Images, IDs and binary assets are not edited by this script.'}
    OUT.mkdir(parents=True, exist_ok=True)
    filename = 'applied.json' if args.apply else 'audit.json'
    destination = OUT / filename
    if destination.exists() and args.apply:
        raise RuntimeError('Existing migration report: inspect before applying again')
    destination.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'applied': args.apply, 'text_files': len(text_records), 'renamed_files': sum(r['renamed'] for r in text_records),
                      'excel_sheets': len(excel_records), 'cells': sum(len(r['cells']) for r in excel_records),
                      'ground_files': [r['path'] for r in text_records if r['ground_context']], 'report': str(destination)}, ensure_ascii=False))

if __name__ == '__main__':
    main()
