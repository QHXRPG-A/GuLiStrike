"""Lay out actual Blender evidence with labels; never paint over rendered geometry."""
import json
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1';S=O/'Sheets';S.mkdir(exist_ok=True);Q=O/'QA';Q.mkdir(exist_ok=True)
d=json.loads((O/'construction_report.json').read_text(encoding='utf8'));refs={a['key']:a for a in json.loads((R/'References_A_v7/render_manifest.json').read_text(encoding='utf8'))['assets']}
FONT='C:/Windows/Fonts/msyh.ttc';font=lambda n:ImageFont.truetype(FONT,n);bg='#F3EEE5';ink='#1A182F';muted='#797083'
labels={'AirBase':'空军基地','CloningCenter':'克隆中心','CommandCenter':'指挥中心','MilitaryFactory':'军工厂','Reactor':'反应堆','StrategyCenter':'战略中心','Floor':'平台','Lamp':'灯柱','Light':'灯片','Drone':'无人机'}
views={'Hero':'三分之四实际效果','Front':'正面','Left':'左侧','Back':'背面','Top':'俯视'}
def image(path,size):return Image.open(path).convert('RGB').resize(size,Image.Resampling.LANCZOS)
for a in d['assets']:
 key=a['key'];sheet=Image.new('RGB',(4200,4700),bg);draw=ImageDraw.Draw(sheet)
 draw.text((110,80),f'SSF 成品审核 B_v1 · {key} · {labels[key]}',font=font(74),fill=ink)
 draw.text((115,180),'实际 Blender 成品 · A_v7 配色与同机位 · 三渲二 / 独立线稿 / 骨骼描边',font=font(40),fill=muted)
 for idx,view in enumerate(('Hero','Front','Left','Back')):
  x=(idx%2)*2050+50;y=(idx//2)*2050+340
  sheet.paste(image(O/'Renders'/f'{key}_{view}.png',(2000,2000)),(x,y));draw.text((x+60,y+20),views[view],font=font(45),fill=ink)
 row=a['lods'][0];draw.text((115,4495),f'LOD0 本体 {row["body_triangles"]} + 描边 {row["outline_triangles"]} 三角面；实际区段 {row["actual_total_sections"]}；原比例与活动结构保留。',font=font(36),fill=ink)
 draw.text((115,4560),'本体预算差额另列；B 与预算例外尚待用户决定，正式 UE 导入尚未执行。',font=font(34),fill=muted)
 sheet.save(S/(key+'_Actual_Views.png'))
 # The reference is shown in full; the product receives the identical pose,
 # orthographic scale and camera from its native evidence manifest.
 comparison=Image.new('RGB',(4200,4520),bg);c=ImageDraw.Draw(comparison);c.text((100,60),f'{labels[key]} · A_v7 与 B_v1 同机位对照',font=font(65),fill=ink)
 for y,view in enumerate(('Hero','Front')):
  for x,(label,root) in enumerate((('已审 A_v7',R/'References_A_v7/Renders'),('实际成品 B_v1',O/'Renders'))):
   comparison.paste(image(root/f'{key}_{view}.png',(2000,2000)),(100+2050*x,300+2050*y));c.text((130+2050*x,230+2050*y),label+' · '+views[view],font=font(42),fill=ink)
 comparison.save(S/(key+'_Reference_Comparison.png'))
 lodsheet=Image.new('RGB',(4800,2060),bg);ld=ImageDraw.Draw(lodsheet);ld.text((70,50),f'{labels[key]} · 三档实际 LOD / 相同镜头和比例',font=font(66),fill=ink)
 for lod in range(3):
  e=a['lods'][lod];path=O/'Renders'/f'{key}_Hero{("_LOD"+str(lod)) if lod else ""}.png';lodsheet.paste(image(path,(1600,1600)),(1600*lod,230))
  ld.text((1600*lod+60,180),f'LOD{lod} · 本体 {e["body_triangles"]} + 描边 {e["outline_triangles"]}',font=font(35),fill=ink)
  delta=max(0,e['body_triangles']-e['body_cap']);ld.text((1600*lod+60,1880),f'本体上限 {e["body_cap"]}，差额 +{delta}；保留 {len(a["parts"])} 个制作分件',font=font(32),fill=muted)
 lodsheet.save(S/(key+'_LOD_Comparison.png'))
 if key in ('Floor','Drone'):
  image(O/'Renders'/f'{key}_Top.png',(2048,2048)).save(S/(key+'_Actual_Top.png'))

for name,keys in [('Buildings_1_AllViews',['AirBase','CloningCenter','CommandCenter']),('Buildings_2_AllViews',['MilitaryFactory','Reactor','StrategyCenter']),('Accessories_AllViews',['Floor','Lamp','Light','Drone'])]:
 canvas=Image.new('RGB',(2400,650*len(keys)),bg);draw=ImageDraw.Draw(canvas)
 for y,key in enumerate(keys):
  for x,view in enumerate(('Hero','Front','Left','Back')):
   canvas.paste(image(O/'Renders'/f'{key}_{view}.png',(600,600)),(600*x,650*y+40));draw.text((600*x+10,650*y+5),key+' / '+view,font=font(25),fill=ink)
 canvas.save(Q/(name+'.png'))

buildings=['AirBase','CloningCenter','CommandCenter','MilitaryFactory','Reactor','StrategyCenter']
overview=Image.new('RGB',(3000,2850),bg);draw=ImageDraw.Draw(overview);draw.text((65,55),'SSF 建筑实际成品 · 审核 B_v1',font=font(72),fill=ink);draw.text((68,140),'A_v7 配色 · 原比例 / 完整装配 / 三档明暗 / 实际线稿与描边',font=font(33),fill=muted)
for index,key in enumerate(buildings):
 x=(index%3)*1000;y=(index//3)*1190+280;overview.paste(image(O/'Renders'/f'{key}_Hero.png',(1000,1000)),(x,y));a=next(a for a in d['assets'] if a['key']==key);e=a['lods'][0]
 draw.text((x+55,y+1010),key+' · '+labels[key],font=font(37),fill=ink);draw.text((x+55,y+1080),f'LOD0 {e["body_triangles"]} + {e["outline_triangles"]} 三角面',font=font(28),fill=muted)
overview.save(O/'Overview_Buildings_B_v1.png')
print('SSF_NATIVE_EVIDENCE_BOARDS_OK',len(d['assets']),flush=True)
