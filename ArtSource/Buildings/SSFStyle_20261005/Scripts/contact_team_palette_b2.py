"""Inspection contacts: only arrange the native Blender renders, without retouching."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OUT = Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005/TeamPalette_B_v2_20261007')
KEYS = ['AirBase', 'CloningCenter', 'CommandCenter', 'MilitaryFactory', 'Reactor', 'StrategyCenter']
BG = (245, 239, 228)
FONT = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 24)

def contact(path, entries, cols, cell=650):
    rows = (len(entries) + cols - 1) // cols
    canvas = Image.new('RGB', (cols * cell, rows * (cell + 40)), BG)
    draw = ImageDraw.Draw(canvas)
    for i, name in enumerate(entries):
        p = OUT / 'Renders' / (name + '.png')
        with Image.open(p) as native:
            rgba = native.convert('RGBA')
        base = Image.new('RGBA', rgba.size, (*BG, 255))
        base.alpha_composite(rgba)
        im = base.convert('RGB')
        im.thumbnail((cell, cell), Image.Resampling.LANCZOS)
        x, y = (i % cols) * cell, (i // cols) * (cell + 40)
        canvas.paste(im, (x + (cell-im.width)//2, y + (cell-im.height)//2))
        draw.text((x + 10, y + cell + 5), name, font=FONT, fill=(30, 28, 45))
    canvas.save(OUT / 'Sheets' / path)
    print(path)

for team in ['Blue', 'Red']:
    for page in range(3):
        keys = KEYS[page*2:page*2+2]
        contact(f'QA_{team}_Views_{page+1}.png', [f'{team}_{k}_{v}' for k in keys for v in ['Hero', 'Front', 'Left', 'Back']], 4)
    for page in range(2):
        keys = KEYS[page*3:page*3+3]
        contact(f'QA_{team}_LODs_{page+1}.png', [f'{team}_{k}_Hero' + (f'_LOD{lod}' if lod else '') for k in keys for lod in range(3)], 3)
