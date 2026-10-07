"""Readable contact sheets made from actual rendered views for visual inspection."""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

parser=argparse.ArgumentParser();parser.add_argument('--version',type=int,default=4)
args=parser.parse_args()
ROOT=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
OUT=ROOT/f'References_A_v{args.version}'
QA=OUT/'QA';QA.mkdir(exist_ok=True)
assert not (OUT/'reference_manifest.json').exists(), 'Published version is frozen'
font=ImageFont.truetype('C:/Windows/Fonts/msyh.ttc',25)
groups=[('Buildings_1_AllViews',['AirBase','CloningCenter','CommandCenter']),
        ('Buildings_2_AllViews',['MilitaryFactory','Reactor','StrategyCenter']),
        ('Accessories_AllViews',['Floor','Lamp','Light','Drone'])]
for name,keys in groups:
    out=Image.new('RGB',(2400,650*len(keys)),'#F3EEE5');draw=ImageDraw.Draw(out)
    for y,key in enumerate(keys):
        for x,view in enumerate(['Hero','Front','Left','Back']):
            im=Image.open(OUT/'Renders'/f'{key}_{view}.png').convert('RGB')
            out.paste(im.resize((600,600),Image.Resampling.LANCZOS),(600*x,650*y+40))
            draw.text((600*x+15,650*y+8),key+' / '+view,font=font,fill='#1A182F')
    out.save(QA/(name+'.png'))
print('Three contacts cover all 40 core renders')
