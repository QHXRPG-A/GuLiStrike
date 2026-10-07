"""Contact sheets from actual UE PNGs; no generated or substitute model images."""
import json
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont,ImageChops
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');D=R/'UE_Delivery_v1';V=D/'Preview'
validation=json.loads((D/'ue_validation.json').read_text(encoding='utf8'))
assert validation['success'] and len(validation['captures'])==54
font=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',23)
titlefont=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',34)
buildings=['AirBase','CloningCenter','CommandCenter','MilitaryFactory','Reactor','StrategyCenter']
names={'AirBase':'空军基地','CloningCenter':'克隆中心','CommandCenter':'指挥中心','MilitaryFactory':'军工厂',
       'Reactor':'反应堆','StrategyCenter':'战略中心','Floor':'平台','Lamp':'灯柱','Light':'灯片','Drone':'无人机'}
captured={c['name']:c for c in validation['captures']}
records=[]
for c in validation['captures']:
    im=Image.open(c['path']).convert('RGB');assert im.size==(2048,2048)
    background=im.getpixel((0,0));diff=ImageChops.difference(im,Image.new('RGB',im.size,background))
    bbox=diff.convert('L').point(lambda n:255 if n>12 else 0).getbbox()
    records.append({'capture':c['name'],'dimensions':list(im.size),'foreground_bbox':bbox,'background_rgb':list(background)})

def board(filename,title,keys,columns=3,cell=520,crop=False):
    rows=(len(keys)+columns-1)//columns;height=cell+(90 if crop else 55)
    canvas=Image.new('RGB',(columns*cell,90+rows*height),(246,241,231));draw=ImageDraw.Draw(canvas)
    draw.text((24,20),title,font=titlefont,fill=(32,29,50))
    for i,key in enumerate(keys):
        c=captured[key];im=Image.open(c['path']).convert('RGB')
        if crop and any(tag in key for tag in ('Tactical','Overview')):
            # Labelled centre crops aid inspection of tiny distance previews.
            im=im.crop((768,768,1280,1280))
        im.thumbnail((cell,cell),Image.Resampling.LANCZOS)
        x=(i%columns)*cell;y=90+(i//columns)*height
        canvas.paste(im,(x+(cell-im.width)//2,y+(cell-im.height)//2))
        suffix=key[len(c['key'])+1:]
        if crop:
            metres={'Near_35m':'35m / 25°','Tactical_300m':'300m / 55°','Tactical_700m':'700m / 55°','Overview_1500m':'1500m / 55°'}[suffix]
            label=names[c['key']]+' · '+metres
            detail='实际 LOD'+str(c['component_predicted_LOD'])
            if any(tag in key for tag in ('Tactical','Overview')):detail+=' · 中心裁切'
            draw.text((x+12,y+cell+5),label,font=font,fill=(32,29,50))
            draw.text((x+12,y+cell+38),detail,font=font,fill=(77,71,86))
        else:
            label=names[c['key']]+' · '+suffix
            if c.get('component_predicted_LOD') is not None:label+=' / 实际LOD'+str(c['component_predicted_LOD'])
            draw.text((x+12,y+cell+5),label,font=font,fill=(32,29,50))
    canvas.save(V/filename)
    return str(V/filename)

outputs=[]
outputs.append(board('UE_Buildings_LOD0_Contact.png','SSF · 实际 UE 正式资源 / B_v1', [k+'_LOD0' for k in buildings]))
allkeys=buildings+['Floor','Drone','Lamp','Light']
outputs.append(board('UE_AllAssets_3LOD_Contact.png','SSF · UE 三档 LOD 同机位', [k+'_LOD'+str(i) for k in allkeys for i in range(3)]))
outputs.append(board('UE_LOD_Contact_1.png','SSF · UE LOD0 / 1 / 2', [k+'_LOD'+str(i) for k in buildings[:3] for i in range(3)],cell=600))
outputs.append(board('UE_LOD_Contact_2.png','SSF · UE LOD0 / 1 / 2', [k+'_LOD'+str(i) for k in buildings[3:] for i in range(3)],cell=600))
outputs.append(board('UE_Props_3LOD_Contact.png','SSF · 配套 / UE LOD0 / 1 / 2', [k+'_LOD'+str(i) for k in allkeys[6:] for i in range(3)],cell=520))
distance=[k+'_'+d for k in buildings for d in ('Near_35m','Tactical_300m','Tactical_700m','Overview_1500m')]
outputs.append(board('UE_Distance_LOD_Contact.png','SSF · 独立距离预览 / 远距为中心裁切',distance,columns=4,cell=430,crop=True))
(D/'preview_image_inventory.json').write_text(json.dumps({'success':True,'captures':records,'boards':outputs},ensure_ascii=False,indent=2),encoding='utf8')
print(json.dumps({'success':True,'boards':outputs},ensure_ascii=False))
