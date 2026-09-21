"""One-time migration from the captured pre-ID baseline. Uses excelize-cli for Excel I/O."""
import json
import re
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'outputs/vfx-registry-20260921'
XLSX = r'D:\UE5.7\excelize-cli\bin\xlsx.exe'
if (ROOT/'Data/Excel/GuLiStrikeVfx.xlsx').exists():
    raise RuntimeError('Registry already exists; edit it directly without renumbering IDs.')

def cli(*args):
    return subprocess.check_output([XLSX, *map(str,args)], encoding='utf-8')

def col(index):
    result=''
    while index:
        index, rem=divmod(index-1,26); result=chr(65+rem)+result
    return result

definitions=[]
uses=[]
def register(name, resource, scale, location, note=''):
    scale=[round(float(x),6) for x in (scale if isinstance(scale,list) else [scale]*3)]
    existing=next((r for r in definitions if r['ResourcePath']==resource and [r['ScaleX'],r['ScaleY'],r['ScaleZ']]==scale),None)
    if existing is None:
        existing=dict(id=len(definitions)+1,name=name,Note=note or name,ResourcePath=resource,
                      ScaleX=scale[0],ScaleY=scale[1],ScaleZ=scale[2])
        definitions.append(existing)
    elif note and note not in existing['Note']:
        existing['Note']+='；'+note
    uses.append(dict(location=location,resource=resource,scale=scale,vfx_id=existing['id'],name=existing['name']))
    return existing['id']

names={'M_GroundWarning_Circle':'GroundWarning','NS_WM01_MissileFlight':'MissileFlight',
       'NS_CommanderGunfireBatch':'CommanderGunfire','NS_WingmanLaserPool':'MachineGunTracer',
       'NS_Flash_1':'MachineGunImpact','NS_WM01_Explosion_2A':'CommanderMissileExplosion',
       'NS_WingmanBombardment_01':'WingmanBombardment'}
notes={'MachineGunImpact':'指挥官、僚机、玩家地面机甲机枪命中单位或地形',
       'MissileFlight':'指挥官与僚机导弹飞行', 'MachineGunTracer':'指挥官、僚机机枪弹道和僚机枪口'}
baseline=json.loads((OUT/'asset-baseline.json').read_text(encoding='utf-8'))
for entry in baseline['entries']:
    name=names[entry['resource'].rsplit('.',1)[-1]]
    register(name,entry['resource'],entry['scale'],entry['location'],notes.get(name,''))

tables=[('GuLiStrikeMech','技能表','DT_GuLiStrikeMech_Skills',
        {'BulletSystem':('BulletVfxId','PlayerBullet'), 'MuzzleSystem':('MuzzleVfxId','PlayerMuzzle'),
         'JetSystem':('JetVfxId','RocketJet'), 'FuelBarMaterial':('FuelBarVfxId','RocketFuelBar')}),
        ('GuLiStrikeSpellFields','Fields','DT_GuLiStrikeSpellFields_Fields',
        {'EnergyMaterial':('EnergyVfxId','TransitEnergy'), 'TrailSystem':('TrailVfxId','TransitTrail'),
         'FlashSystem':('FlashVfxId','TransitFlash')})]
for workbook,sheet,table,fields in tables:
    rows=json.loads((ROOT/'Data/Json'/f'{table}.json').read_text(encoding='utf-8'))
    grid=json.loads(cli('read',ROOT/'Data/Excel'/f'{workbook}.xlsx','--sheet',sheet,'--format','json','--raw'))['rows']
    # excelize emits an array of rows.
    cells=[]
    for old,(new,name) in fields.items():
        column=grid[0].index(old)+1
        cells.extend([dict(cell=col(column)+'1',value=new,type='string'),dict(cell=col(column)+'2',value='int',type='string')])
        for i,row in enumerate(rows,4):
            resource=row[old]
            scale=row.get('JetScale',1) if old=='JetSystem' else 1
            id=register(name,resource,scale,table+'.'+row['Name']+'.'+old,row['Note']) if resource else 0
            cells.append(dict(cell=col(column)+str(i),value=id,type='int'))
    cell_path=OUT/(workbook+'-cells.json');cell_path.write_text(json.dumps(cells,ensure_ascii=False),encoding='utf-8')
    cli('write',ROOT/'Data/Excel'/f'{workbook}.xlsx','--sheet',sheet,'--data-file',cell_path)
    if 'JetScale' in grid[0]:
        cli('delete-cols',ROOT/'Data/Excel'/f'{workbook}.xlsx','--sheet',sheet,'--col',col(grid[0].index('JetScale')+1),'--count','1')

config=(ROOT/'Config/DefaultGame.ini').read_text(encoding='utf-8')
ini_names={'OutlineMaterial':'TeamOutline','FlightTrailSystem':'WingmanFlightTrail','WreckMaterial':'UnitWreck',
           'HitMaterial':'UnitHit','InstancedHitMaterial':'UnitHitInstanced','+Explosions':'GroundDestruction','+WingmanExplosions':'WingmanDestruction'}
for line in config.splitlines():
    key,_,value=line.partition('=')
    if key in ini_names:
        register(ini_names[key],value,7.552620 if 'Explosions' in key else 1,'Config/DefaultGame.ini:'+key)

for name,resource,scale,location,note in [
 ('UnitHitHealthBar','/Game/GuLiStrike/FX/UnitFeedback/M_UnitHitHealthBarWorld.M_UnitHitHealthBarWorld',1,'GuLiCommanderHealthBarRenderer','受击世界血条'),
 ('BlinkAfterimage','/Game/GuLiStrike/FX/M_BlinkAfterimage.M_BlinkAfterimage',1,'BlinkVFX.Afterimage','闪现残影材质'),
 ('BlinkHeatwave','/Game/GuLiStrike/FX/M_BlinkHeatwave.M_BlinkHeatwave',1,'BlinkVFX.Heatwave','闪现热波材质'),
 ('BlinkSphere','/Engine/BasicShapes/Sphere.Sphere',.48,'BlinkVFX.HeatwaveMesh','闪现热波网格，时间曲线保留为动态系数'),
 ('TeleportCylinder','/Game/GuLiStrike/FX/CommanderTeleport/SM_TeleportCylinder.SM_TeleportCylinder',1,'GuLiTeleportFieldPresentation.Beam','传送光柱网格'),
 ('TeleportGround','/Game/GuLiStrike/FX/CommanderTeleport/M_TeleportGround.M_TeleportGround',1,'GuLiTeleportFieldPresentation.Ground','传送范围与进度'),
 ('TeleportBeam','/Game/GuLiStrike/FX/CommanderTeleport/M_TeleportBeam.M_TeleportBeam',1,'GuLiTeleportFieldPresentation.Beam','传送光柱材质'),
 ('TeleportBody','/Game/GuLiStrike/FX/CommanderTeleport/M_TeleportBody.M_TeleportBody',1,'GuLiTeleportFieldActor;GuLiWingmanPawn;GuLiCommanderPresentationActor','传送单位相位覆盖材质'),
 ('UnitRingMesh','/Game/Commander/Units/SM_CommanderUnitRing.SM_CommanderUnitRing',[2.4,2.4,.004],'GuLiCommanderPresentationActor.Ring','指挥官单位选择圈'),
 ('UnitRingMaterial','/Game/Commander/UI/M_CommanderUnitRing.M_CommanderUnitRing',1,'GuLiCommanderPresentationActor.Ring','单位选择圈材质'),
 ('TransitOrb','/Engine/BasicShapes/Sphere.Sphere',10,'GuLiStrongholdTransitPresentationComponent.Orb','运输能量球'),
 ('TransitNode','/Engine/BasicShapes/Sphere.Sphere',1.2,'GuLiStrongholdNetworkPresentationComponent.Node','运输网络节点'),
 ('TransitEdge','/Engine/BasicShapes/Cylinder.Cylinder',[.16,.16,1],'GuLiStrongholdNetworkPresentationComponent.Edge','运输网络连线，长度动态计算'),
 ('FuelBarPlane','/Engine/BasicShapes/Plane.Plane',1,'GuLiGroundMechRocketComponent.FuelBar','燃料条承载网格'),
 ('MiningLaser','/Game/GuLiStrike/FX/Mining/NS_MiningLaser_Green.NS_MiningLaser_Green',1,'BP_MiningVehicle_TransporterLvl2.MiningLaser_L/R;BP_ConstructionVehicle.MiningLaser_L/R','采矿与建造激光')]:
    register(name,resource,scale,location,note)

workbook=ROOT/'Data/Excel/GuLiStrikeVfx.xlsx'
if workbook.exists():
    raise RuntimeError('Registry source already exists; do not renumber IDs. Edit the workbook directly.')
cli('new',workbook,'--sheet','Effects')
headers=['id','name','Note','ResourcePath','ScaleX','ScaleY','ScaleZ']
types=['int','str','str','softobject','float','float','float']
grid=[headers,types,['Necessary','Necessary','Optional','Necessary','Necessary','Necessary','Necessary']]+[[r[k] for k in headers] for r in definitions]
cells=[dict(cell=col(c)+str(r),value=value,type='int' if isinstance(value,int) else 'float' if isinstance(value,float) else 'string') for r,row in enumerate(grid,1) for c,value in enumerate(row,1)]
p=OUT/'registry-cells.json';p.write_text(json.dumps(cells,ensure_ascii=False),encoding='utf-8')
cli('write',workbook,'--sheet','Effects','--data-file',p)
cli('style',workbook,'--sheet','Effects','--range','A1:G3','--bold','--bg','D9E1F2','--border')
cli('col-width',workbook,'--sheet','Effects','--col','B','--width','30')
cli('col-width',workbook,'--sheet','Effects','--col','C','--to','D','--width','65')
(ROOT/'Scripts/Vfx/migration-manifest.json').write_text(json.dumps({'definitions':definitions,'uses':uses},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'definitions':len(definitions),'uses':len(uses)}))
