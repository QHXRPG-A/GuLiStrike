"""Idempotent WM01 missile card source migration, using the project excelize CLI.

Only named card fields/rows and public text keys are changed. Native schema must
be compiled before importing the resulting DataTables into the editor.
"""
import hashlib
import json
from author_rogue_card_text import DESCRIPTION_PATTERNS
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI = Path('D:/UE5.7/excelize-cli/bin/xlsx.exe')
OUT = ROOT / 'Artifacts/WM01MissileCards/DataSource'


def read(book, sheet):
    path = ROOT / 'data/Excel' / book
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    result = subprocess.run([str(CLI), 'read', str(path), '--sheet', sheet, '--format', 'json', '--raw'],
                            check=True, capture_output=True, encoding='utf-8')
    return path, digest, [r or [] for r in json.loads(result.stdout)['rows']]


def column(index):
    value = ''
    while index:
        index, remainder = divmod(index - 1, 26)
        value = chr(65 + remainder) + value
    return value


def save(path, digest, sheet, changes):
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise RuntimeError(f'Source changed during preparation: {path}')
    OUT.mkdir(parents=True, exist_ok=True)
    payload = OUT / (path.stem + '-' + sheet + '-cells.json')
    payload.write_text(json.dumps(changes, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    subprocess.run([str(CLI), 'write', str(path), '--sheet', sheet, '--data-file', str(payload)], check=True)


def main():
    path, digest, rows = read('GuLiStrikeRogueCards.xlsx', 'Cards')
    names = list(rows[0])
    changes = []
    def put(row, name, value, kind='string'):
        changes.append({'cell': column(names.index(name) + 1) + str(row), 'value': value, 'type': kind})
    new = {'RequiredCardIds': ('str[]', 'Necessary'), 'ExcludedCardIds': ('str[]', 'Necessary'),
           'MaxAcquisitions': ('int', 'Necessary'), 'BonusCount': ('int', 'Necessary')}
    for name, (kind, mark) in new.items():
        if name not in names:
            names.append(name)
        for row, value in ((1, name), (2, kind), (3, mark)):
            put(row, name, value)
    put(3, 'BonusPercent', 'Optional')
    ids = {r[0]: i for i, r in enumerate(rows, 1) if i >= 4 and r and r[0]}
    for ident in ('01.01', '02.01', '03.01'):
        if ident not in ids:
            raise RuntimeError(f'Missing baseline card {ident}')
        i = ids[ident]
        # Preserve already authored requirements/exclusions on rerun.
        existing = dict(zip(rows[0], rows[i - 1]))
        required = json.loads(existing.get('RequiredCardIds') or '[]')
        if ident == '03.01' and '04.01' not in required:
            required.append('04.01')
        put(i, 'RequiredCardIds', json.dumps(required))
        for field, default in (('ExcludedCardIds', '[]'), ('MaxAcquisitions', 0), ('BonusCount', 0)):
            if field not in existing or existing[field] == '':
                put(i, field, default, 'int' if isinstance(default, int) else 'string')
    for ident, slug, title, effect, maximum, count, required in (
            ('04.01', 'MissilePod', '重防导弹仓', 'GuLiRogueCardMissilePodEffect', 1, 0, []),
            ('05.01', 'RainSalvo', '雨点攻势', 'GuLiRogueCardMissileCountEffect', 0, 1, ['04.01'])):
        i = ids.get(ident, max([len(rows)] + list(ids.values())) + 1)
        ids[ident] = i
        material = f'/Game/GuLiStrike/Cards/Commander/WM01/{slug}/Materials/MI_{slug}_ModelComic_v1'
        fields = {'id': ident, 'name': f'WM01_{slug}_Lv1', 'Type': 1, 'UnitTypeId': 2,
                  'Note': f'己方本局当前及后续重防号：{title}。' + ('解锁导弹技能与导弹仓，每队一次。' if maximum else '每次Q的同时齐射数量加1，可重复叠加。'),
                  'TextIds': json.dumps([f'Card.WM01.{slug}.Title', f'Card.WM01.{slug}.Description']),
                  'ImplementationClass': '/Script/GuLiStrike.' + effect, 'BonusPercent': 0.0,
                  'FrontMaterial': material + '.' + material.rsplit('/', 1)[-1],
                  'UpgradeVfx': '/Game/GuLiStrike/FX/RogueCards/NS_RogueUpgrade_Lite.NS_RogueUpgrade_Lite',
                  'UpgradeVfxScale': 6.0, 'UpgradeVfxColor': '(R=20,G=6.930114,B=0.933554,A=1)',
                  'RequiredCardIds': json.dumps(required), 'ExcludedCardIds': '[]',
                  'MaxAcquisitions': maximum, 'BonusCount': count}
        for field, value in fields.items():
            put(i, field, value, 'int' if isinstance(value, int) else 'float' if isinstance(value, float) else 'string')
    save(path, digest, 'Cards', changes)

    path, digest, rows = read('GuLiStrikeGameTexts.xlsx', 'Texts')
    keys = {r[0]: i for i, r in enumerate(rows, 1) if i >= 4 and r and r[0]}
    changes = []
    for key, note, text in (
            ('Card.WM01.MissilePod.Title', '重防号肉鸽卡标题', '重防导弹仓'),
            ('Card.WM01.MissilePod.Description', '重防号导弹槽解锁；无数值占位符', DESCRIPTION_PATTERNS['MissilePod']),
            ('Card.WM01.RainSalvo.Title', '重防号肉鸽卡标题', '雨点攻势'),
            ('Card.WM01.RainSalvo.Description', '重防号肉鸽卡说明；{0}来自BonusCount', DESCRIPTION_PATTERNS['RainSalvo']),
            ('UI.RogueCards.Empty', '无合格候选卡时显示', '暂无可选牌'),
            ('UI.RogueCards.AcquisitionLimit', '结算复核：达到获取次数', '该卡已达到本局获取上限'),
            ('UI.RogueCards.RequirementNotMet', '结算复核：缺少依赖', '尚未获得该卡要求的前置卡牌'),
            ('UI.RogueCards.Excluded', '结算复核：双向互斥', '该卡与本队已获得的卡牌互斥')):
        i = keys.get(key, max([len(rows)] + list(keys.values())) + 1)
        keys[key] = i
        changes.extend({'cell': c + str(i), 'value': v, 'type': 'string'}
                       for c, v in zip('ABC', (key, note, text)))
    save(path, digest, 'Texts', changes)


if __name__ == '__main__':
    main()
