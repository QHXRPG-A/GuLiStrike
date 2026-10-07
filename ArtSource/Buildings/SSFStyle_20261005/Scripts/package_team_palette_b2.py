"""Readable review pages composed only from actual Blender model renders."""
import json,hashlib
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'TeamPalette_B_v2_20261007'
M=json.loads((O/'candidate_manifest.json').read_text(encoding='utf8'));S=O/'Sheets';S.mkdir(exist_ok=True)
NAMES={'AirBase':'空军基地','CloningCenter':'克隆中心','CommandCenter':'指挥中心','MilitaryFactory':'军工厂','Reactor':'反应堆','StrategyCenter':'战略中心'}
KEYS=list(NAMES);BG=(245,239,228);INK=(30,28,45)
FONT='C:/Windows/Fonts/msyh.ttc';f=ImageFont.truetype(FONT,30);title=ImageFont.truetype(FONT,48);small=ImageFont.truetype(FONT,23)
def native(name,size):
    im=Image.open(O/'Renders'/(name+'.png')).convert('RGBA');background=Image.new('RGBA',im.size,(*BG,255));background.alpha_composite(im)
    im=background.convert('RGB');im.thumbnail(size,Image.Resampling.LANCZOS);return im
def board(file,label,entries,cols=3,cell=900,imgheight=900):
    rows=(len(entries)+cols-1)//cols;can=Image.new('RGB',(cols*cell,110+rows*(imgheight+95)),BG);draw=ImageDraw.Draw(can)
    draw.text((30,24),label,font=title,fill=INK)
    for i,(name,text,palette) in enumerate(entries):
        x=(i%cols)*cell;y=110+(i//cols)*(imgheight+95);im=native(name,(cell,imgheight));can.paste(im,(x+(cell-im.width)//2,y+(imgheight-im.height)//2))
        draw.text((x+24,y+imgheight+8),text,font=f,fill=INK)
        for j,code in enumerate(dict.fromkeys(palette)):
            draw.rounded_rectangle((x+24+j*180,y+imgheight+52,x+51+j*180,y+imgheight+79),radius=5,fill=code)
            draw.text((x+61+j*180,y+imgheight+51),code,font=small,fill=INK)
    can.save(S/file);return 'Sheets/'+file
overview=[]
for team,label in [('Blue','蓝方 · 图1色系'),('Red','红方 · 图2色系')]:
    overview.append(board(team+'_Buildings_Overview.png',label+' / 实际 Blender B_v2',[(team+'_'+k+'_Hero',NAMES[k]+' · '+k,list(M['role_mapping'][team][k].values())) for k in KEYS]))
    for key in KEYS:
        views=['Hero','Front','Left','Back']
        if all((O/'Renders'/(team+'_'+key+'_'+v+'.png')).exists() for v in views):
            board(team+'_'+key+'_Views.png',label+' · '+NAMES[key]+' / 视图一致',[(team+'_'+key+'_'+v,{'Hero':'效果图','Front':'正面','Left':'左侧','Back':'背面'}[v],[]) for v in views],cols=2,cell=900,imgheight=900)
        if all((O/'Renders'/(team+'_'+key+'_Hero'+(('_LOD'+str(i)) if i else '')+'.png')).exists() for i in range(3)):
            board(team+'_'+key+'_LODs.png',label+' · '+NAMES[key]+' / 三档LOD',[(team+'_'+key+'_Hero'+(('_LOD'+str(i)) if i else ''),'LOD'+str(i),[]) for i in range(3)],cols=3,cell=700,imgheight=700)
blue=Image.open(S/'Blue_Buildings_Overview.png');red=Image.open(S/'Red_Buildings_Overview.png')
blue.thumbnail((1800,1600),Image.Resampling.LANCZOS);red.thumbnail((1800,1600),Image.Resampling.LANCZOS)
pair=Image.new('RGB',(blue.width+red.width,max(blue.height,red.height)),BG);pair.paste(blue,(0,0));pair.paste(red,(blue.width,0));pair.save(S/'Blue_Red_Buildings_Overview.png')
renders=json.loads((O/'render_manifest.json').read_text(encoding='utf8'))
inventory=[]
for r in renders['renders']:
    p=O/r['file'];im=Image.open(p);assert list(im.size)==r['resolution'];inventory.append({'file':r['file'],'dimensions':list(im.size),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
summary={'success':True,'native_capture_count':len(inventory),'frozen_candidate_sha256':M['candidate_blend_sha256'],'images':inventory,'overview':overview,'combined':'Sheets/Blue_Red_Buildings_Overview.png'}
(O/'preview_inventory.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf8')
print('SSF_TEAM_REVIEW_PACKAGED',len(inventory),flush=True)
