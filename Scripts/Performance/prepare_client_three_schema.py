"""Idempotent optional VFX columns, through the project's excelize CLI."""
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'outputs/performance/20261009-client-three-optimizations'
CLI = Path('D:/UE5.7/excelize-cli/bin/xlsx.exe')
BOOK = ROOT / 'Data/Excel/GuLiStrikeVFX.xlsx'
FIELDS = ['ReducedResourcePath', 'MinimalResourcePath', 'BatchResourcePath',
          'ReducedBatchResourcePath', 'MinimalBatchResourcePath']

def main():
    current = json.loads(subprocess.check_output([str(CLI), 'read', str(BOOK), '--sheet', 'Effects',
                         '--format', 'json'], text=True, encoding='utf-8'))['rows']
    names = current[0]
    cells = []
    for offset, field in enumerate(FIELDS, 8):
        col = chr(64 + offset)
        existing = names[offset - 1] if len(names) >= offset else ''
        if existing not in ('', field):
            raise RuntimeError(f'Unexpected occupied column {col}: {existing}')
        for row, value in [(1, field), (2, 'softobject'), (3, 'Optional')]:
            cells.append({'cell': f'{col}{row}', 'value': value, 'type': 'string'})
    OUT.mkdir(parents=True, exist_ok=True)
    payload = OUT / 'vfx-schema-cells.json'
    payload.write_text(json.dumps(cells), encoding='utf-8')
    subprocess.run([str(CLI), 'write', str(BOOK), '--sheet', 'Effects', '--data-file', str(payload)], check=True)
    print(json.dumps({'columns': FIELDS, 'preserved_rows': len(current)-3}))

if __name__ == '__main__':
    main()
