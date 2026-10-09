import json
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
O=Path(__file__).resolve().parents[1]
font=ImageFont.truetype('C:/Windows/Fonts/arial.ttf',18)
r=json.loads((O/'Reports/component-id-projections.json').read_text(encoding='utf8'))
out=O/'Reports/ComponentID';out.mkdir(exist_ok=True)
for key,views in r.items():
    sheet=Image.new('RGB',(3300,1000),'white')
    for n,(view,rows) in enumerate(views.items()):
        im=Image.open(O/'Previews'/f'{key}_Blue_{view}.png').convert('RGB');d=ImageDraw.Draw(im)
        for row in rows:
            x=row['xy'][0]*im.width;y=(1-row['xy'][1])*im.height;t=str(row['id'])
            if row['role']=='Gray':continue
            d.rectangle((x-1,y-1,x+len(t)*11+3,y+21),fill='white');d.text((x,y),t,fill='black',font=font)
        sheet.paste(im,(1100*n,0))
    sheet.save(out/(key+'.png'))
print('COMPONENT_ID_DIAGNOSTICS_READY')
