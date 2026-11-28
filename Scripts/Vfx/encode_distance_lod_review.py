"""Encode the UE capture into one synchronized three-column GIF per effect and a local review page."""
import argparse
import html
import json
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / 'ArtSource/FX/DistanceLOD_20261127'
AUDIT = ROOT / 'Progress/Evidence/20261127-默认对局特效LOD审计.json'
EXTRA_LABELS = {
    'NS_Sparks':'模型弹体命中火花','NS_Sparks_Smaller':'模型弹体小火花','NS_Trails':'模型火箭飞行尾迹',
    'NS_WM01_MissileFlight': '重防号备用单枚导弹',
    'NS_GroundDestruction_03': '地面单位摧毁', 'NS_WingmanDestruction_05': '僚机摧毁',
    'N_Stylized_Explosion_1A_Classic_Explosion': '巡天一级舰腹机炮爆炸',
    'NS_Explosion_04': '巡天二级双联火炮爆炸', 'NS_Explosion_07': '巡天三级三联火炮爆炸',
    'NS_XuntianBlackSmoke': '巡天舰腹炮滞留黑烟',
}
HEADERS = ('近档   35 m', '中档   110 m', '远档   190 m')
GAP = 3
HEADER = 34


def section(label):
    for prefix, group in [('巡天', '巡天舰腹炮'), ('游骑兵', '游骑兵'), ('重防号', '重防号'), ('僚机', '僚机')]:
        if label.startswith(prefix):
            return group
    if label.startswith('模型'): return '模型弹体'
    if any(label.startswith(word) for word in ('建筑', '施工', '据点')):
        return '建筑与据点'
    return '部队与指挥官'


def encode(folder, file, sample):
    count = sample['frames']
    with Image.open(folder / '0-000.png') as first:
        width, height = first.size
    total_width = 3 * width + 2 * GAP
    band = Image.new('RGB', (total_width, HEADER), '#192534')
    draw = ImageDraw.Draw(band)
    face = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 17)
    for col, label in enumerate(HEADERS):
        draw.text((col * (width + GAP) + 12, 4), label, font=face, fill='#f1f4f6')
    frames = []
    for idx in range(count):
        canvas = Image.new('RGB', (total_width, height + HEADER), '#344759')
        canvas.paste(band, (0, 0))
        for col in range(3):
            with Image.open(folder / f'{col}-{idx:03d}.png') as frame:
                assert frame.size == (width, height)
                canvas.paste(frame.convert('RGB'), (col * (width + GAP), HEADER))
        frames.append(canvas)
    # One shared palette prevents independent per-frame palette changes during playback.
    palette_source = Image.new('RGB', (total_width // 2, (height + HEADER) // 2 * 10))
    patch_height = (height + HEADER) // 2
    for step in range(10):
        thumb = frames[round(step * (len(frames) - 1) / 9)].resize((total_width // 2, patch_height), Image.Resampling.BOX)
        palette_source.paste(thumb, (0, step * patch_height))
    palette = palette_source.quantize(colors=256, method=Image.Quantize.MEDIANCUT)
    indexed = [frame.quantize(palette=palette, dither=Image.Dither.FLOYDSTEINBERG) for frame in frames]
    ticks = max(count, round(sample['duration'] * 100))
    delays = [(round((i + 1) * ticks / count) - round(i * ticks / count)) * 10 for i in range(count)]
    temp = file.with_suffix('.part')
    indexed[0].save(temp, format='GIF', save_all=True, append_images=indexed[1:], loop=0, duration=delays, optimize=False, disposal=1)
    with Image.open(temp) as gif:
        assert gif.n_frames > 1, f'{file.name} encoded as a single frame'
        duration = 0
        for i in range(gif.n_frames):
            gif.seek(i)
            duration += gif.info['duration']
        assert gif.size == (total_width, HEADER + height) and gif.info.get('loop') == 0 and duration == ticks * 10
        result = {'encoded_frames': gif.n_frames, 'duration_ms': duration, 'size': list(gif.size)}
    temp.replace(file)
    return result


def write_index(items, filename='index.html', title='战斗特效距离 LOD 审核', related=''):
    intro = ('每项动图同步播放三个距离。三列取景宽度相同，方便检查核心轮廓、粒子层次和消散。'
             '点击动图可打开原尺寸。')
    def card(entry):
        link = html.escape(entry['gif'], quote=True)
        name = html.escape(entry['label'])
        status = entry.get('review_note') or ('原有三档复核' if entry['previous'] else '本次补齐')
        return (f'<figure><figcaption><strong>{name}</strong><span>{status}</span></figcaption>'
                f'<a href="{link}"><img loading="lazy" src="{link}" alt="{name}近中远同步动图" width="1086" height="282"></a></figure>')
    groups = ['巡天舰腹炮', '游骑兵', '重防号', '僚机', '建筑与据点', '部队与指挥官', '模型弹体']
    groups = [(g, [it for it in items if section(it['label']) == g]) for g in groups]
    nav = ' '.join(f'<a href="#g{i}">{g}（{len(rows)}）</a>' for i, (g, rows) in enumerate(groups) if rows)
    sections = ''.join(f'<section id="g{i}"><h2>{g}</h2>{"".join(card(it) for it in rows)}</section>' for i, (g, rows) in enumerate(groups) if rows)
    css = ('*{box-sizing:border-box}html{color-scheme:dark}body{margin:0;background:#101923;color:#eef4f8;'
           'font:16px/1.55 "Microsoft YaHei UI",sans-serif}main{max-width:1120px;margin:auto;padding:28px 16px 70px}'
           'h1{margin:0;font-size:28px}h2{margin:44px 0 12px;font-size:22px}p,span{color:#bacad8}nav{display:flex;flex-wrap:wrap;gap:8px}'
           'nav a{padding:6px 12px;background:#213547;color:#eff9ff;border-radius:5px;text-decoration:none}'
           'figure{margin:0 0 18px;background:#192534;padding:10px;border-radius:7px}figcaption{display:flex;justify-content:space-between;align-items:baseline;gap:10px;margin:0 4px 8px}'
           'figcaption span{font-size:13px}img{display:block;width:100%;height:auto}')
    (OUTPUT / filename).write_text(f'<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{html.escape(title)} · GuLiStrike</title><style>{css}</style><main><h1>{html.escape(title)}</h1><p>{len(items)} 项 · {intro}</p><nav>{related}{nav}</nav>{sections}</main></html>', encoding='utf8')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('frame_root', type=Path)
    parser.add_argument('--only', nargs='*')
    args = parser.parse_args()
    progress = json.loads((args.frame_root / 'progress.json').read_text(encoding='utf8'))
    rows = json.loads(AUDIT.read_text(encoding='utf8'))['niagara_assets']
    expected = {row['name'] for row in rows if row['scope'] in ('未见距离减量', '备用单弹', '模型蓝图依赖')
                or (row['scope'] == '已制作、当前模块未接入' and row['resident'])}
    selected = expected if args.only is None else set(args.only)
    if not selected or selected - expected or selected - progress['finished'].keys():
        raise RuntimeError('One or more requested effects have no finished UE recording.')
    if selected.intersection(progress['failed']) or selected.intersection(progress['pending']):
        raise RuntimeError('Complete the selected recordings before publishing.')
    if args.only is None and set(progress['finished']) != expected:
        raise RuntimeError('The full recording does not match the effect catalog.')
    manifest = OUTPUT / 'capture.json'
    previous = json.loads(manifest.read_text(encoding='utf8')) if args.only is not None and manifest.exists() else {}
    entries_by_name = {item['name']: item for item in previous.get('effects', [])}
    simulation = dict(previous.get('simulation', {}))
    if (expected - selected) - (entries_by_name.keys() & simulation.keys()):
        raise RuntimeError('An incremental review needs the existing full gallery.')
    rules = json.loads((OUTPUT / 'authored-systems.json').read_text(encoding='utf8'))
    dest = OUTPUT / 'GIF'
    dest.mkdir(parents=True, exist_ok=True)
    entries = []
    for row in rows:
        name = row['name']
        if name not in expected:
            continue
        media = dest / (name + '.gif')
        if name in selected:
            sample = progress['finished'][name]
            metadata = encode(args.frame_root / name, media, sample)
            simulation[name] = sample
            entries_by_name[name] = dict(name=name, label=row.get('display_name') or EXTRA_LABELS[name],
                                         previous=row['scope']=='已制作、当前模块未接入', gif='GIF/'+media.name, **metadata)
        if not media.is_file():
            raise FileNotFoundError(media)
        entry = entries_by_name[name]
        rule, paths = rules.get(name, {}), simulation[name]['paths']
        if rule.get('all_distances_identical'):
            entry['review_note'] = '三档共用红色燃烧爆发'
        elif rule.get('shared_middle') or (paths[0] != paths[1] and paths[1] == paths[2]):
            entry['review_note'] = '远档沿用中档'
        entries.append(entry)
    updated = [entry for entry in entries if entry.get('review_note')]
    related = ''
    if updated:
        related = f'<a href="review-update.html">本轮调整（{len(updated)} 项）</a> '
        write_index(updated, 'review-update.html', '特效调整复核', '<a href="index.html">查看全部特效</a> ')
    write_index(entries, related=related)
    temp = manifest.with_suffix('.tmp')
    temp.write_text(json.dumps({'effects':entries,'simulation':simulation,'failed':{}},ensure_ascii=False,indent=2),encoding='utf8')
    temp.replace(manifest)
    print(f'{len(selected)} updated, {len(entries)} review GIFs')


if __name__ == '__main__':
    main()
