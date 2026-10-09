"""Unretouched native renders and A_v3 boards placed together for visual QA."""
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
import json
O=Path(__file__).resolve().parents[1]
A=O.parent/'LocalTeamColorReference_A_v3_20261008'
out=O/'Comparisons';out.mkdir(exist_ok=True)
font=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',25)
small=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',20)
for cp in sorted((A/'Configs').glob('*.json')):
    c=json.loads(cp.read_text(encoding='utf8'));key=c['id']
    canvas=Image.new('RGB',(2400,1920),'#e9e9e5');d=ImageDraw.Draw(canvas)
    d.text((25,10),c['name']+' / A_v3 参考 ↔ Blender 实际成品（原渲染未修图）',font=font,fill='#2C3735')
    for j,team in enumerate(('blue','red')):
        with Image.open(A/'Boards'/f'{key}_{team}.png') as im:
            im=im.convert('RGB');im.thumbnail((1190,780));canvas.paste(im,(1200*j+(1200-im.width)//2,50))
    for t,team in enumerate(('Blue','Red')):
        for j,view in enumerate(('Hero','Front','Left','Back')):
            with Image.open(O/'Previews'/f'{key}_{team}_{view}.png') as im:
                im=im.convert('RGB');im.thumbnail((598,522));x=600*j+(600-im.width)//2;y=855+530*t
                canvas.paste(im,(x,y));d.text((600*j+15,y-27),team+' / '+view,font=small,fill='#2C3735')
    canvas.save(out/f'{key}_A3_B1.png')
print('14_MODEL_COMPARISON_SHEETS_READY')
