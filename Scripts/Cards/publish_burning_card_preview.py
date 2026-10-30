"""Publish the two-card HTML and a portable offline edition, without UE import."""
from pathlib import Path
from datetime import datetime,timezone,timedelta
import ast
import base64
import hashlib
import json
import shutil
import subprocess
import zipfile
import numpy as np
from PIL import Image

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'ArtSource/UI/GroundMechRogueCards/BurningCards_HTML_20261029_v1'


def sha(path):return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def audit_source_contract(build):
    """Audit generated source provenance and assembly policy, not appearance."""
    assert build['revision']=='rigid-objects-v4'
    assert build['assembly_policy']['complete_generated_layers']
    assert build['assembly_policy']['original_rgb_paste'] is False
    assert build['assembly_policy']['ownership_mask_alpha_clip'] is False
    assert build['assembly_policy']['motion_model']=='rigid-translation'
    assert build['assembly_policy']['spatial_uv_warp'] is False
    audit={'scope':'source file hashes, generated-layer provenance, alpha, common canvas and assembly policy; no rendered-image or visual comparison',
        'revision':build['revision'],'build_sha256':sha(OUT/'build.json'),'cards':{}}
    records=json.loads((OUT/'generation-results.json').read_text(encoding='utf-8'))
    active={r['id']:r for r in records if r.get('status')=='active'}
    assert len(active)==12
    for key,card in build['cards'].items():
        reference=json.loads((OUT/key/'reference.json').read_text(encoding='utf-8'))
        assert sha(OUT/card['reference'])==reference['sha256']==card['reference_sha256']
        consumed=[n for p in card['passes'] for n in ([p['layer'],p['host_layer']] if p.get('host_layer') else [p['layer']])]
        assert len(card['layers'])==6 and sorted(consumed)==list(range(1,7))
        assert not any('mask' in p or 'part' in p for p in card['passes'])
        if key=='Burst':
            ring=[p for p in card['passes'] if p['layer']==3]
            assert len(ring)==1 and ring[0]['host_layer']==4
            assert card['layers'][2]['generated_fill_registration']['raw_border_alpha_max']<32
        layers=[]
        object_ids=set()
        for layer in card['layers']:
            n=layer['index'];record=active[f'{key}_{n}']
            assert layer['complete_painting_used'] and not layer['original_pixel_paste'] and not layer['ownership_mask_applied']
            assert sha(OUT/layer['file'])==layer['sha256']
            assert sha(OUT/layer['raw_generated_copy'])==layer['raw_sha256']==record['sha256']
            assert not object_ids.intersection(layer['object_ids'])
            object_ids.update(layer['object_ids'])
            with Image.open(OUT/layer['file']) as image:
                assert image.mode=='RGBA' and image.size==(1536,1536)
                alpha=list(image.getchannel('A').getextrema())
                assert alpha==layer['alpha_extrema']
                assert alpha==[255,255] if n==6 else alpha[0]==0 and alpha[1]>0
            layers.append({'layer':n,'generated_raw_sha256':record['sha256'],'final_sha256':layer['sha256'],
                'complete_painted_layer':True,'original_rgb_paste':False,'old_ownership_alpha_clip':False,
                'actual_alpha_extrema':alpha,'registration':layer['generated_fill_registration']['method'],
                'declared_object_ids':layer['object_ids'],'motion_owner_layer':layer['motion_owner_layer']})
        audit['cards'][key]={'reference_file_sha256':reference['sha256'],'layer_count':6,
            'source_window':[256,256,1280,1280],'layers':layers,'hidden_surface_visual_review':'pending_user',
            'declared_object_ids_unique':True,'object_visual_uniqueness':'pending_user_review',
            'all_six_textures_consumed_once':True,'spatial_uv_warp':False}
        if key=='Bullet':assert card['layers'][2]['motion_owner_layer']==4
    audit['current_generated_layers']=len(active)
    audit['original_rgb_paste']=False
    audit['old_ownership_alpha_clip']=False
    audit['status']='passed'
    (OUT/'source-contract.json').write_text(json.dumps(audit,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    return audit


def main():
    build=json.loads((OUT/'build.json').read_text(encoding='utf-8'))
    requests=json.loads((OUT/'generation-requests.json').read_text(encoding='utf-8'))
    results=json.loads((OUT/'generation-results.json').read_text(encoding='utf-8'))
    cards=build['cards']
    source_audit=audit_source_contract(build)
    assets=set()
    for card in cards.values():
        assets.add(card['reference'])
        for layer in card['layers']:
            path=OUT/layer['file'];assets.add(layer['file']);assert sha(path)==layer['sha256']
            with Image.open(path) as image:
                assert image.size==(1536,1536)
                alpha=image.getchannel('A').getextrema()
                assert alpha==(255,255) if layer['index']==6 else alpha[0]==0 and alpha[1]>0
        assets.update(p['mask'] for p in card['passes'] if p.get('mask'))
    for script in ['prepare_burning_card_layers.py','prepare_burning_card_repairs.py','prepare_burning_card_rigid_repair.py','record_burning_card_repair.py','complete_burning_card_layers.py','build_burning_card_rigid_layers.py','build_burning_card_layers.py','publish_burning_card_preview.py']:
        ast.parse((ROOT/'Scripts/Cards'/script).read_text(encoding='utf-8'))
    result=subprocess.run([shutil.which('node'),'--check',str(ROOT/'Scripts/Cards/burning_cards_preview.js')],capture_output=True,text=True)
    assert result.returncode==0,result.stderr
    css=(ROOT/'Scripts/Cards/burning_cards_preview.css').read_text(encoding='utf-8')
    js=(ROOT/'Scripts/Cards/burning_cards_preview.js').read_text(encoding='utf-8')
    assert 'uShift*pin' not in js and 'smoothstep' not in js
    template=(ROOT/'Scripts/Cards/burning_cards_preview.html').read_text(encoding='utf-8')
    data={'cards':cards,'standalone':False,'version':build['revision']}
    def page(payload):
        return template.replace('__STYLES__',css).replace('__DATA__',json.dumps(payload,ensure_ascii=False).replace('</','<\\/')).replace('__SCRIPT__',js)
    (OUT/'index.html').write_text(page(data),encoding='utf-8')
    offline=dict(data,standalone=True,embedded={name:'data:image/png;base64,'+base64.b64encode((OUT/name).read_bytes()).decode('ascii') for name in sorted(assets)})
    (OUT/'BurningCards_Standalone.html').write_text(page(offline),encoding='utf-8')
    static={'checked_at':datetime.now(timezone(timedelta(hours=8))).isoformat(),
        'python_syntax':'passed','javascript_syntax':'passed','layer_files':12,'layers_per_card':6,
        'canvas':[1536,1536],'source_window':[256,256,1280,1280],
        'alpha':'first five layers true RGBA, sixth fully opaque, for each card',
        'source_reference_hashes':{k:v['reference_sha256'] for k,v in cards.items()},
        'offline_html_embedded_images':len(assets),'assistant_visual_test':False,
        'browser_shader_execution':'pending','user_visual_review':'pending','ue_import':'not_run',
        'source_contract':'source-contract.json','current_generated_layers':source_audit['current_generated_layers'],
        'original_rgb_paste':False,'ownership_alpha_clip':False,
        'motion_model':'rigid-translation','spatial_uv_warp':False,'ring_baked_cut_mask':False,
        'object_registry':'unique declared object owners; actual picture content awaits user review',
        'source_data_verification':'real generation provenance, file hashes, alpha, common canvas and complete-layer assembly policy',
        'native_composition_resolution':[1024,1024]}
    browser_file=OUT/'browser-check.json'
    if browser_file.is_file():
        browser=json.loads(browser_file.read_text(encoding='utf-8'))
        if browser.get('build_sha256')==sha(OUT/'build.json') and browser.get('preview_revision')==build['revision'] and browser.get('preview_html_sha256')==sha(OUT/'index.html'):
            static['browser_shader_execution']='passed: current complete-layer build and native 1024 framebuffer initialized'
            static['browser_check']='browser-check.json'
    (OUT/'static-check.json').write_text(json.dumps(static,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    manifest={**build,'state':'rigid-object-repair-ready-for-user-review',
        'generation_requests':'generation-requests.json','generation_results':'generation-results.json',
        'preview':{'file':'index.html','url':'http://127.0.0.1:8192/','offline':'BurningCards_Standalone.html',
            'default_mode':'flat','default_card':'Burst','default_parallax_px':6,'maximum_parallax_px':14,
            'default_tilt_degrees':10,'motion_model':'rigid-translation','spatial_uv_warp':False,'source_gutter_pixels':256},
        'user_correction':'两卡取消果冻式局部拉伸；同物件只归属一个内容层，附着火焰跟随物件；冲击圈保持完整。燃烧弹体及附着火焰同层。',
        'palette_policy':'Preserve user-selected reference colors and flat three-tone linework; no recoloring.',
        'acceptance':{'standard':'reference elements present, correct six layer groups, principal composition retained; hidden surfaces and edges require user review',
            'pixel_exactness_required':False,'user_instruction':'不用像素级还原，该有的元素都有就算通过'},
        'validation':static,'source_contract':'source-contract.json',
        'formal_art_replaced':False,'ue_imported':False,'gameplay_modified':False}
    (OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    readme='# 灼烧两卡 · 单物件归属与刚性视差 v4\n\n根据用户对冲击圈被切割、同物件跨层及两张卡果冻式变形的反馈，当前预览已更新为页头04。元素与主要构图沿用选定参考，不要求像素级还原；画面仍待用户验收。\n\n## 打开\n\n- [在线页面](index.html)，本机地址 http://127.0.0.1:8192/\n- [离线单文件HTML](BurningCards_Standalone.html)，已内嵌两卡十二张当前图层与两张原图。\n- [完整素材包](BurningCards_Package.zip)，含输入、提示词、真实原始输出、来源清单和当前页面。\n\n每组支持单独查看、开关、透明度和PNG下载，页面支持原图对照、原位叠合、交互视差及叠合PNG导出。可先单独查看冲击圈、碎片、机甲、背景，再开启视差检查。\n\n## 本次修复\n\n1. 两张卡统一采用整物件刚性平移。移除原先边缘少动、内部多动的UV渐变，物件内所有像素接收同一位移；依靠共同画布的外扩留量提供边缘覆盖，没有降低原有14px最大视差。\n2. 冲击圈重新生成完整椭圆及左右转折，源图四边有透明留白；装配后左右转折位于可见窗口外，消除旧版把源图切口缩到画面内部的问题。仍是一张完整纹理。WebGL在一次局部合成中按真实机甲alpha处理前后遮挡，不使用两份独立环图、前后独立深度或预裁切矩形蒙版。\n3. 重绘爆裂卡近景灰石和背景，前景石块专属第二层，背景补成连续草地；重绘子弹卡碎片，排除属于中央机甲的连接腿、机身、枪座和天线。物件归属写入当前构建的object_ids，禁止同一声明ID同时属于不同内容层。物件是否仍被模型误画到别层，交用户逐层审核。\n4. 燃烧弹体和附着火焰保留第一层。独立命中火焰仍为第三个语义分组，但跟随受击机甲使用同一刚性运动，接触点不因不同深度位移而拉开。机甲本体、腿、炮与天线整体留在机甲层。\n\n本轮通过内置imagegen定向重绘4张，真实原始输出均为1254×1254，另外8张保留上一版已装配文件及其哈希，没有重新生成。最终仍为两卡各6张1536×1536 PNG，共用源窗口[256,256,1280,1280]和同一基础映射；前五层有真实透明，第六层不透明。\n\n## 六层\n\n图1：飞溅燃烧碎片、独立近景灰石、完整冲击圈、爆燃机甲（含连接部件与附着燃烧）、远处机甲、草地与远景。\n\n图2：燃烧子弹及附着火焰、独立燃烧碎片、命中爆燃、受击机甲（含连接部件）、远处射手机甲及弹道、草地与远景。\n\n一个完整物件只拥有一个纹理归属。冲击圈与机甲是两个物件，以完整源纹理做前后遮挡合成；不把一个环拆成多个能独立移动的图层。Canvas2D兼容路径也共用同一环源图和同一个刚性位移。\n\n## 记录与历史\n\n- [本轮完整提示词与编辑输入](Repairs/RigidObjects_v4/requests.json)，其中revision=4的4项为本轮新请求，其他8项为已有输入记录。\n- [当前12层真实来源](Repairs/RigidObjects_v4/results.json)，reused_from_revision标记复用项，不冒充新生成。\n- [当前图层归属、位置与渲染配置](build.json)\n- [来源与组装约束核对](source-contract.json)\n- [页面技术核对](browser-check.json)\n\n本轮Inputs/为必要编辑输入，Prompts/为完整请求文本，Raw/为真实工具输出；Feedback/只保存用户提供的问题图片，没有助手截图。其他历史目录及旧Mask文件仅用于归档，当前页面不引用旧裁切蒙版。\n\n修改前备份位于同级BurningCards_HTML_History/BeforeRigidObjects_20261029_v4.zip，SHA256为6304fdbb84bcffc8d6d6a8a3a28de23b1c9ed32671d4ac991dfe5b8dab04f206。\n\n## 核对边界\n\n只核对来源、实际尺寸、alpha、哈希、坐标／位移参数、单归属声明、脚本语法、页面资源和着色器／控件状态。没有助手结果读图、截图、录屏、渲染像素读取或图片对比。技术核对不证明图中完全没有重复物件、接缝或透视偏差，也不替代用户对果冻效果和遮挡的验收。\n\n本轮不改玩法、Excel、UE正式资源或Map，不构建、不启动PIE，不新增自动化测试。\n'
    (OUT/'README.md').write_text(readme,encoding='utf-8')
    bundle=OUT/'BurningCards_Package.zip'
    with zipfile.ZipFile(bundle,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for file in sorted(OUT.rglob('*')):
            if file.is_file() and file!=bundle and file.suffix not in {'.log','.pid'}:
                archive.write(file,file.relative_to(OUT).as_posix())
    print(json.dumps({'preview':(OUT/'index.html').as_posix(),'offline_bytes':(OUT/'BurningCards_Standalone.html').stat().st_size,
        'layers':12,'package_bytes':bundle.stat().st_size,'technical_checks':'passed','visual_review':'pending_user'},ensure_ascii=False))


if __name__=='__main__':main()
