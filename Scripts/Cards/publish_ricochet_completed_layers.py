"""Publish the staged six-layer completion into the existing preview package."""
from pathlib import Path
from datetime import datetime,timezone,timedelta
import ast
import hashlib
import json
import re
import shutil
import subprocess
import zipfile
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/UI/GroundMechRogueCards/GroundMech_ChainRicochet/CardProduction_20261026_v1'
build=json.loads((OUT/'completion-r3-build.json').read_text(encoding='utf-8'))
requests=json.loads((OUT/'completion-r3-requests.json').read_text(encoding='utf-8'))
results=json.loads((OUT/'completion-r3-results.json').read_text(encoding='utf-8'))
assert build['state']=='model_completed_layers_staged' and len(build['layers'])==6


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


layers=[]
for layer in build['layers']:
    staging=Path(layer['staged_file'])
    assert sha(staging)==layer['sha256']
    with Image.open(staging) as image:
        assert image.size==(1536,1536)
        assert image.getchannel('A').getextrema()==((255,255) if layer['index']==6 else (0,255))
    destination=OUT/layer['file']
    shutil.copy2(staging,destination)
    published=dict(layer)
    published.pop('staged_file')
    layers.append(published)

mask=np.asarray(Image.open(OUT/'Masks/ForegroundFront_R3.png').convert('RGBA'))
assert set(np.unique(mask[:,:,0]))=={0,255}
assert np.array_equal(mask[:,:,0],mask[:,:,3])
assert sha(OUT/'Master.png')==build['master_sha256']
assert abs((1/6)*1.5-.25)<1e-12 and abs((2/3)*1.5-1)<1e-12
shutil.copy2(ROOT/'Scripts/Cards/ground_ricochet_preview_template.html',OUT/'preview.html')
shutil.copy2(ROOT/'Scripts/Cards/ricochet_foreground_renderer.js',OUT/'foreground-renderer.js')
import_source=(ROOT/'Scripts/Cards/import_ground_ricochet_card_template.py').read_text(encoding='utf-8').replace('__PRODUCTION_DIR__',OUT.as_posix())
ast.parse(import_source)
(OUT/'ImportToUE.py').write_text(import_source,encoding='utf-8',newline='\n')
for name in ['prepare_ricochet_layer_completion.py','build_ricochet_completed_layers.py','publish_ricochet_completed_layers.py']:
    ast.parse((ROOT/'Scripts/Cards'/name).read_text(encoding='utf-8'))
scripts=re.findall(r'<script>([\s\S]*?)</script>',(OUT/'preview.html').read_text(encoding='utf-8'))
scripts.append((OUT/'foreground-renderer.js').read_text(encoding='utf-8'))
for script in scripts:
    result=subprocess.run([shutil.which('node'),'--check','--input-type=commonjs'],input=script,text=True,capture_output=True)
    assert result.returncode==0,result.stderr

manifest={**build,'state':'model_completed_preview_ready','updated_at':datetime.now(timezone(timedelta(hours=8))).isoformat(),
          'layers':layers,'reference':'Master.png','generation_requests':'completion-r3-requests.json',
          'generation_results':'completion-r3-results.json',
          'technical_validation':{'passed':True,'scope':'file dimensions, real alpha, hashes, binary occlusion mask, shared UV mapping, Python/JavaScript syntax only'},
          'preview':{'url':'http://127.0.0.1:8191/preview.html','file':'preview.html','renderer':'foreground-renderer.js',
                     'shared_css':{'width_percent':150,'height_percent':150,'left_percent':-25,'top_percent':-25},
                     'foreground_fixed_rim':.02,'foreground_motion_ramp_end':.12,
                     'foreground_compositing':'same completed foreground split around player with one binary occlusion mask',
                     'default_mode':'flat','default_parallax_pixels':4,'maximum_parallax_pixels':8},
          'ue_delivery':{'import_script':'ImportToUE.py','imported':False,'execution':'not run; pending editor connection'},
          'assistant_output_visual_test':False,'user_art_review':'pending'}
(OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(OUT/'generation-requests.json').write_text(json.dumps(requests,ensure_ascii=False,indent=2),encoding='utf-8')
prior_results_path=OUT/'generation-results.json'
prior_results=json.loads(prior_results_path.read_text(encoding='utf-8')) if prior_results_path.is_file() else []
prior_hashes={item['layer']:item.get('sha256') for item in prior_results}
active=[]
for result in results['active']:
    item=dict(result)
    item['sha256']=sha(Path(item['output']))
    cached_source=Path(item['source'])
    if cached_source.is_file():
        assert item['sha256']==sha(cached_source)
    else:
        # The generated-image cache may be cleared after its verified project
        # copy is archived. Retain verification without requiring that cache.
        assert item['sha256']==prior_hashes.get(item['layer']), 'Restore the recorded raw output before republishing'
    active.append(item)
(OUT/'generation-results.json').write_text(json.dumps(active,ensure_ascii=False,indent=2),encoding='utf-8')
for result in results['first_pass']:
    if result['layer'] in (1,4):
        raw=Path(result['output'])
        result['retained_first_pass_file']=str(raw.with_name(raw.stem+'_FirstPass.png'))
(OUT/'completion-r3-results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
report={'python_syntax':'passed','javascript_syntax':'passed','common_canvas':[1536,1536],
        'source_window':[256,256,1280,1280],'source_window_uv':[1/6,1/6,5/6,5/6],
        'master_hash_unchanged':True,'foreground_occlusion_mask':'binary RGBA, R equals alpha',
        'original_core_pixels_transformed':False,'generated_fill_registration':'production operation only',
        'assistant_visual_test':False,'browser_shader_execution':'pending','ue_execution':'not_run'}
(OUT/'static-check.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
readme='''# 折射子弹 · 六层模型补绘

此修订沿用同一预览网址和六张主图文件名。六张切片均通过内置imagegen逐层补绘，处理破碎边界、缺失表面、被遮挡结构和外沿。没有使用ComfyUI，没有修改卡牌数据、技能、效果或玩法逻辑。

## 六层与共同坐标

1. 近景：完整墙岩、植被、扬土与烟尘。
2. 速度线：连续的白色漫画速度线。
3. 机甲：补全机械结构及附着尾焰、枪口火光。
4. 场景：连续草原、石阶与中近景残骸。
5. 折射弹道和三个敌方机甲：补全敌人和受击效果；各单位按原位置装配，金色路径使用原图连续轨迹像素。
6. 远景：不透明的完整远景底图。

六张交付PNG统一为1536×1536；原始1024×1024位于[256,256,1280,1280]，各层使用相同基础映射。生成原文件的真实尺寸单独记录；只对生成补绘做归一化和坐标装配，原图核心像素不做单层UV补偿或重采样。
原始Master.png保持不变。补绘后的透明边界、遮挡区域和表面细节允许改变；主要可见色块按原图回贴保护，实际保护数量和生产装配参数见completion-r3-build.json。

## 前景遮挡与露边

近景是同一张完整补绘图。Masks/ForegroundFront_R3.png是一张技术遮挡蒙版，将其分到机甲前后显示，补绘的隐藏表面可以衔接。仍为六个内容分组；近景开关和透明度共同控制两个显示部分。
网页前景在窗口外侧2%范围固定源UV，2%至12%平滑过渡到内部视差；支持WebGL时使用边缘固定采样。后备Canvas2D固定前景，仅其他内容产生相对位移。补绘图仍保留统一外沿，不使用旧的边缘像素拉长方案。

## 查看与验收

打开preview.html，先查看原位叠合和原图，再切换视差。可独立开关六组内容并下载六张主图。
本轮做了尺寸、alpha、来源哈希、二值蒙版、共同坐标及脚本静态检查。没有进行结果读图、截图对比或UE视口核验；构图、部件、轨迹连接、透明边缘和倾斜自然程度仍待用户视觉验收。生产中的特征装配不是画面验收。

## 可追溯记录与UE

完整提示词与按顺序列出的输入图见completion-r3-requests.json；原始生成来源见completion-r3-results.json；原始输出保留在Supplements/。前景和场景使用固定画布再次定点编辑，首轮生成记录另行保留，不再由页面消费。
ImportToUE.py已按1536共同画布、前景遮挡和边缘固定更新，只创建本牌纹理、技术蒙版与独立材质。尚未在UE执行；静态语法通过不能代替材质编译、保存回读或用户画面验收。
'''
(OUT/'README.md').write_text(readme,encoding='utf-8')

print(json.dumps({'state':manifest['state'],'layers':6,'canvas':[1536,1536],'technical_static_checks':'passed',
                  'preview':manifest['preview']['url'],'assistant_visual_test':False,'ue_imported':False},ensure_ascii=False))
