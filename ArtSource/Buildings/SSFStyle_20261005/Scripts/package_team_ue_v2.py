"""Compose review boards solely from the actual UE screenshots."""
import json,hashlib
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');D=R/'UE_Delivery_Team_v2';V=D/'Preview';S=D/'Sheets';S.mkdir(exist_ok=True)
report=json.loads((D/'ue_validation.json').read_text(encoding='utf8'));assert report['success']
NAMES={'AirBase':'空军基地','CloningCenter':'克隆中心','CommandCenter':'指挥中心','MilitaryFactory':'军工厂','Reactor':'反应堆','StrategyCenter':'战略中心'}
KEYS=list(NAMES);BG=(240,238,232);INK=(30,28,45)
title=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',42);font=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',23)
def board(path,label,entries,cols=3,cell=800):
    rows=(len(entries)+cols-1)//cols;canvas=Image.new('RGB',(cell*cols,90+rows*(cell+50)),BG);draw=ImageDraw.Draw(canvas)
    draw.text((25,20),label,font=title,fill=INK)
    for i,(name,caption) in enumerate(entries):
        image=Image.open(V/(name+'.png')).convert('RGB');image.thumbnail((cell,cell),Image.Resampling.LANCZOS)
        x=(i%cols)*cell;y=90+(i//cols)*(cell+50);canvas.paste(image,(x+(cell-image.width)//2,y+(cell-image.height)//2))
        draw.text((x+14,y+cell+8),caption,font=font,fill=INK)
    canvas.save(S/path);print(path,flush=True)
for team,label in [('Blue','蓝方'),('Red','红方')]:
    board(team+'_UE_Overview.png',label+' / 正式 UE B_v2 / 实际资源截图',[(team+'_'+key+'_LOD0',NAMES[key]+' · '+key) for key in KEYS])
    for p in range(2):
        keys=KEYS[p*3:p*3+3]
        board(f'QA_{team}_UE_LODs_{p+1}.png',label+' / 实际三档LOD',[(team+'_'+key+'_LOD'+str(lod),key+' · LOD'+str(lod)) for key in keys for lod in range(3)])
    board('QA_'+team+'_UE_Near.png',label+' / 35m · 25° / 自动LOD',[(team+'_'+key+'_Near_35m',NAMES[key]+' · 35m / 25°') for key in KEYS])
    board('QA_'+team+'_UE_Distances.png',label+' / 战术与总览 / 自动LOD',[(team+'_'+key+'_'+label2,key+' · '+label2) for key in KEYS for label2 in ['Tactical_300m','Tactical_700m','Overview_1500m']],cell=550)
blue=Image.open(S/'Blue_UE_Overview.png');red=Image.open(S/'Red_UE_Overview.png')
blue.thumbnail((1800,1600),Image.Resampling.LANCZOS);red.thumbnail((1800,1600),Image.Resampling.LANCZOS)
pair=Image.new('RGB',(blue.width+red.width,max(blue.height,red.height)),BG);pair.paste(blue,(0,0));pair.paste(red,(blue.width,0));pair.save(S/'Blue_Red_UE_Overview.png')
inventory=[]
for capture in report['captures']:
    p=Path(capture['path']);im=Image.open(p);assert list(im.size)==[2048,2048]
    inventory.append({'file':'Preview/'+p.name,'dimensions':list(im.size),'sha256':hashlib.sha256(p.read_bytes()).hexdigest(),
        'requested_LOD':capture['LOD'],'actual_component_LOD':capture['component_predicted_LOD'],'distance_cm':capture['distance_cm'],'fov_degrees':capture['fov']})
(D/'preview_inventory.json').write_text(json.dumps({'success':True,'native_UE_capture_count':len(inventory),'captures':inventory,
    'combined':'Sheets/Blue_Red_UE_Overview.png','source':'actual independently reloaded UE assets, no AI imagery'},ensure_ascii=False,indent=2),encoding='utf8')
print('SSF_TEAM_UE_BOARDS_PACKAGED',len(inventory),flush=True)
