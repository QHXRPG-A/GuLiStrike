"""Lay out unchanged native Blender renders for style review and quantify three band visibility."""
from pathlib import Path
import hashlib
import json
import shutil
import numpy as np
from PIL import Image,ImageDraw,ImageFont
ROOT=Path('D:/UE5.7/test1/ArtSource/Buildings/SSFStyle_20261005')
OUT=ROOT/'References_A_v7';QA=OUT/'QA';QA.mkdir(exist_ok=True)
assert not (OUT/'reference_manifest.json').exists()
OLD=ROOT/'StyleAudit_A_v6';PREVIEW=OLD/'Preview_Candidate_v1'
FONT='C:/Windows/Fonts/msyh.ttc';BOLD='C:/Windows/Fonts/msyhbd.ttc'
BG='#F3EEE5';INK='#1A182F';MUTED='#62596B'
def text(draw,xy,label,size=34,bold=False):
    draw.text(xy,label,font=ImageFont.truetype(BOLD if bold else FONT,size),fill=INK if bold else MUTED)
def box(path):
    a=np.array(Image.open(path).convert('RGB'),dtype=np.int16);bg=a[0,0]
    ys,xs=np.where(np.max(np.abs(a-bg),axis=2)>20)
    return (max(0,int(xs.min())-45),max(0,int(ys.min())-45),min(a.shape[1],int(xs.max())+46),min(a.shape[0],int(ys.max())+46))
def paste_model(canvas,path,bbox,xy,size):
    im=Image.open(path).convert('RGB').crop(bbox)
    im.thumbnail(size,Image.Resampling.LANCZOS)
    canvas.paste(im,(int(xy[0]+(size[0]-im.width)/2),int(xy[1]+(size[1]-im.height)/2)))

comparison=Image.new('RGB',(3000,2720),BG);d=ImageDraw.Draw(comparison)
text(d,(72,40),'SSF 线稿与三档明暗 · 同机位对照',56,True)
text(d,(76,125),'配色、几何、法线和光向保持 A_v6；调整线条可见性与亮／中／暗分区。',34)
for row,(key,label) in enumerate([('MilitaryFactory','军工厂'),('AirBase','空军基地')]):
    y=240+row*1200;bbox=box(OUT/'Renders'/f'{key}_Hero.png')
    for col,(directory,version,detail) in enumerate([(ROOT/'References_A_v6','A_v6','线细，亮部面积偏大'),(OUT,'A_v7','外轮廓略重，三档分区清楚')]):
        x=col*1500
        text(d,(x+72,y),label+' · '+version,40,True)
        text(d,(x+72,y+68),detail,31)
        paste_model(comparison,directory/'Renders'/f'{key}_Hero.png',bbox,(x+58,y+138),(1384,1000))
text(d,(76,2646),'实际 Blender 参考渲染；仍待审核 A，成品重制、LOD 和 UE 正式材质尚未开始。',30)
comparison.save(OUT/'Style_Comparison_A_v6_to_A_v7.png')

stats={}
for version,path in [('A_v6',OLD/'Renders/MilitaryFactory_BandIDs.png'),('A_v7',PREVIEW/'Renders/MilitaryFactory_BandIDs.png')]:
    a=np.array(Image.open(path).convert('RGB'))
    c=[int(np.count_nonzero((a[:,:,i]>245)&(a[:,:,(i+1)%3]<10)&(a[:,:,(i+2)%3]<10))) for i in range(3)]
    stats[version]={'pixels':dict(zip(['bright','middle','shadow'],c)),
                    'percent':dict(zip(['bright','middle','shadow'],[round(n/sum(c)*100,2) for n in c])),
                    'source_file':path.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
for src,name in [(OLD/'Renders/MilitaryFactory_BandIDs.png','A_v6_BandIDs.png'),
                 (OLD/'Renders/MilitaryFactory_LineOnly.png','A_v6_LineOnly.png'),
                 (PREVIEW/'Renders/MilitaryFactory_BandIDs.png','A_v7_BandIDs.png')]:shutil.copyfile(src,QA/name)
breakdown=Image.new('RGB',(3000,2740),BG);d=ImageDraw.Draw(breakdown)
text(d,(72,40),'A_v6 核对 · 线稿和三档色阶分别显示',54,True)
text(d,(76,125),'同一模型、同一机位；彩色分区仅作诊断，红=亮、绿=中、蓝=暗。',34)
panels=[(OLD/'Renders/MilitaryFactory_FlatColor.png','关闭线稿与明暗 · 平涂'),
        (OLD/'Renders/MilitaryFactory_A_v6_Current.png','A_v6 原始参考 · 两者开启'),
        (OLD/'Renders/MilitaryFactory_LineOnly.png','A_v6 线稿拆分 · 白底'),
        (OLD/'Renders/MilitaryFactory_BandIDs.png','A_v6 三档实际分区 · 诊断色')]
bbox=box(OLD/'Renders/MilitaryFactory_BandIDs.png')
for i,(path,label) in enumerate(panels):
    x=(i%2)*1500;y=240+(i//2)*1200
    text(d,(x+72,y),label,37,True)
    paste_model(breakdown,path,bbox,(x+58,y+100),(1384,1050))
text(d,(76,2645),'A_v6 军工厂本机位：亮 75.07% / 中 24.23% / 暗 0.70%；三档存在，但阴影面积不足。',30)
breakdown.save(OUT/'Style_Breakdown_A_v6.png')
shutil.copyfile(OLD/'native_node_audit.json',OUT/'A_v6_native_node_audit.json')
for key in ('MilitaryFactory','AirBase','StrategyCenter'):
    # Final full-render candidate must agree with the inspected preview, allowing render noise only.
    a=np.array(Image.open(OUT/'Renders'/f'{key}_Hero.png').convert('RGB'),dtype=np.int16)
    b=np.array(Image.open(PREVIEW/'Renders'/f'{key}_Hero.png').convert('RGB'),dtype=np.int16)
    error=float(np.abs(a-b).mean())
    assert error<.1,(key,error)
stats.update({'version':'SSF_StyleVisibility_A_v7','success':True,'asset':'MilitaryFactory','view':'Hero',
              'method':'Native connected ColorRamp diagnostic IDs; saturated core pixels only, antialiasing/logo excluded.',
              'ratios_are_specific_to_this_camera':True,'A_v7_palette_unchanged':True,
              'preview_and_full_render_mean_RGB_difference_below_0_1':True,
              'interpretation':'A_v6 has connected linework and all three bands, but visible shadow coverage is only 0.70%; A_v7 adjusts thresholds and line widths while preserving palette values.'})
(OUT/'style_visibility_validation.json').write_text(json.dumps(stats,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:stats[k]['percent'] for k in ('A_v6','A_v7')},ensure_ascii=False))
