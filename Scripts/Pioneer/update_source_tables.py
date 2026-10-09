"""Feature-scoped source edits through excelize-cli, then the existing exporter."""
import json
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
if (ROOT/'Data/Excel/GuLiStrikeModels.xlsx').exists():
    raise RuntimeError('Frozen Pioneer producer predates ModelId. Maintain Soldiers gameplay fields and Models resource bindings separately; this script cannot recreate path columns.')
CLI=Path('D:/UE5.7/excelize-cli/bin/xlsx.exe')
OUT=ROOT/'ArtSource/Mechs/RSGMechStyle_20261003/Delivery_UE_v1/Reports'
OUT.mkdir(parents=True,exist_ok=True)
VAT=json.loads((OUT.parent/'vat_metadata.json').read_text(encoding='utf8'))
report=[]

def col(index):
    value=''
    while index:
        index,remainder=divmod(index-1,26); value=chr(65+remainder)+value
    return value

def edit(book,sheet,transform):
    path=ROOT/'Data/Excel'/book
    raw=subprocess.check_output([str(CLI),'read',str(path),'--sheet',sheet,'--raw','--format','json'],text=True,encoding='utf8')
    rows=json.loads(raw)['rows']
    before=[list(row) for row in rows]
    transform(rows)
    cells=[]
    for r,row in enumerate(rows):
        for c,value in enumerate(row):
            old=before[r][c] if r<len(before) and c<len(before[r]) else ''
            if str(value)!=str(old):
                kind='string' if r<3 or rows[1][c] in ['str','softobject','softclass'] else 'bool' if rows[1][c]=='bool' else 'int' if rows[1][c]=='int' else 'float'
                cells.append({'cell':f'{col(c+1)}{r+1}','value':value,'type':kind})
    if cells:
        data=OUT/f'cells_{Path(book).stem}_{sheet}.json'
        data.write_text(json.dumps(cells,ensure_ascii=False,indent=2),encoding='utf8')
        subprocess.run([str(CLI),'write',str(path),'--sheet',sheet,'--data-file',str(data)],check=True)
    report.append({'workbook':book,'sheet':sheet,'changed_cells':len(cells),'rows':len(rows)-3})

def add_columns(rows,columns):
    for name,kind in columns:
        if name not in rows[0]:
            rows[0].append(name);rows[1].append(kind);rows[2].append('Optional')
    for row in rows:
        row.extend(['']*(len(rows[0])-len(row)))

def update(rows,row,values):
    for key,value in values.items(): row[rows[0].index(key)]=value

def append(rows,values):
    row=['']*len(rows[0]);update(rows,row,values);rows.append(row)

def soldiers(rows):
    add_columns(rows,[('VATDefinition','softobject'),('bSummonOnly','bool')])
    base=next(r for r in rows[3:] if r[1]=='DefaultSoldier')
    sweeper=next((r for r in rows[3:] if r[1]=='SweeperSummon'),None)
    if not sweeper:
        sweeper=list(base);rows.append(sweeper)
    update(rows,sweeper,dict(id=5,name='SweeperSummon',Note='先驱号Q专用召唤兵；永久可控、可累积、不计生产人口',
        MovementSpeedCmPerSecond=720,MaxHealth=100,Defense=0,ModelAsset='/Game/Commander/Units/Tactical/Cel/Sweeper/Meshes/SM_Sweeper_Rigid.SM_Sweeper_Rigid',
        PresentationScale=.2,DisplayName='扫荡者',ModelWidthMeters=3.1,MinAvoidanceDistanceMeters=.5,VATDefinition='',bSummonOnly=True))
    update(rows,base,dict(Note='B-v1先驱号；6.25米；刚性骨骼纹理VAT；默认双枪与Q召唤',MovementSpeedCmPerSecond=1440,
        ModelAsset='/Game/GuLiStrike/Robots/RSGMech/Meshes/SM_Pioneer_VAT.SM_Pioneer_VAT',PresentationScale=1,
        DisplayName='先驱号',ModelWidthMeters=6.25,MinAvoidanceDistanceMeters=.5,
        VATDefinition='/Game/GuLiStrike/Robots/RSGMech/VAT/DA_Pioneer_VAT.DA_Pioneer_VAT',bSummonOnly=False))
    for r in rows[3:]:
        if r[1]!='SweeperSummon': r[rows[0].index('bSummonOnly')]=False

def attacks(rows):
    add_columns(rows,[('ProjectileCount','int')])
    base=next(r for r in rows[3:] if r[1]=='SoldierA_Strafe')
    if not any(r[1]=='SweeperSummon_Strafe' for r in rows[3:]):
        copy=list(base);rows.append(copy)
        update(rows,copy,dict(id=max(int(r[0]) for r in rows[3:])+1,name='SweeperSummon_Strafe',Note='召唤扫荡者原单枪数值',UnitTypeId=5,
            RangeCentimeters=2000,Damage=10,AttackRatePerSecond=1,ProjectileCount=1,ProjectileSpreadAngleDegrees=2))
    for r in rows[3:]:
        if r[rows[0].index('ProjectileCount')]=='': r[rows[0].index('ProjectileCount')]=1
        if r[rows[0].index('ProjectileSpreadAngleDegrees')]=='10':r[rows[0].index('ProjectileSpreadAngleDegrees')]=2
    update(rows,base,dict(Note='先驱号左右主枪同时发射；每枪1发/秒，每发10伤害，总锥角2度',RangeCentimeters=6000,ProjectileCount=2))

def mounts(rows):
    old=[list(r) for r in rows[3:] if r[3]=='1']
    for r in old if not any(x[3]=='5' for x in rows[3:]) else []:
        if not any(x[1]=='SweeperSummon_'+r[1] for x in rows[3:]):
            update(rows,r,dict(id=max(int(x[0]) for x in rows[3:])+1,name='SweeperSummon_'+r[1],UnitTypeId=5))
            rows.append(r)
    aim=next(r for r in rows[3:] if r[3]=='1' and r[5]=='AimTarget')
    bounds=VAT['gameplay_bounds_cm'];center=[(a+b)/2 for a,b in zip(bounds['min'],bounds['max'])]
    update(rows,aim,dict(name='Pioneer_AimTarget',Note='先驱号真实游戏包围盒中心',OffsetX=center[0],OffsetY=center[1],OffsetZ=center[2]))
    main=next(r for r in rows[3:] if r[3]=='1' and r[5]=='Muzzle')
    for i,m in enumerate(VAT['muzzles']):
        r=main if i==0 else next((x for x in rows[3:] if x[1]=='Pioneer_BasicAttack_Muzzle_1'),None)
        if r is None:
            r=list(main); rows.append(r);update(rows,r,dict(id=max(int(x[0]) for x in rows[3:])+1))
        p=m['reference_position_cm']
        update(rows,r,dict(name=f'Pioneer_BasicAttack_Muzzle_{i}',Note=f'VAT与CPU共享的{m["bone_name"]}枪口',PointIndex=i,
            SocketName=f'FX_Muzzle_Basic_0{i+1}',OffsetX=p[0],OffsetY=p[1],OffsetZ=p[2]))

def active_skills(rows):
    columns=[('SummonUnitTypeId','int'),('SummonCount','int'),('SummonClearanceCentimeters','float'),('SummonOuterRings','int'),('SummonCandidatesPerRing','int')]
    add_columns(rows,columns)
    r=next((x for x in rows[3:] if x[1]=='Pioneer_Summon'),None)
    if r is None:
        r=['']*len(rows[0]); rows.append(r);update(rows,r,dict(id=max(int(x[0] or 0) for x in rows[3:])+1))
    update(rows,r,dict(name='Pioneer_Summon',Note='Q立即召唤五个扫荡者；每台独立30秒；整批5或0；免费且不计人口',TargetMode='Self',CooldownSeconds=30,
        RangeCentimeters=0,RangeSourceSlot='',RangeMultiplier=1,MaximumLevel=1,
        ExecutorClass='/Script/GuLiStrike.GuLiSummonSkillExecutor',ConfigurationClass='/Script/GuLiStrike.GuLiSummonSkillConfiguration',
        Configuration='/Game/GuLiStrike/Commander/Skills/DA_Pioneer_Summon',SummonUnitTypeId=5,SummonCount=5,
        SummonClearanceCentimeters=50,SummonOuterRings=3,SummonCandidatesPerRing=36))

def active_mapping(rows):
    r=next(x for x in rows[3:] if x[1]=='DefaultSoldier')
    r.extend(['']*(len(rows[0])-len(r)))
    update(rows,r,dict(Note='先驱号默认解锁Q召唤',SkillId='Pioneer_Summon'))
    if not any(x[1]=='SweeperSummon' for x in rows[3:]):append(rows,dict(id=5,name='SweeperSummon',Note='召唤扫荡者无主动Q',UnitTypeId=5,SkillId=''))

edit('GuLiStrikeCommander.xlsx','Soldiers',soldiers)
edit('GuLiStrikeSecondaryWeapons.xlsx','UnitSkills',attacks)
edit('GuLiStrikeSecondaryWeapons.xlsx','WeaponMounts',mounts)
edit('GuLiStrikeSecondaryUnitSkills.xlsx','Skills',active_skills)
edit('GuLiStrikeSecondaryUnitSkills.xlsx','UnitSkills',active_mapping)
def text_rows(rows):
    name='UI.CommanderSkills.PioneerSummon'
    content='\n先驱号：按Q立即在周围召唤5个扫荡者，每台独立冷却30秒，无资源消耗。召唤兵永久可控、可累积、不占生产人口；空间或容量不足时整批失败，冷却保持不变。'
    r=next((x for x in rows[3:] if x[0]==name),None)
    if r is None:rows.append([name,'先驱号Q召唤提示',content])
    else:r[:]=[name,'先驱号Q召唤提示',content]
edit('GuLiStrikeGameTexts.xlsx','Texts',text_rows)
subprocess.run([sys.executable,'-X','utf8',str(ROOT/'Tools/DataPipeline/export_data_from_excel.py')],cwd=ROOT,check=True)
(OUT/'source_tables.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps(report,ensure_ascii=False))
