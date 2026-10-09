"""One-time model catalogue migration. Excel I/O exclusively uses excelize-cli.

The baseline is captured read-only from the open editor before schema changes.
Published IDs are retained on reruns. This script never imports UE assets.
"""
import json
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(r'D:/UE5.7/excelize-cli/bin/xlsx.exe')
EXCEL = ROOT / 'Data/Excel'
BASELINE = ROOT / 'Data/Models/migration-baseline-20261008.json'
REGISTRY = EXCEL / 'GuLiStrikeModels.xlsx'

def cli(*args):
    return subprocess.check_output([str(CLI), *map(str, args)], encoding='utf-8')

def read(book, sheet):
    return json.loads(cli('read', book, '--sheet', sheet, '--format', 'json', '--raw'))['rows']

def letter(n):
    out = ''
    while n:
        n, digit = divmod(n - 1, 26); out = chr(65 + digit) + out
    return out

def write(book, sheet, rows):
    for row in rows[3:]:
        for c, value in enumerate(row):
            if value == '': continue
            typ = rows[1][c]
            if typ == 'int': row[c] = int(value)
            elif typ == 'float': row[c] = float(value)
            elif typ == 'bool': row[c] = str(value).lower() in ('true', '1')
    cells = [{'cell': f'{letter(c)}{r}', 'value': value, 'type':
              'bool' if isinstance(value, bool) else 'int' if isinstance(value, int) else
              'float' if isinstance(value, float) else 'string'}
             for r, row in enumerate(rows, 1) for c, value in enumerate(row, 1)]
    with tempfile.TemporaryDirectory(prefix='guli-model-cells-') as tmp:
        data = Path(tmp) / 'cells.json'; data.write_text(json.dumps(cells, ensure_ascii=False), encoding='utf-8')
        cli('write', book, '--sheet', sheet, '--data-file', data)

def table(schema, rows):
    cols = [('id', 'int', True), ('name', 'str', True), ('Note', 'str', False)] + schema
    return [[x[i] for x in cols] for i in (0, 1)] + [
        ['Necessary' if x[2] else 'Optional' for x in cols]] + [
        [row.get(x[0], '') for x in cols] for row in rows]

def migrate_reference(book_name, sheet, remove, field, values):
    book = EXCEL / book_name
    rows = read(book, sheet)
    names = rows[0]
    for key in remove:
        if key in names:
            col = names.index(key)
            cli('delete-cols', book, '--sheet', sheet, '--col', letter(col + 1), '--count', 1)
            rows = read(book, sheet); names = rows[0]
    if field not in names:
        col = len(names)
        for row in rows: row += [''] * (col + 1 - len(row))
        rows[0][col], rows[1][col], rows[2][col] = field, 'int', 'Necessary'
    else:
        col = names.index(field)
    for row in rows[3:]:
        row += [''] * (len(rows[0]) - len(row))
        row[col] = values[row[rows[0].index('name')]]
    write(book, sheet, rows)

def main():
    if (ROOT / 'Data/Models/published-model-ids.json').exists():
        raise SystemExit('Initial migration is complete. Maintain Models/Parts/parameters in Excel; do not rebuild the catalogue from historical B configs.')
    baseline = json.loads(BASELINE.read_text(encoding='utf-8'))
    soldiers = json.loads((ROOT / 'Data/Json/DT_GuLiStrikeCommander_Soldiers.json').read_text(encoding='utf-8-sig'))
    buildings = json.loads((ROOT / 'Data/Json/DT_GuLiStrikeBuildings_Buildings.json').read_text(encoding='utf-8-sig'))
    art = json.loads((ROOT / 'ArtSource/LocalTeamColorReview_20261008/source-readback.json').read_text(encoding='utf-8-sig'))
    existing = {}
    if REGISTRY.exists():
        grid = read(REGISTRY, 'Models')
        existing = {r[1]: int(r[0]) for r in grid[3:] if r and r[0]}
    models, parts, parameters, regions = [], [], [], []
    by_name, by_path = {}, {}
    path_info = {r['path']: r for r in baseline['models']}
    next_extra = 8001

    def add(name, display, path, model_id=None, team=False, vat=''):
        nonlocal next_extra
        if name in by_name: return by_name[name]
        if model_id is None:
            model_id = existing.get(name)
            if model_id is None:
                while next_extra in existing.values() or next_extra in by_name.values(): next_extra += 1
                model_id = next_extra; next_extra += 1
        if name in existing and existing[name] != model_id:
            raise ValueError(f'Published model ID changed: {name}')
        typ = path_info.get(path, {}).get('type', 'PresentationClass' if path.endswith('_C') else 'StaticMesh')
        models.append(dict(id=model_id, name=name, DisplayName=display, ResourceType=typ, ResourcePath=path,
                           VATDefinition=vat, bTeamColorEnabled=team, BluePrimaryHex='#6AA4BE',
                           BlueSecondaryHex='#274E61', EnemyPrimaryHex='#A34053', EnemySecondaryHex='#662249',
                           CandidateResourcePath='', CandidateVATDefinition='',
                           Note='已审资源迁移；B_v1 配色候选须逐模型 B 放行才切换正式路径' if team else '本轮保留现有配色'))
        by_name[name] = model_id
        by_path.setdefault(path, model_id)
        return model_id

    soldier_refs, building_refs, ship_refs = {}, {}, {}
    for i, row in enumerate(soldiers, 1001):
        name = row['Name']
        # JSON on a rerun already contains ModelId; read its catalogue resource instead.
        if 'ModelAsset' in row:
            path = row['PresentationClass'] or row['ModelAsset']
            vat = row.get('VATDefinition', '')
        else:
            grid = read(REGISTRY, 'Models'); data = dict(zip(grid[0], next(r for r in grid[3:] if r[1] == name)))
            path, vat = data['ResourcePath'], data.get('VATDefinition', '')
        soldier_refs[name] = add(name, row['DisplayName'], path, i, name not in ('ElectromagneticMiner', 'ConstructionVehicle'), vat)
    for i, row in enumerate(buildings, 2001):
        name = row['Name']
        path = row.get('Mesh')
        if name == 'ResourceFactory':
            path = '/Game/GuLiStrike/Buildings/ResourceProcessingFactory/Blueprints/BP_ResourceProcessingFactory.BP_ResourceProcessingFactory_C'
        if not path:
            grid = read(REGISTRY, 'Models'); path = next(r for r in grid[3:] if r[1] == name)[grid[0].index('ResourcePath')]
        building_refs[name] = add(name, row['DisplayName'], path, i, True)
    ssf = [m for m in art['models'] if m['name'].startswith('SSF_')]
    for i, m in enumerate(ssf, 3001): add(m['name'], m.get('display_name', m['name']), m['mesh'], i, True)
    ground = {x['component']: x for x in baseline['ground']}
    add('GroundMech', '玩家地面机甲', ground['CharacterMesh0']['mesh']['path'], 4001)
    for i, key in enumerate(['Armor', 'Shoulder', 'Machinegun'], 4002):
        child = add('GroundMech_' + key, '地面机甲 ' + key, ground[key]['mesh']['path'], i)
        parts.append(dict(ModelId=4001, PartKey=key, ComponentPath=key, ChildModelId=child))
    parts.append(dict(ModelId=4001, PartKey='Legs', ComponentPath='CharacterMesh0', ChildModelId=0))
    hull_refs = {}
    for i, ship in enumerate(baseline['ships'], 5001):
        name = 'ShipDefaultHull' if i == 5001 else 'ShipDreadnoughtHull'
        hull_refs['Default' if i == 5001 else 'Dreadnought'] = add(name, name, ship['hull']['mesh']['path'], i)
    for i, p in enumerate(baseline['ships'][0]['parts'], 5101):
        # Two Thor part classes deliberately share one mesh identity; their gameplay IDs remain distinct.
        model_id = by_path.get(p['mesh']['path']) or add(p['part_id'], p['part_id'], p['mesh']['path'], i)
        ship_refs[p['part_id']] = model_id
    for i, p in enumerate(baseline['legacy_parts'], 5801):
        if p['mesh']:
            ship_refs[p['part_id']] = by_path.get(p['mesh']['path']) or add(p['part_id'], p['part_id'], p['mesh']['path'], i)
    # Retired Excel entries have no active Blueprint; keep their identity and explicit placeholder resource.
    for i, key in enumerate(['Engine_Heavy', 'Engine_Standard', 'Weapon_Laser', 'Weapon_RocketPod'], 5901):
        if key not in ship_refs: ship_refs[key] = add('Legacy_' + key, key, '/Engine/BasicShapes/Cube.Cube', i)
    add('Wingman', '僚机', '/Game/GuLiStrike/Wingman/SM_Wingman_Mass.SM_Wingman_Mass', 6001)
    ores = sorted(x for x in path_info if '/Resources/Ores/' in x)
    for i, path in enumerate(ores, 7001): add(path.rsplit('.', 1)[1].removeprefix('SM_'), path.rsplit('.', 1)[1], path, i)
    for assembly in baseline['assemblies']:
        root_id = by_path[assembly['class']]
        seen_components = set()
        for p in assembly['parts']:
            key = p['component'].removesuffix('_GEN_VARIABLE')
            if key in seen_components: continue
            seen_components.add(key)
            path = p['mesh']['path']
            child = by_path.get(path) or add(re.sub(r'[^A-Za-z0-9_]', '_', path.rsplit('.', 1)[1]), p['component'], path)
            parts.append(dict(ModelId=root_id, PartKey=key, ComponentPath=key, ChildModelId=child))

    # Every leaf has a root binding. Assembly children also expose material bindings through the root ModelId.
    for model in models:
        if not any(p['ModelId'] == model['id'] for p in parts):
            parts.append(dict(ModelId=model['id'], PartKey='Root', ComponentPath='', ChildModelId=0))
    for p in parts:
        model = next(m for m in models if m['id'] == (p['ChildModelId'] or p['ModelId']))
        for slot in path_info.get(model['ResourcePath'], {}).get('slots', []):
            for typ, names in [('Vector', slot['vectors']), ('Scalar', slot['scalars'])]:
                for name in names:
                    managed = name == 'Team Color' and typ == 'Vector'
                    protected = bool(re.search(r'VAT|Bone|Anim|PositionTexture|RotationTexture|Pivot|HitTime|Rigid', name, re.I))
                    parameters.append(dict(ModelId=p['ModelId'], PartKey=p['PartKey'], MaterialSlotName=slot['name'],
                                           ParameterKey='TeamPrimary' if managed else name, ParameterName=name,
                                           ParameterType=typ, Driver='MID', CustomDataIndex=-1, Scope='Existing',
                                           bRuntimeWritable=not protected and not managed, bTeamManaged=managed,
                                           DefaultScalar=0.0, DefaultR=0.0, DefaultG=0.0, DefaultB=0.0, DefaultA=1.0,
                                           Note='默认值取材质；实际值读取目标 MID，阵营值由本地系统管理' if managed else '实际值读取目标材质；动画内部参数只读' if protected else '实际值读取目标材质'))
    role_ids = ['Cream', 'Sand', 'Gray', 'TeamLight', 'TeamDark', 'Amber', 'Cyan', 'TeamLightLamp']
    for config in sorted((ROOT / 'ArtSource/LocalTeamColorProduction_B_v1_20261008/Configs').glob('*.json')):
        cfg = json.loads(config.read_text(encoding='utf-8'))
        mid = by_name[cfg['id']]
        native = cfg['construction_B'].get('native_regions', {})
        for obj, zones in native.items():
            for zone_index, z in enumerate(zones):
                roles = [z['role']] if z['role'] in role_ids else [role for token, role in
                    [('team-light', 'TeamLight'), ('team-dark', 'TeamDark'), ('cream', 'Cream'), ('sand', 'Sand'), ('gray', 'Gray')]
                    if token in z['role'].lower()]
                if not roles: raise ValueError(f'Unresolved B region: {config.name}: {z}')
                for role in roles:
                    regions.append(dict(ModelId=mid, PartKey='Root', RegionKey=re.sub(r'[^A-Za-z0-9_]', '_', obj + '_' + str(z['source_component']) + '_' + role + '_' + str(zone_index)),
                                        DisplayName=str(z['source_component']) + ': ' + z['role'], PaintRole=role_ids.index(role),
                                        MaskSource='VertexColorAlpha', MaskId=role_ids.index(role), Scope='Candidate',
                                        Note='Bv1_ColorRegion 面区；原 RGB / UV / 几何保留；B 待审'))
    # Construction body follows the same approved partition rules as the operational gun.
    for r in list(regions):
        if r['ModelId'] == by_name['BiZhiMao']:
            regions.append({**r, 'ModelId': by_name['BiZhiMaoConstruction']})
    for m in models:
        if m['bTeamColorEnabled']:
            if not any(r['ModelId'] == m['id'] for r in regions):
                regions.append(dict(ModelId=m['id'], PartKey='Root', RegionKey='ExistingTeamMask', DisplayName='既有标记区',
                                    PaintRole=3, MaskSource='ExistingMaterialMask', MaskId=3, Scope='Existing', Note='保留已审分区'))
            for key, typ, index in [('TeamPrimary', 'Vector', 8), ('TeamSecondary', 'Vector', 12), ('TeamEnabled', 'Scalar', 16), ('TeamLightStrength', 'Scalar', 17)]:
                parameters.append(dict(ModelId=m['id'], PartKey='Root', MaterialSlotName='*', ParameterKey=key,
                                       ParameterName='GuLi_' + key, ParameterType=typ, Driver='CPD', CustomDataIndex=index,
                                       Scope='Candidate', bRuntimeWritable=False, bTeamManaged=True, DefaultScalar=0.0,
                                       DefaultR=0.0, DefaultG=0.0, DefaultB=0.0, DefaultA=1.0,
                                       Note='统一材质函数；共享网格，组件 CPD；不占 VAT 实例槽'))
    schemas = {
        'Models': [('DisplayName', 'str', True), ('ResourceType', 'str', True), ('ResourcePath', 'softobject', True),
                   ('VATDefinition', 'softobject', False), ('bTeamColorEnabled', 'bool', True),
                   *[(k, 'str', True) for k in ['BluePrimaryHex', 'BlueSecondaryHex', 'EnemyPrimaryHex', 'EnemySecondaryHex']],
                   ('CandidateResourcePath', 'softobject', False), ('CandidateVATDefinition', 'softobject', False)],
        'Parts': [('ModelId', 'int', True), ('PartKey', 'str', True), ('ComponentPath', 'str', False), ('ChildModelId', 'int', False)],
        'MaterialParameters': [('ModelId', 'int', True), ('PartKey', 'str', True), ('MaterialSlotName', 'str', True),
                               ('ParameterKey', 'str', True), ('ParameterName', 'str', True), ('ParameterType', 'str', True),
                               ('Driver', 'str', True), ('CustomDataIndex', 'int', True), ('Scope', 'str', True),
                               ('bRuntimeWritable', 'bool', True), ('bTeamManaged', 'bool', True),
                               *[(k, 'float', False) for k in ['DefaultScalar', 'DefaultR', 'DefaultG', 'DefaultB', 'DefaultA']]],
        'ColorRegions': [('ModelId', 'int', True), ('PartKey', 'str', True), ('RegionKey', 'str', True), ('DisplayName', 'str', True),
                         ('PaintRole', 'int', True), ('MaskSource', 'str', True), ('MaskId', 'int', True), ('Scope', 'str', True)]}
    if not REGISTRY.exists(): cli('new', REGISTRY, '--sheet', 'Models')
    sheets = json.loads(cli('info', REGISTRY))['sheets']
    for key, rows in [('Models', models), ('Parts', parts), ('MaterialParameters', parameters), ('ColorRegions', regions)]:
        if key not in {s['name'] for s in sheets}: cli('add-sheet', REGISTRY, '--name', key)
        if key != 'Models':
            for i, r in enumerate(rows, 1): r.update(id=i, name=key + '_' + str(i))
        write(REGISTRY, key, table(schemas[key], rows))
        cli('style', REGISTRY, '--sheet', key, '--range', 'A1:' + letter(len(schemas[key]) + 3) + '3', '--bold', '--bg', '274E61', '--font-color', 'FFFFFF', '--wrap')
        cli('col-width', REGISTRY, '--sheet', key, '--col', 'A', '--to', letter(len(schemas[key]) + 3), '--width', 20)
    migrate_reference('GuLiStrikeCommander.xlsx', 'Soldiers', ['ModelAsset', 'PresentationClass', 'VATDefinition'], 'ModelId', soldier_refs)
    migrate_reference('GuLiStrikeBuildings.xlsx', 'Buildings', ['Mesh'], 'ModelId', building_refs)
    migrate_reference('GuLiStrikeShip.xlsx', 'Parts', [], 'ModelId', {**ship_refs, '重型引擎': ship_refs['Engine_Heavy'], '标准引擎': ship_refs['Engine_Standard'], '激光炮': ship_refs['Weapon_Laser'], '火箭巢': ship_refs['Weapon_RocketPod']})
    ship_book = EXCEL / 'GuLiStrikeShip.xlsx'
    grid = read(ship_book, 'Parts')
    for p in baseline['ships'][0]['parts']:
        if any(r[1] == p['part_id'] for r in grid[3:]): continue
        stats = p['stats']
        muzzle = [float(v) for v in re.findall(r'[XYZ]=([-+\d.eE]+)', stats.get('muzzle_offset', ''))] or [0, 0, 0]
        values = dict(id=len(grid) - 2, name=p['part_id'], Note='现有活跃部件 CDO 数值与资源迁移；socket 和部件类保持', PartId=p['part_id'],
                      Type='Weapon' if 'damage' in stats else 'Module', PartMass=float(stats.get('part_mass', 10)),
                      Thrust=float(stats.get('thrust', 0)), Damage=float(stats.get('damage', 0)), FireRate=float(stats.get('fire_rate', 0)),
                      MuzzleX=muzzle[0], MuzzleY=muzzle[1], MuzzleZ=muzzle[2], ProjectileClass=stats.get('projectile_class', '').replace('None', ''),
                      ModelId=ship_refs[p['part_id']])
        grid.append([values.get(k, '') for k in grid[0]])
    write(ship_book, 'Parts', grid)
    migrate_reference('GuLiStrikeShip.xlsx', 'Tuning', [], 'HullModelId', hull_refs)
    mech = EXCEL / 'GuLiStrikeMech.xlsx'
    if 'Visuals' not in {s['name'] for s in json.loads(cli('info', mech))['sheets']}: cli('add-sheet', mech, '--name', 'Visuals')
    write(mech, 'Visuals', table([('ModelId', 'int', True)], [dict(id=1, name='Default', ModelId=4001, Note='Ground 席位；保留原骨骼、socket、装配和配色')]))
    (ROOT / 'Data/Models/migration-manifest.json').write_text(json.dumps(dict(models=len(models), parts=len(parts), parameters=len(parameters), regions=len(regions),
        soldier_refs=soldier_refs, building_refs=building_refs, ship_part_refs=ship_refs, hull_refs=hull_refs,
        baseline=str(BASELINE.relative_to(ROOT)), native_compile=False, ue_import=False), ensure_ascii=False, indent=2), encoding='utf-8')
    print(f'Migrated {len(models)} models, {len(parts)} parts, {len(parameters)} parameters, {len(regions)} regions. UE import deferred until native compile.')

if __name__ == '__main__': main()
