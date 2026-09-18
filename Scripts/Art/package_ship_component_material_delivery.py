"""Package the B-approved Ship material delivery without changing review evidence."""
import hashlib
import json
import re
import shutil
import zipfile
from datetime import datetime, timezone
from html.parser import HTMLParser
from pathlib import Path

PROJECT=Path('D:/UE5.7/test1')
ROOT=PROJECT/'ArtSource/Ships/ShipComponentStyle_20260917'
OUT=ROOT/'Delivery/v4'
ZIP=ROOT/'ShipComponentStyle_20260917_v4_Delivery.zip'
KEYS=('Twin_Barrel_Turret','CIWS','Thor_MissilePod')
LABELS=('双联炮','CIWS','Thor 一级导弹舱')

def read(path):return json.loads(path.read_text(encoding='utf-8'))
def dump(path,data):path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
def digest(path):
    with path.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def copy(source,destination):
    destination.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(source,destination)

def prepare():
    approval=read(ROOT/'approval_B_20260917.json')
    assert approval['decision']=='approved'
    assert digest(ROOT/'review_B_material_manifest_v4.json')==approval['review_manifest_sha256']
    for key,part in approval['parts'].items():
        assert digest(ROOT/part['approved_blend'])==part['approved_blend_sha256']
        assert digest(ROOT/part['hero'])==part['hero_sha256']
    assert read(OUT/'Validation/FBX_Readback_Summary.json')['passed']
    assert read(OUT/'Validation/Portable_Blender_Readback.json')['passed']
    for filename in ('approval_A_20260917.json','approval_B_20260917.json',
                     'review_B_material_manifest_v4.json','material_scope_and_connection_fix_v4.json'):
        copy(ROOT/filename,OUT/'Parameters'/filename)
    copy(ROOT/'Source/source_snapshot_v1.json',OUT/'Parameters/source_snapshot_v1.json')
    copy(ROOT/'Production/v4/saved_blender_readback_v4.json',OUT/'Validation/ApprovedSource_Blender_Readback.json')
    for key in KEYS:
        copy(ROOT/'Production/v4'/(key+'_material_report.json'),OUT/'Validation'/(key+'_ApprovedSource_Audit.json'))
    for filename in ('Twin_Barrel_Turret_Design_v1.png','CIWS_Design_v1.png','Thor_MissilePod_Design_v3.png',
                     'Ship_Style_Anchor_20260916.png'):
        copy(ROOT/'References'/filename,OUT/'References'/filename)
    for image in (ROOT/'Source/Previews').glob('*_original_*_v1.png'):
        copy(image,OUT/'References'/image.name)
    copy(ROOT/'Previews/v3/CIWS_hero.png',OUT/'References/CIWS_v3_superseded.png')
    copy(ROOT/'Feedback/CIWS_connection_user_markup_20260917.png',OUT/'References/CIWS_connection_user_markup_20260917.png')
    interface=(ROOT/'InterfaceSpec_v1.md').read_text(encoding='utf-8').replace('Source/source_snapshot_v1.json','source_snapshot_v1.json')
    (OUT/'Parameters/InterfaceSpec_v1.md').write_text(interface,encoding='utf-8')
    params=read(OUT/'Parameters/MaterialParameters_v4.json')
    params['approval_B']='approved v4; see approval_B_20260917.json'
    params['delivery_UV']={'Blender':'Original UV channel order retained; material uses named SC_PaintUV and SC_LineUV',
                         'FBX':'UV0=SC_PaintUV; UV1=SC_LineUV; all original channels follow unchanged'}
    params['FBX_material']={'shader':'Principled BaseColor preview fallback only','roughness':0.64,'metallic':0.2,
                           'final_surface_parameters':'Use provided ORM per-pixel G roughness/B metallic',
                           'outline':'Separate editable Blender render shell, excluded from FBX main-body export',
                           'sockets':'GuLiStrike_Sockets_JSON custom property and per-component *_Sockets.json sidecar',
                           'demo_animation':'Retained in Blender and MP4; not baked into FBX'}
    for key in KEYS:
        report=read(OUT/'Validation'/(key+'_FBX_Readback.json'))
        params['parts'][key]['fbx_uv_channels']=report['uv_names']
    dump(OUT/'Parameters/MaterialParameters_v4.json',params)
    readme='''# Ship 三组件材质样板 v4 — 最终交付

2026-09-17，三件均获用户审核 B 通过：“三件均通过 B，导出 FBX”。规范版本 1.0。原模型的顶点、拓扑、法线、尺寸、机械绑定及安装原点保留；本轮修改渲染、贴图、结构内线和可关闭外轮廓。

打开 [交付总览](Review.html) 查看实际渲染、三视图、灰模、光向对照和俯仰演示。

## 文件

| 组件 | LOD0 主体三角面 | FBX | 可编辑源 |
|---|---:|---|---|
| 双联炮 | 1680 | [骨骼 FBX](FBX/SKM_SC_Twin_Barrel_Turret_Styled.fbx) | [Blender](Blender/SC_Twin_Barrel_Turret_Styled.blend) |
| CIWS | 1066 | [骨骼 FBX](FBX/SKM_SC_CIWS_Styled.fbx) | [Blender](Blender/SC_CIWS_Styled.blend) |
| Thor 一级导弹舱 | 2080 | [静态 FBX](FBX/SM_SC_Thor_MissilePod_Styled.fbx) | [Blender](Blender/SC_Thor_MissilePod_Styled.blend) |

[总览 Blender](Blender/ShipComponentStyle_Overview.blend) 含三个独立 Scene；在 Scene 选择器切换，保留各自原尺寸和原点。两门炮使用已冻结的既有骨骼源，含之前完成的俯仰避让；本轮没有新增几何改造。以上面数对应该既有骨骼源，不能与最早静态参考面数混用。每份 FBX 只有一个主体网格、一个材质槽。

## Blender 与贴图

使用 Blender 5.2.2 LTS、EEVEE 实际渲染。材质依场景光照与遮挡形成三档明暗，BaseColor 没有烘入光影。所有贴图已打包到 Blender，同时在 Textures 目录提供独立 PNG；保持交付目录结构即可迁移。

- 每件各有 2048×2048 BaseColor（sRGB）、ORM（Non-Color）、LineMask（Non-Color），共 9 张。
- ORM：R=1（未烘焙 AO），G=粗糙度，B=金属度。LineMask 白色为内线；材质节点 `Line_Strength` 默认 0.85，设为 0 可关闭。
- 外轮廓在 `OUTLINE_TOGGLE` 集合，关闭渲染或视口可隐藏。它是单独的描边壳，三角面数量与对应主体相同，不改变主体网格。
- Blender 保留原 UV 顺序并新增 `SC_PaintUV`、`SC_LineUV`；材质按名称引用。FBX 统一以 UV0 为绘制 UV、UV1 为内线 UV，后续通道保留全部原 UV。
- 两门炮 Blender 第 0 帧为静止展示；第 1–97 帧为连续俯仰，24 FPS。Previews 内含 MP4 和 −15°、0°、30°、75° 彩色/灰模图。FBX 输出静止绑定，不烘入演示动画。

## 骨骼、挂点与材质移交

两门炮保持 `Root → BarrelPitch`，刚性零件单骨骼权重 1。双联炮保留 2 个炮口、CIWS 保留 3 个；Thor 保留 5 组 × 3 孔及 5 个逻辑 Socket。

挂点名称、所属骨骼、原 UE 厘米坐标、旋转与缩放保存在每件 `Parameters/*_Sockets.json`，并原样嵌入 FBX 主体自定义属性 `GuLiStrike_Sockets_JSON`。Blender 内保留可编辑挂点标记。FBX 没有额外的 Socket 骨骼；后续 UE 接入时按该接口数据恢复引擎 Socket。[完整接口](Parameters/InterfaceSpec_v1.md)和[只读源快照](Parameters/source_snapshot_v1.json)包含轴心及坐标系约定。

FBX 提供 BaseColor 的基础材质预览；完整三渲二光照阴影、内线和外轮廓以 Blender 实际渲染为准，需按 [材质参数](Parameters/MaterialParameters_v4.json) 在后续 UE 材质中重建。本轮 UE 正式导入和运行验证未运行。

## 核对与审核证据

[FBX 回读](Validation/FBX_Readback_Summary.json)复用项目已有读取工具，三件尺寸、原点、UV、三角面、材质槽、骨骼层级、轴心、刚性权重及挂点元数据通过。最大回读几何误差约 0.0000069 m，原点误差为 0，UV 数值误差为 0。[可迁移 Blender 回读](Validation/Portable_Blender_Readback.json)验证原几何哈希、内嵌和外部贴图、三渲二节点及审核记录路径。

[审核 B 决定](Parameters/approval_B_20260917.json)冻结获批源与效果图哈希；[最终文件清单](Delivery_Manifest.json)记录最终交付 SHA-256。最终 Blender 只作交付元数据、相对资源路径和未使用 ORM 纹理的保留调整，获批节点与几何未改变，制作根下获批源不覆盖。Validation 中 `ApprovedSource` 报告及候选清单保留审核前时点，最终状态以本交付清单和 B 决定为准。

References 保留原型视图、已通过 A 的配色参考、风格锚点及 CIWS 连接处分色修正依据。A 图只作配色与光影参考；原模型是几何依据。
'''
    (OUT/'README.md').write_text(readme,encoding='utf-8')
    page=(ROOT/'Review_B_Materials_v4.html').read_text(encoding='utf-8')
    page=page.replace('v4 审核','v4 最终交付').replace('材质候选 v4','材质 v4 · 审核 B 已通过')
    page=page.replace('B 待审核','B 已通过').replace('当前等待实际渲染审核 B。','三件审核 B 已通过，FBX 已导出并通过回读。')
    page=page.replace('B 通过后导出最终 FBX 并回读。','三件 FBX 导出与回读已通过。')
    page=page.replace('Production/v4/ShipComponentStyle_OriginalMeshes_Overview.blend','Blender/ShipComponentStyle_Overview.blend')
    page=page.replace('Production/v4/MaterialParameters_v4.json','Parameters/MaterialParameters_v4.json')
    page=page.replace('Production/v4/README.md','README.md').replace('href="InterfaceSpec_v1.md"','href="Parameters/InterfaceSpec_v1.md"')
    for key in KEYS:
        page=page.replace('Production/v4/'+key+'_OriginalMesh_MaterialCandidate.blend','Blender/SC_'+key+'_Styled.blend')
        page=page.replace('Production/v4/'+key+'_material_report.json','Validation/'+key+'_ApprovedSource_Audit.json')
        prefix='SM' if key=='Thor_MissilePod' else 'SKM'
        anchor=f'<a href="FBX/{prefix}_SC_{key}_Styled.fbx">最终 FBX</a> · '
        page=page.replace(f'<a href="Blender/SC_{key}_Styled.blend">',anchor+f'<a href="Blender/SC_{key}_Styled.blend">')
    page=page.replace('Previews/v4/','Previews/').replace('Previews/v3/CIWS_hero.png','References/CIWS_v3_superseded.png')
    page=page.replace('Feedback/CIWS_connection_user_markup_20260917.png','References/CIWS_connection_user_markup_20260917.png')
    page=page.replace('href="material_scope_and_connection_fix_v4.json"','href="Parameters/material_scope_and_connection_fix_v4.json"')
    page=page.replace('<a href="README.md">使用说明</a>','<a href="README.md">使用说明</a><a href="Delivery_Manifest.json">交付清单</a><a href="Validation/FBX_Readback_Summary.json">FBX 回读</a>')
    (OUT/'Review.html').write_text(page,encoding='utf-8')
    root_readme='''# Ship 三组件风格样板制作

**v4 三件均通过审核 B，最终 Blender、FBX、贴图和预览已交付。** 保留原模型几何、法线、骨骼和安装接口，仅修改色块、三渲二光影与框线；CIWS 连接处误分色已修正。

- [完整交付 ZIP](ShipComponentStyle_20260917_v4_Delivery.zip)
- [最终效果与交付总览](Delivery/v4/Review.html)
- [使用说明](Delivery/v4/README.md)
- [总览 Blender](Delivery/v4/Blender/ShipComponentStyle_Overview.blend)
- [最终文件清单与哈希](Delivery/v4/Delivery_Manifest.json)
- [FBX 回读结果](Delivery/v4/Validation/FBX_Readback_Summary.json)
- [审核 B 决定](approval_B_20260917.json)

交付含两套骨骼 FBX、一套静态 FBX、三个单件 Blender 和一个总览、九张 2K 贴图、材质参数及两门炮的连续俯仰演示。FBX 挂点按原数据嵌入自定义属性并提供 JSON；完整三渲二光影与外轮廓保留在 Blender，后续 UE 按参数重建。本轮 UE 正式接入与运行验证未运行。

## 来源与版本记录

规范 1.0；双联炮赭黄、CIWS 青蓝、Thor 砖红。Thor 保留原型 5 组 × 每组 3 孔，另有 5 个逻辑 Socket。用户先通过参考 A，随后明确要求原模型不改，因此原 A 只继续作为配色与光影依据。

`Source/FBX_static_v1/` 保存本次只读静态参考；`Source/ExistingAuthoring/` 保存两门炮已有骨骼源和历史验证。原型来源、安装尺寸、骨骼和挂点以 [源快照](Source/source_snapshot_v1.json) 与 [接口清单](InterfaceSpec_v1.md) 为准。未修改或保存 UE 源资产。

`Production/v1` 几何重建和 `v2` 新增装甲已停止采用；`v3` 保留连接处错误分色与诊断证据；`Production/v4` 是获批材质源，不再覆盖。`Delivery/v4` 是最终可迁移交付。旧骨骼源已有历史俯仰避让，本轮没有新增几何改造。

- [历史 A 参考总览](Review_A_v1.html) · [A 决定](approval_A_20260917.json)
- [审核前 v4 候选页面](Review_B_Materials_v4.html) · [候选文件哈希](review_B_material_manifest_v4.json)
- [范围调整和连接修正](material_scope_and_connection_fix_v4.json)
- [当前状态索引](review_manifest_v1.json) · [源文件哈希](source_file_manifest_v1.json)

历史候选页面和报告保留当时状态；后续 B 决定与最终交付清单记录完成状态。
'''
    (ROOT/'README.md').write_text(root_readme,encoding='utf-8')
    state=read(ROOT/'review_manifest_v1.json')
    state.update(stage='delivered_Blender_FBX',user_B='approved',final_fbx_delivery='passed',
                 material_production='v4_B_approved_delivered',latest_review='Delivery/v4/Review.html',
                 B_decision=approval,final_delivery_manifest='Delivery/v4/Delivery_Manifest.json',
                 final_delivery_zip=ZIP.name,ue_formal_validation='not_run')
    for row in state['parts'].values():row['B']='approved';row['delivered_material_version']='v4'
    dump(ROOT/'review_manifest_v1.json',state)

class Links(HTMLParser):
    def __init__(self):super().__init__();self.links=[]
    def handle_starttag(self,tag,attrs):
        for name,value in attrs:
            if name in ('src','href','poster') and value and not value.startswith(('#','http:','https:','data:')):self.links.append(value)

def seal():
    page=Links();page.feed((OUT/'Review.html').read_text(encoding='utf-8'))
    # The manifest is the one self-reference created below.
    for link in set(page.links)-{'Delivery_Manifest.json'}:assert (OUT/link.split('#')[0]).is_file(),link
    for key in KEYS:
        for suffix in ('hero','front','right','rear','top','lines_off','original_geometry_gray','alternate_light','small'):
            assert (OUT/'Previews'/f'{key}_{suffix}.png').is_file()
    files=sorted(p for p in OUT.rglob('*') if p.is_file() and p.name!='Delivery_Manifest.json')
    assert not any(p.suffix in ('.blend1','.uasset','.umap','.uexp','.ubulk','.pak') for p in files)
    counts={suffix:sum(p.suffix==suffix for p in files) for suffix in ('.blend','.fbx','.png','.mp4')}
    assert counts['.blend']==4 and counts['.fbx']==3 and counts['.mp4']==2
    approval=read(ROOT/'approval_B_20260917.json')
    parts={}
    for key in KEYS:
        audit=read(OUT/'Validation'/(key+'_ApprovedSource_Audit.json'))
        expected=read(OUT/'Validation'/(key+'_export_expected.json'))
        part=read(OUT/'Validation'/(key+'_FBX_Readback.json'))
        assert part['passed'] and digest(OUT/part['fbx_file'])==part['fbx_sha256']
        assert digest(OUT/expected['final_blend'])==expected['final_blend_sha256']
        parts[key]={'version':'v4','A':'approved','B':'approved',
                    'original_source_file':audit['source_file'],'original_source_file_sha256':audit['source_file_sha256'],
                    'approved_source_sha256':approval['parts'][key]['approved_blend_sha256'],
                    'original_geometry_sha256':approval['parts'][key]['original_geometry_sha256'],
                    'original_geometry_and_normals_unchanged':audit['body_geometry_unchanged'] and audit['original_corner_normals_unchanged'],
                    'final_blend':expected['final_blend'],'final_blend_sha256':expected['final_blend_sha256'],
                    'FBX_readback':part,'motion_audit':audit.get('motion',{'status':'not_applicable_static'})}
    manifest={'schema':'guli-ship-material-delivery/v1','work_id':'WORK-20260917-003','art_revision':'1.0',
              'assembled_at':datetime.now(timezone.utc).isoformat(),'status':'delivered','user_B':approval['user_reply'],
              'scope':'Original geometry; Blender and FBX only; textures, toon lighting/shadows and outline',
              'UE_formal_validation':'not_run','counts':counts,'html_local_reference_count':len(set(page.links)),
              'parts':parts,'files':[{'path':p.relative_to(OUT).as_posix(),'bytes':p.stat().st_size,'sha256':digest(p)} for p in files]}
    dump(OUT/'Delivery_Manifest.json',manifest)
    with zipfile.ZipFile(ZIP,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for path in files+[OUT/'Delivery_Manifest.json']:
            archive.write(path,'ShipComponentStyle_v4/'+path.relative_to(OUT).as_posix())
    with zipfile.ZipFile(ZIP) as archive:
        assert archive.testzip() is None
        for row in manifest['files']:
            assert hashlib.sha256(archive.read('ShipComponentStyle_v4/'+row['path'])).hexdigest()==row['sha256']
    record={'passed':True,'archive':ZIP.name,'archive_sha256':digest(ZIP),'archive_bytes':ZIP.stat().st_size,
            'manifest_sha256':digest(OUT/'Delivery_Manifest.json'),'files_including_manifest':len(files)+1,
            'zip_crc_and_all_file_sha256':'passed','approved_sources_preserved':True,'counts':counts}
    dump(ROOT/'delivery_validation_v4.json',record)
    print(json.dumps(record,ensure_ascii=False))

if __name__=='__main__':prepare();seal()
