"""Editor/production helpers resolve generated model JSON, never legacy path columns."""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

def rows(sheet):
    return json.loads((ROOT / ('Data/Json/DT_GuLiStrikeModels_' + sheet + '.json')).read_text(encoding='utf-8-sig'))

def definition(model_id):
    return next(r for r in rows('Models') if r['Id'] == int(model_id))

def model_id(name):
    return next(r['Id'] for r in rows('Models') if r['Name'] == name)

def resource(model_id, candidate=False):
    return definition(model_id)['CandidateResourcePath' if candidate else 'ResourcePath']

def soldier_visual(row):
    model = definition(row['ModelId'])
    return dict(model_id=model['Id'], model_asset=model['ResourcePath'] if model['ResourceType'] != 'PresentationClass' else '',
                presentation_class=model['ResourcePath'] if model['ResourceType'] == 'PresentationClass' else '',
                vat_definition=model['VATDefinition'])

def ship_part_model(part_id):
    parts = json.loads((ROOT / 'Data/Json/DT_GuLiStrikeShip_Parts.json').read_text(encoding='utf-8-sig'))
    return next(r['ModelId'] for r in parts if r['PartId'] == part_id)

def write_binding_cells(book, model_id, path, vat=None, candidate=False):
    """Only model Excel owns mesh/VAT/presentation paths. Calls excelize-cli."""
    import sys
    sys.path.insert(0, str(Path(__file__).parent))
    from migrate_model_workbooks import read, write
    grid = read(book, 'Models')
    row = next(r for r in grid[3:] if int(r[0]) == model_id)
    row += [''] * (len(grid[0]) - len(row))
    row[grid[0].index('CandidateResourcePath' if candidate else 'ResourcePath')] = path
    if vat is not None: row[grid[0].index('CandidateVATDefinition' if candidate else 'VATDefinition')] = vat
    write(book, 'Models', grid)
