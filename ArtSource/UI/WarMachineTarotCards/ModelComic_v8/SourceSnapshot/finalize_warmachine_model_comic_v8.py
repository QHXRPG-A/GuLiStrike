"""Preserve raw outputs and exact image/text requests without inspecting pixels."""
from pathlib import Path
import hashlib
import json
import shutil
import struct

ROOT=Path('D:/UE5.7/test1')
OUT=ROOT/'ArtSource/UI/WarMachineTarotCards/ModelComic_v8'
OLD=OUT.with_name('ModelComic_v7')
GENERATED=Path('C:/Users/a/.codex/generated_images/01a0c4a6-55da-79a3-a89d-4a78cb43dd89')
FILES={'FireRate':'exec-ba9e4c1f-99eb-4974-ae39-f6450168f6be.png',
       'HighSpeed':'exec-9b1bde9e-1530-4346-aa9a-8f9eb100bdb7.png'}
TITLES={'FireRate':'增加射速','HighSpeed':'极速机动'}
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def png_info(path):
    raw=path.read_bytes()
    assert raw[:8]==b'\x89PNG\r\n\x1a\n'
    return {'path':path.as_posix(),'sha256':hashlib.sha256(raw).hexdigest(),
            'bytes':len(raw),'resolution':list(struct.unpack('>II',raw[16:24]))}

requests=json.loads((OUT/'generation-requests.json').read_text(encoding='utf-8'))
cards=[]
for request in requests:
    name=request['id']
    original=OLD/'Prompts'/f'{name}.txt'
    copy=OUT/'Prompts'/f'{name}.txt'
    assert original.read_bytes()==copy.read_bytes()==request['arguments']['prompt'].encode('utf-8')
    assert digest(copy)==request['prompt_sha256']
    src=GENERATED/FILES[name]
    dest=OUT/f'{name}.png'
    shutil.copyfile(src,dest)
    assert digest(src)==digest(dest)
    refs=[png_info(Path(p)) for p in request['arguments']['referenced_image_paths']]
    for ref in refs:
        ref['role']='model reference image, not an edit target'
        assert ref['sha256']!=digest(OLD/'References'/Path(ref['path']).name)
    cards.append({'id':name,'title':TITLES[name],'source_output':src.as_posix(),
        'output':png_info(dest),'raw_output_unchanged':True,'input_images_ordered':refs,
        'prompt_file':f'Prompts/{name}.txt','prompt':request['arguments']['prompt'],
        'prompt_sha256':request['prompt_sha256'],'prompt_exactly_matches_v7':True,
        'transparent_background':False})
manifest={'version':'ModelComic_v8','date':'2026-09-28','tool':'built-in image_gen.imagegen',
    'authorization':'开始第二个任务：重新制作增加射速 和 极速机动，prompt文字部分不变，图像部分更新成更改后的',
    'source_model_version':'WarMachine_LevelNodes_v6',
    'model_authorization_scope':'Use the latest revised model for independent UE reference captures and two card generations; not general production acceptance.',
    'source_model':'D:/UE5.7/test1/ArtSource/TacticalStyle_20260916/WarMachine_LevelNodes_v6/WarMachine_LevelNodes_Review.blend',
    'source_model_sha256':digest(ROOT/'ArtSource/TacticalStyle_20260916/WarMachine_LevelNodes_v6/WarMachine_LevelNodes_Review.blend'),
    'preview_asset':'/Game/GuLiStrike/Cards/WarMachineTarot/ModelReferences/v8/Meshes/SM_WarMachine_LevelNodes_Reference',
    'assistant_visual_review':'not_performed_per_user_request','user_art_review':'pending',
    'card_ue_import':'not_performed','production_mesh_replaced':False,'layer_separation':'not_performed',
    'missile_damage':'Existing ModelComic_v7/MissileDamage.png retained; no generation requested.',
    'temporary_capture_actors_readback':[],'cards':cards}
(OUT/'generation-manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
snap=OUT/'SourceSnapshot';snap.mkdir(exist_ok=True)
for script in ('Scripts/Blender/export_warmachine_card_refs_v8.py',
               'Scripts/Cards/import_warmachine_model_refs_v8.py',
               'Scripts/Cards/capture_warmachine_model_refs_v8.py',
               'Scripts/Cards/finalize_warmachine_model_comic_v8.py'):
    shutil.copyfile(ROOT/script,snap/Path(script).name)

lines=['# 新版连接模型 · 射速与机动重绘 v8','',
    '用户明确开始第二个任务，使用刚完成的 v6 水平方块模型更新参考。两张由内置 imagegen 重新生成；文字逐字沿用 v7，只更新两张输入图片。用户直接审核原始生成结果，助手未做读图评价或生成后修订。','',
    '## 原始结果','',
    '[增加射速](FireRate.png) · [极速机动](HighSpeed.png)','',
    '## 实际输入','',
    '| 结果 | 输入图1：机械身份 | 输入图2：局部结构与镜头 | 原文 |','|---|---|---|---|',
    '| 增加射速 | [新版整机](References/FullModel.png) | [新版双联炮](References/FireRate_Model.png) | [FireRate.txt](Prompts/FireRate.txt) |',
    '| 极速机动 | [新版整机](References/FullModel.png) | [新版悬浮盘](References/HighSpeed_Model.png) | [HighSpeed.txt](Prompts/HighSpeed.txt) |','',
    '三个输入均为 1536×1536 的真实 UE SceneCapture。沿用 v7 相机、背景、固定曝光、无 Bloom 与无运动模糊设置；截图来源为独立预览副本，模型和纹理由 [v6 Blender 审核版](../../../TacticalStyle_20260916/WarMachine_LevelNodes_v6/README.md)导出。没有使用旧整机图混搭新局部图。','',
    'UE 预览资产：`/Game/GuLiStrike/Cards/WarMachineTarot/ModelReferences/v8/Meshes/SM_WarMachine_LevelNodes_Reference`。复用原 Cel 生产脚本的同一材质构建逻辑和分档光影，载入本版图集/线稿遮罩。正式战斗网格未覆盖；临时截图 Actor 已销毁并独立查询确认为零。','',
    '## 完整文字 prompt（实际调用原文）','']
for card in cards:
    lines += ['### '+card['title'],'',f"SHA256：`{card['prompt_sha256']}`；与 v7 文件及实际调用字符串 UTF-8 字节完全一致。",'',
              '```text',card['prompt'].rstrip('\n'),'```','']
lines += ['## 检查与边界','',
    '- 已核对模型来源、导入保存结果、截图参数、PNG 文件头/尺寸和哈希；未读取或评价图像内容。',
    '- 两个原始结果均为 1024×1536，复制后 SHA256 与工具输出一致；所有输入截图哈希均不同于 v7。',
    '- 模型使用授权不自动等于两张卡牌美术通过。卡图审核待用户反馈。',
    '- 增加导弹伤害继续使用 [v7 原图](../ModelComic_v7/MissileDamage.png)。本轮没有拆层、替换 UE 卡牌、运行游戏或原生编译。','',
    '[实际调用参数](generation-requests.json) · [来源与结果清单](generation-manifest.json) · [原文哈希](prompt-integrity.json) · [UE截图参数](References/capture-manifest.json) · [导出记录](ModelPreview/export-manifest.json) · [导入记录](ModelPreview/import-manifest.json)','']
(OUT/'README.md').write_text('\n'.join(lines),encoding='utf-8')
print(json.dumps({'success':True,'cards':[{'id':x['id'],'output':x['output'],'prompt_unchanged':True} for x in cards]},ensure_ascii=False))
