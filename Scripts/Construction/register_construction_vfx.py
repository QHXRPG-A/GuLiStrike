"""Append construction VFX IDs through excelize; existing IDs and unrelated cells are preserved."""
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'outputs/construction-vfx'
CLI = 'D:/UE5.7/excelize-cli/bin/xlsx.exe'


def command(*args):
    result = subprocess.run([CLI, *map(str, args)], capture_output=True, text=True, encoding='utf-8', check=True)
    return json.loads(result.stdout) if result.stdout.strip().startswith('{') else result.stdout


def main():
    book = ROOT / 'Data/Excel/GuLiStrikeVfx.xlsx'
    rows = command('read', book, '--sheet', 'Effects', '--format', 'json', '--raw')['rows']
    current = {r[1]: r for r in rows[3:] if r and r[0]}
    number = max(int(r[0]) for r in current.values()) + 1
    last = len(rows)
    edits, ids = [], {}
    for name, asset, note in [
        ('BuildingConstructionComplete', 'NS_ConstructionComplete', '矿厂完工底部UpgradeGlow一次性完整闪光'),
        ('BuildingConstructionTopLoop', 'NS_ConstructionTopLoop', '矿厂屋檐淡青绿呼吸光柱、漂浮粒子与独立电火花'),
        ('BuildingConstructionLaser', 'NS_ConstructionLaser_Purple', '建造车左右紫色激光、蓝白落点火花'),
        ('BuildingConstructionColumnTail', 'M_ConstructionColumnTail', '施工弱光柱材质，提取UpgradeGlow残尾'),
        ('BuildingConstructionMote', 'M_ConstructionMote', '施工向上漂浮三角粒子材质'),
    ]:
        path = f'/Game/GuLiStrike/Buildings/Construction/{asset}.{asset}'
        if name in current:
            assert current[name][3] == path, (name, current[name])
            ids[name] = int(current[name][0])
            continue
        ids[name] = number
        last += 1
        edits.extend({'cell': f'{chr(65+i)}{last}', 'value': value}
                     for i,value in enumerate([number,name,note,path,1,1,1]))
        number += 1
    OUT.mkdir(parents=True, exist_ok=True)
    if edits:
        cells = OUT / 'registry-cells.json'
        cells.write_text(json.dumps(edits,ensure_ascii=False),encoding='utf-8')
        command('write', book, '--sheet', 'Effects', '--data-file', cells)
    updated = command('read', book, '--sheet', 'Effects', '--format', 'json', '--raw')['rows']
    assert updated[:len(rows)] == rows, 'Existing rows changed'
    config = ROOT / 'Config/DefaultGame.ini'
    text = config.read_text(encoding='utf-8-sig')
    section = '[/Script/GuLiStrike.GuLiBuildingConstructionSettings]'
    start = text.index(section) + len(section)
    end = text.find('\n[', start)
    if end < 0:
        end = len(text)
    part = text[start:end]
    for key, name in [('CompleteVfxId','BuildingConstructionComplete'),
                      ('TopLoopVfxId','BuildingConstructionTopLoop'), ('LaserVfxId','BuildingConstructionLaser')]:
        line = f'{key}={ids[name]}'
        part = re.sub(r'^'+key+r'=.*$',line,part,flags=re.M) if re.search(r'^'+key+r'=',part,re.M) else part.rstrip()+'\n'+line+'\n'
    config.write_text(text[:start]+part+text[end:],encoding='utf-8')
    (OUT/'registered-ids.json').write_text(json.dumps(ids,indent=2),encoding='utf-8')
    print(json.dumps(ids))


if __name__ == '__main__':
    main()
