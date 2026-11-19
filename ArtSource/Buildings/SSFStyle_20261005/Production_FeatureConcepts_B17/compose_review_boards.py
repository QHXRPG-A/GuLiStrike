"""Lay out the actual Blender views and identified A5 crop for each B17 board."""

import argparse
import hashlib
import html
import json
from functools import lru_cache
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parent
REFERENCE = ROOT.parent / 'References_FeatureConcepts_A5'
WIDTH, HEIGHT = 2320, 1860
MARGIN = 56
GUTTER = 16
INK = '#1F2631'
SUBDUED = '#526071'
PAPER = '#F7F9FB'
PANEL = '#E5EAEE'
LABELS = (('Hero', '三分之四'), ('Front', '正面'), ('Left', '左侧'), ('Back', '背面'))


@lru_cache
def font(size, bold=False):
    family = 'msyhbd.ttc' if bold else 'msyh.ttc'
    return ImageFont.truetype(f'C:/Windows/Fonts/{family}', size)


def label(draw, x, y, text, size=24, color=INK, bold=False):
    draw.text((x, y), text, font=font(size, bold), fill=color)


def panel(canvas, path, xy, size, crop=None):
    x, y = xy
    w, h = size
    surface = Image.new('RGBA', (w, h), PANEL)
    with Image.open(path) as original:
        image = original.crop(crop).convert('RGBA') if crop else original.convert('RGBA')
        image.thumbnail((w, h), Image.Resampling.LANCZOS)
        # A5 crops can be small in their source image. Fit them to the panel at
        # their original aspect ratio so the corresponding structures are legible.
        if crop:
            scale = min(w / image.width, h / image.height)
            image = image.resize((round(image.width * scale), round(image.height * scale)), Image.Resampling.LANCZOS)
        surface.alpha_composite(image, ((w - image.width) // 2, (h - image.height) // 2))
    canvas.paste(surface.convert('RGB'), (x, y))


def swatches(draw, y, colors, width):
    for index, (name, color) in enumerate(colors):
        x = MARGIN + index * width
        draw.rounded_rectangle((x, y + 2, x + 30, y + 32), radius=5, fill=color, outline='#83929E')
        label(draw, x + 43, y, f'{name}  {color}', 23)


def make_board(row, raw, output, revision):
    canvas = Image.new('RGB', (WIDTH, HEIGHT), PAPER)
    draw = ImageDraw.Draw(canvas)
    draw.rectangle((MARGIN, 46, MARGIN + 8, 118), fill='#557B78')
    label(draw, MARGIN + 25, 38, f'{row["name"]}  ·  {revision} 贴图审核', 51, bold=True)
    label(draw, MARGIN + 25, 110, f'ModelId {row["model_id"]}    LOD0 {row["triangles"]:,} 三角面    三档明暗 40% / 68% / 100%', 25, SUBDUED)
    label(draw, MARGIN, 165, f'可变区  {row["team_areas"]}', 26, bold=True)
    label(draw, WIDTH - MARGIN - 440, 166, '红蓝四个角度使用相同正交比例', 23, SUBDUED)

    tile_w = (WIDTH - MARGIN * 2 - GUTTER * 3) // 4
    tile_h = 412
    for team, name, y, colors in (('Blue', '蓝方', 222, ('#6AA4BE', '#274E61')),
                                  ('Red', '红方', 694, ('#A34053', '#662249'))):
        draw.rounded_rectangle((MARGIN, y + 4, MARGIN + 18, y + 27), radius=4, fill=colors[0])
        label(draw, MARGIN + 28, y, f'{name}  {colors[0]} / {colors[1]}', 22, bold=True)
        for index, (key, caption) in enumerate(LABELS):
            x = MARGIN + index * (tile_w + GUTTER)
            label(draw, x + tile_w - 118, y, caption, 22, SUBDUED)
            panel(canvas, raw / row['files'][team][key], (x, y + 43), (tile_w, tile_h))

    draw.line((MARGIN, 1183, WIDTH - MARGIN, 1183), fill='#CBD4DB', width=2)
    label(draw, MARGIN, 1204, '固定区  ·  主要分件底色', 27, bold=True)
    swatches(draw, 1255, row['fixed_swatches'], tile_w + GUTTER)
    label(draw, MARGIN, 1319, f'局部对照  ·  {row["detail_title"]}', 30, bold=True)
    label(draw, WIDTH - MARGIN - 440, 1323, f'A5 局部与 {revision} 同朝向放大', 23, SUBDUED)
    focus_w = (WIDTH - MARGIN * 2 - GUTTER * 2) // 3
    focus_h = 388
    review = ((REFERENCE / row['source'], 'A5 原图局部', row['source_crop']),
              (raw / row['files']['Blue']['Detail'], f'{revision} 蓝方', None),
              (raw / row['files']['Red']['Detail'], f'{revision} 红方', None))
    for index, (path, name, crop) in enumerate(review):
        x = MARGIN + index * (focus_w + GUTTER)
        label(draw, x, 1373, name, 24, SUBDUED)
        panel(canvas, path, (x, 1413), (focus_w, focus_h), crop)
    label(draw, MARGIN, 1818, f'A5 的局部只取原图可见范围。其他角度显示 {revision} 实际模型。', 20, SUBDUED)
    path = output / f'{row["key"]}_{revision}_Review.png'
    canvas.save(path, optimize=True)
    with Image.open(path) as saved:
        assert saved.size == (WIDTH, HEIGHT) and saved.format == 'PNG'
        saved.verify()
    return path


def main(raw, root=ROOT):
    data = json.loads((raw / 'review_renders.json').read_text(encoding='utf-8'))
    revision = data.get('revision', 'B17')
    project = root / data.get('project', 'SSF_A5_SurfaceMarks_B17.blend')
    assert hashlib.sha256(project.read_bytes()).hexdigest() == data[f'{revision.lower()}_sha256']
    output = root / 'ReviewBoards'
    output.mkdir(exist_ok=True)
    cards = []
    for row in data['models']:
        assert hashlib.sha256((REFERENCE / row['source']).read_bytes()).hexdigest() == row['source_sha256']
        path = make_board(row, raw, output, revision)
        row['board'] = path.name
        row['board_sha256'] = hashlib.sha256(path.read_bytes()).hexdigest()
        row['board_size_px'] = [WIDTH, HEIGHT]
        row.pop('files')
        name = html.escape(row['name'])
        cards.append(f'<section id="{row["key"]}"><h2>{name}</h2>'
                     f'<a href="{path.name}"><img src="{path.name}" alt="{name}的红蓝四视角和局部对照" '
                     f'width="{WIDTH}" height="{HEIGHT}" loading="lazy"></a>'
                     f'<p><a href="{path.name}">打开原尺寸图板</a> · '
                     f'<a href="../../References_FeatureConcepts_A5/{row["source"]}">A5 完整参考</a></p></section>')
        print(path.name, path.stat().st_size)
    assert len(data['models']) == 6
    index = ' '.join(f'<a href="#{row["key"]}">{html.escape(row["name"])}</a>' for row in data['models'])
    document = f'''<!doctype html>
<html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">
<title>六座 SSF 建筑 · {revision} 贴图审核</title>
<style>
body{{margin:0;background:#f0f3f5;color:#1f2631;font:16px/1.6 system-ui,sans-serif}}
main{{max-width:1200px;margin:auto;padding:32px 22px 50px}}h1{{margin:0 0 6px}}h2{{font-size:20px}}
nav{{display:flex;flex-wrap:wrap;gap:8px 20px;margin:16px 0 20px}}a{{color:#274e61}}
section{{margin:24px 0;padding:16px;background:#fff;border:1px solid #d6dfe4;border-radius:8px}}
img{{display:block;width:100%;height:auto}}p{{margin:8px 0 0}}
</style>
<main><h1>六座 SSF 建筑 · {revision} 贴图审核</h1>
<p>图板上蓝下红，四个角度共用比例。局部对照使用 A5 原图与 {revision} 模型。</p>
<p><a href="../Previews/SSF_Overview_{revision}.png">红蓝战术总览</a> · <a href="../{project.name}">Blender 工程</a></p>
<nav aria-label="建筑">{index}</nav>{''.join(cards)}</main></html>'''
    (output / 'index.html').write_text(document, encoding='utf-8')
    (output / f'{revision}_Review_Boards_Manifest.json').write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('raw', type=Path)
    parser.add_argument('--root', type=Path, default=ROOT)
    args = parser.parse_args()
    main(args.raw, args.root)
