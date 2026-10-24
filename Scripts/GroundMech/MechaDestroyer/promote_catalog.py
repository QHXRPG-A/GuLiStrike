"""Maintain Ground's stable model ID through the existing excelize source pipeline."""
from pathlib import Path
import json,sys,hashlib,tempfile
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'Scripts'))
from Models.migrate_model_workbooks import cli,read,letter
BOOK=ROOT/'Data/Excel/GuLiStrikeModels.xlsx'
ART=ROOT/'ArtSource/Characters/GroundMech_MechaDestroyer_20261023'
record=ART/'catalog_change.json'
if record.exists():
    raise RuntimeError('Catalogue already promoted; inspect readback instead of overwriting history')
before={s:read(BOOK,s) for s in ['Models','Parts']}
digest=hashlib.sha256(BOOK.read_bytes()).hexdigest()
updates = {
    'Models': {4001: {
        'ResourcePath': '/Game/GuLiStrike/GroundMech/MechaDestroyer_v1/Meshes/SK_MechaDestroyer.SK_MechaDestroyer',
        'DisplayName': '玩家地面机甲 MechaDestroyer',
        'Note': '2026-10-23 原模型先接通；三渲二与阵营色待设计和成品审核',
        'Description': 'Ground玩家整机骨骼，保留原控制/移动/建造/火箭跳/升级/网络玩法；左右枪交替，原总射速和伤害保持。',
    }},
    'Parts': {4: {
        'PartKey': 'Body',
        'Note': 'MechaDestroyer整机骨骼与双枪喷口；旧分件模型ID4002-4004保留供回溯',
    }},
}
changes=[]
assert hashlib.sha256(BOOK.read_bytes()).hexdigest()==digest
for sheet in ['Models','Parts']:
    rows=read(BOOK,sheet);cells=[]
    for index,row in enumerate(rows[3:],4):
        if not row or not row[0] or int(row[0]) not in updates[sheet]:continue
        for field,value in updates[sheet][int(row[0])].items():
            col=rows[0].index(field)
            cells.append({'cell':f'{letter(col+1)}{index}','value':value,'type':'string'})
            changes.append({'sheet':sheet,'id':int(row[0]),'field':field,'before':row[col] if col<len(row) else '', 'after':value})
    with tempfile.TemporaryDirectory(prefix='mecha-catalog-') as tmp:
        p=Path(tmp)/'cells.json';p.write_text(json.dumps(cells,ensure_ascii=False),encoding='utf8');cli('write',BOOK,'--sheet',sheet,'--data-file',p)
parts=read(BOOK,'Parts')
removed=[{'row':i,'data':r} for i,r in enumerate(parts,1) if i>3 and r and r[0] and int(r[0]) in [1,2,3]]
for r in reversed(removed):cli('delete-rows',BOOK,'--sheet','Parts','--at',r['row'],'--count',1)
after={s:read(BOOK,s) for s in ['Models','Parts']}
for sheet,old in before.items():
    assert old[:3]==after[sheet][:3]
    oldmap={r[0]:r for r in old[3:] if r and r[0]};newmap={r[0]:r for r in after[sheet][3:] if r and r[0]}
    for rid,row in newmap.items():
        if int(rid) not in updates[sheet]:assert row==oldmap[rid],(sheet,rid)
report={'success':True,'source_sha256_before':digest,'changes':changes,'removed_obsolete_assembly_rows':removed,'unchanged_unrelated_rows':True,'full_before':before}
record.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'success':True,'modified_cells':len(changes),'removed_assembly_parts':len(removed)}))
