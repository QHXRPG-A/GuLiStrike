"""Comparison contact sheets of existing actual renders for human visual inspection."""
import json
from pathlib import Path
from PIL import Image,ImageDraw,ImageFont
R=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005');O=R/'Production_B_v1';Q=O/'QA'
keys=[a['key'] for a in json.loads((O/'construction_report.json').read_text(encoding='utf8'))['assets']];font=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',28)
for page in range(3):
 selected=keys[page*4:(page+1)*4]
 for mode in ['Reference_and_Product','Three_LODs','Gray_Normals']:
  w=1600 if mode=='Reference_and_Product' else 1800 if mode=='Three_LODs' else 800;h=850 if mode=='Reference_and_Product' else 650 if mode=='Three_LODs' else 850
  board=Image.new('RGB',(w,h*len(selected)),'#F3EEE5');draw=ImageDraw.Draw(board)
  for row,key in enumerate(selected):
   paths=[R/'References_A_v7/Renders'/f'{key}_Hero.png',O/'Renders'/f'{key}_Hero.png'] if mode=='Reference_and_Product' else [O/'Renders'/f'{key}_Hero{("_LOD"+str(lod)) if lod else ""}.png' for lod in range(3)] if mode=='Three_LODs' else [O/'Renders'/f'{key}_GrayNormals.png']
   size=w//len(paths)
   for col,path in enumerate(paths):
    board.paste(Image.open(path).convert('RGB').resize((size,size),Image.Resampling.LANCZOS),(col*size,row*h+50));draw.text((col*size+15,row*h+10),key+' / '+(('A_v7','B_v1')[col] if mode=='Reference_and_Product' else 'LOD'+str(col) if mode=='Three_LODs' else 'gray'),font=font,fill='#1A182F')
  board.save(Q/(mode+f'_{page+1}.png'))
print('SSF_STATIC_QA_CONTACTS_OK',flush=True)
