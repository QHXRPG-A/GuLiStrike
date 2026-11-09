"""Author Ground HUD presentation data through excelize; does not start UE/build/PIE."""
import json
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
XLSX = r'D:/UE5.7/excelize-cli/bin/xlsx.exe'
PREFIX = 'UI.GroundHUD.'
TEXTS = {
    'Title':'游骑兵', 'Health':'生命值', 'Jet':'喷气容量',
    'Fire':'射击', 'Aim':'瞄准', 'Jump':'喷气跳跃', 'Blink':'闪现',
    'Ready':'就绪', 'Empty':'燃料不足', 'Active':'使用中', 'Disabled':'不可用',
    'Stats':'武器等级 {0}   射速 {1}\n移速 {2}',
    'Details':'属性与强化', 'DetailsHint':'[I] 详情', 'RogueHint':'[F4] 选牌',
    'Owned':'已获强化', 'NoOwned':'尚未获得强化', 'MapHint':'M  战术地图',
    'MapTitle':'战术地图', 'MapHelp':'滚轮缩放 · 左键拖动 / 选择标记 · Space 定位自身 · M / Esc 关闭',
    'MapUnavailable':'地形数据暂不可用', 'MapLegend':'▲ 自身   ▪ 友方   ◆ 敌方   □ 据点',
    'MapLocation':'坐标 {0}, {1} m', 'MapSelected':'{0}\n坐标 {1}, {2} m',
    'Friendly':'友方单位', 'Enemy':'敌方单位', 'Territory':'据点', 'Self':'游骑兵',
    'Close':'[Esc] 关闭', 'SelectDirect':'单击卡牌，直接获得强化', 'Burning':'灼烧',
    'Buffs':'当前状态', 'BurningBurst':'灼烧爆裂', 'BuffOther':'状态 {0}', 'BuffMore':'+{0}',
    'Shield':'护盾 {0} / {1}', 'Dead':'机体失能', 'Damage':'单发伤害 {0}',
    'ModelShort':'A4B2', 'RogueAvailable':'强化选择', 'RicochetShort':'折射', 'BurstShort':'爆裂',
    'LocalTerrain':'局部地形', 'MapHintShort':'[M] 地图', 'Attributes':'机体属性',
    'WeaponLevel':'武器等级', 'FireRate':'射速', 'MoveSpeed':'移速',
    'LocationInfo':'地点信息', 'MapPickLocation':'选择地图标记', 'Distance':'距离',
    'Coordinates':'坐标', 'Affiliation':'状态', 'NoSelection':'—', 'LegendTitle':'图例',
    'SelfLegend':'自身', 'FriendlyLegend':'友方', 'EnemyLegend':'敌方',
    'MapWheelHint':'滚轮  缩放', 'LocateSelf':'Space  定位自身', 'GroundIdentity':'GROUND / 4001',
    'Brand':'GULISTRIKE', 'SystemIdentity':'GROUND SYSTEM', 'SelectTitle':'选择强化', 'SelectOne':'选择 1 项强化',
}

def run(*args):
    return subprocess.check_output([XLSX, *map(str,args)],text=True,encoding='utf-8')

def cells_write(path, sheet, cells):
    with tempfile.TemporaryDirectory(prefix='ground-hud-') as tmp:
        p=Path(tmp)/'cells.json';p.write_text(json.dumps(cells,ensure_ascii=False),encoding='utf-8')
        run('write',path,'--sheet',sheet,'--data-file',p)

def main():
    path=ROOT/'data/Excel/GuLiStrikeGroundHUD.xlsx'
    if not path.exists():run('new',path,'--sheet','Elements')
    names=['id','name','Note','Texture','TextId','Sprite','Value','ColorSRGB']
    types=['int','str','str','softobject','str','int','float','str']
    rows=[names,types,['Necessary','Necessary','Optional','Optional','Optional','Optional','Optional','Optional']]
    for i,name in enumerate(['Fire','Aim','Jump','Blink','Health','Speed','Burning','Ricochet']):
        rows.append([i+1,name,'参考图线条符号，由原生Slate缓存绘制','',PREFIX+name if name not in ['Speed','Ricochet'] else '',-1,0,''])
    for name,value in [('Panel','#021716'),('PanelRaised','#032E30'),('Line','#0D9099'),('Secondary','#6AA4BE'),('Accent','#EE9D58'),('Text','#FEE4D9')]:
        rows.append([len(rows)-2,name,'项目统一色库','', '',-1,0,value])
    for name,value in [('RefreshSeconds',.2),('MapRange',45000),('ContourStep',500),('MapHeightScale',4),('SmallFont',18),('BodyFont',22),('HeadingFont',28),('CardTimeAnimation',1),('CardTitleFont',90),('CardHeight',.68),('CardSpacing',.335)]:
        rows.append([len(rows)-2,name,'表现参数','', '',-1,value,''])
    for name,sprite in [('GroundMech_Ricochet_Lv1',7),('GroundMech_BurningBullet_Lv1',6),('GroundMech_BurningBurst_Lv1',6)]:
        rows.append([len(rows)-2,name,'已获强化摘要线条符号，由原生Slate缓存绘制','','',-1,0,''])
    rows.append([len(rows)-2,'UIFont','Ground 中文字体 Medium/Bold；SIL OFL 1.1','/Game/GuLiStrike/GroundMech/UI/Fonts/F_GroundHUD.F_GroundHUD','',-1,0,''])
    for name,suffix in [
        ('CockpitShell','HUD_SPECIAL_GADGETS/Cockpit/T_HUD_Cockpit_2'),
        ('InstrumentGrid','Elements_Main/Grid/T_HUD_Grid_01'),
        ('EnergyRail','Basic_Elements/Progress_Bars/Energy/T_HUD_Energy_bar_1'),
        ('JetRail','Basic_Elements/Progress_Bars/Progress_bar_Vertical/T_HUD_Progress_Bar_Vertical_01')]:
        asset='/Game/Assets/UI/HUDuiPRO/Textures/Others/'+suffix
        rows.append([len(rows)-2,name,'HUDuiPRO 静态组件；实时数值独立绘制',asset+'.'+suffix.rsplit('/',1)[1],'',-1,0,''])
    # Update named rows in place. Existing IDs and unrelated authoring rows survive reruns.
    old=json.loads(run('read',path,'--sheet','Elements','--format','json'))['rows']
    indices={r[1]:i+1 for i,r in enumerate(old) if i>=3 and len(r)>1}
    max_id=max([int(r[0]) for r in old[3:] if r and r[0]]+[0]);tail=max(4,len(old)+1);cells=[]
    if len(old)<3:
        cells.extend({'cell':f'{chr(65+c)}{r+1}','value':v,'type':'string'} for r,row in enumerate(rows[:3]) for c,v in enumerate(row))
    for row in rows[3:]:
        number=indices.get(row[1])
        if number is None:number=tail;tail+=1;max_id+=1;row[0]=max_id
        else:row[0]=int(old[number-1][0])
        cells.extend({'cell':f'{chr(65+c)}{number}','value':v,'type':types[c] if types[c] in ['int','float'] else 'string'} for c,v in enumerate(row))
    cells_write(path,'Elements',cells)
    textpath=ROOT/'data/Excel/GuLiStrikeGameTexts.xlsx'
    old=json.loads(run('read',textpath,'--sheet','Texts','--format','json'))['rows']
    indices={r[0]:i+1 for i,r in enumerate(old) if r}
    tail=len(old)+1;changes=[]
    for key,value in TEXTS.items():
        full=PREFIX+key;row=indices.get(full)
        if row is None:row=tail;tail+=1
        for col,val in zip('ABC',[full,'Ground HUD 正式界面文案',value]):changes.append({'cell':f'{col}{row}','value':val,'type':'string'})
    cells_write(textpath,'Texts',changes)
    print(json.dumps({'workbook':str(path),'elements':len(rows)-3,'texts':len(TEXTS)},ensure_ascii=True))

if __name__=='__main__':main()
