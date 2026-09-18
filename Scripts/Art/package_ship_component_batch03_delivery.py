"""Package the approved Batch03 Blender/FBX handoff, preserving review evidence."""
import hashlib
import importlib.util
import json
import shutil
import zipfile
from datetime import datetime, timezone
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[2]
ROOT = PROJECT / 'ArtSource/Ships/ShipComponentStyle_20260917/Batch03'
OUT = ROOT / 'Delivery/v3'
ZIP = ROOT / 'ShipComponentStyle_Batch03_20260917_v3_Delivery.zip'
KEYS = ('Drone_LaunchBay', 'Electronic_JammingDevice', 'Shield_Generator')
LABELS = ('无人机发射舱', '电子干扰装置', '护盾发生器')
spec = importlib.util.spec_from_file_location('existing_delivery_package', PROJECT / 'Scripts/Art/package_ship_component_material_delivery.py')
existing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(existing)
read, dump, digest, copy, Links = existing.read, existing.dump, existing.digest, existing.copy, existing.Links


def prepare():
    approval = read(ROOT / 'approval_B_20260917.json')
    review = read(ROOT / 'Production/v3/Review_B_Manifest.json')
    assert approval['decision'] == 'approved' and approval['gate'] == 'B'
    assert digest(ROOT / approval['review_manifest']) == approval['review_manifest_sha256']
    assert digest(ROOT / approval['review_page']) == approval['review_page_sha256']
    assert read(OUT / 'Validation/FBX_Readback_Summary.json')['passed']
    assert read(OUT / 'Validation/Portable_Blender_Readback.json')['passed']
    for name in ('approval_A_20260917.json', 'approval_B_20260917.json', 'surface_revision_20260917_exterior_marking.json'):
        copy(ROOT / name, OUT / 'Parameters' / name)
    for name in ('Review_B_Manifest.json', 'Drone_LaunchBay_launch_markings.json', 'Drone_LaunchBay_launch_marking.svg'):
        copy(ROOT / 'Production/v3' / name, OUT / 'Parameters' / name)
    copy(ROOT / 'Source/source_snapshot_v1.json', OUT / 'Parameters/source_snapshot_v1.json')
    copy(ROOT / 'References/reference_A_manifest_v3.json', OUT / 'Parameters/reference_A_manifest_v3.json')
    copy(ROOT / 'Production/v3/saved_blender_readback.json', OUT / 'Validation/ApprovedSource_Blender_Readback.json')
    for name in ('Drone_LaunchBay_User_ExitDirection_20260917.png', 'Drone_LaunchBay_User_ExteriorMarking_20260917.png'):
        copy(ROOT / 'References' / name, OUT / 'References' / name)
    for source in (ROOT / 'Source/Previews').glob('*_original_*_v1.png'):
        copy(source, OUT / 'References' / source.name)
    for key in KEYS:
        part = approval['parts'][key]
        assert digest(ROOT / part['approved_blend']) == part['approved_blend_sha256']
        assert digest(ROOT / part['hero']) == part['hero_sha256']
        for item in part['textures']:
            assert digest(ROOT / item['path']) == digest(OUT / 'Textures' / Path(item['path']).name) == item['sha256']
        ref = review['parts'][key]['reference']
        assert digest(ROOT / ref['path']) == ref['sha256']
        copy(ROOT / ref['path'], OUT / ref['path'])
        copy(ROOT / f'Production/v3/{key}_material_report.json', OUT / f'Validation/{key}_ApprovedSource_Audit.json')
        copy(ROOT / f'Production/v3/{key}_paint_regions.json', OUT / f'Parameters/{key}_paint_regions.json')
        params = read(OUT / f'Parameters/{key}_material_parameters.json')
        params['approval_B'] = 'approved v3; approval_B_20260917.json'
        params['delivery_UV'] = {'Blender': 'Original UV order retained, named SC_PaintUV and SC_LineUV appended',
            'FBX': 'UV0 SC_PaintUV, UV1 SC_LineUV, followed by all unchanged original UV channels'}
        params['FBX_preview_material'] = {'shader': 'Principled BaseColor preview', 'roughness': .64, 'metallic': .2,
            'actual_roughness_metallic': 'Use per-pixel ORM G/B for final material reconstruction',
            'toon_and_lineart': 'Retained in Blender; reconstruct in target engine using supplied texture/parameter data'}
        params['FBX_UV_channels'] = read(OUT / f'Validation/{key}_FBX_Readback.json')['uv_names']
        dump(OUT / f'Parameters/{key}_material_parameters.json', params)
    # The shared reader covers static and skeletal deliveries. Specialize only
    # its descriptive socket-storage text; all measurements/pass fields remain.
    summary = read(OUT / 'Validation/FBX_Readback_Summary.json')
    for row in summary['checks']:
        row['socket_storage'] = 'FBX custom property GuLiStrike_Sockets_JSON=[]; original static mesh has no sockets or markers'
        dump(OUT / f"Validation/{row['key']}_FBX_Readback.json", row)
    dump(OUT / 'Validation/FBX_Readback_Summary.json', summary)
    for source in ('Scripts/Blender/export_ship_component_batch03_delivery.py',
                   'Scripts/Blender/export_ship_component_material_delivery.py',
                   'Scripts/Art/package_ship_component_batch03_delivery.py',
                   'Scripts/Art/package_ship_component_material_delivery.py',
                   'ArtSource/Buildings/IndustrialDefenseSet/scripts/validate_exports.py'):
        copy(PROJECT / source, OUT / 'Tools' / Path(source).name)
    for source in (ROOT / 'Production/v3/Tools').glob('*.py'):
        copy(source, OUT / 'Tools' / source.name)
    write_readme(summary)
    write_page()


def write_readme(summary):
    rows = '\n'.join(f'| {label} | {row["body_triangles"]} | [静态 FBX]({Path(row["fbx_file"]).as_posix()}) | [Blender](Blender/SC_{key}_Styled.blend) |'
        for key, label, row in zip(KEYS, LABELS, summary['checks']))
    error = max(row['max_geometry_error_m'] for row in summary['checks'])
    text = f'''# Ship 第三批支援组件 v3 — 最终交付

用户已明确“三件均通过 B，导出 FBX”。无人机保留薄壁外侧白色僚机与赭黄箭头，干扰装置采用深蓝白黄，护盾采用绿色；原模型几何、原点、法线和安装尺寸保持。规范1.0，交付范围为Blender与FBX。

[交付总览](Review.html) · [文件与哈希清单](Delivery_Manifest.json) · [审核B决定](Parameters/approval_B_20260917.json)

| 组件 | LOD0主体三角面 | FBX | 可编辑源 |
|---|---:|---|---|
{rows}

[总览Blender](Blender/ShipComponentStyle_Overview.blend)包含三个独立Scene，切换Scene查看各件，保留各自原始原点、物体变换及尺寸。每份FBX只有一个主体网格、一个材质槽，没有额外描边壳、摄影棚、骨骼或演示动画。

## 贴图、框线和光影

- 每件BaseColor、ORM、LineMask各一张2048×2048，共九张，内嵌Blender并在Textures另存PNG。材质与贴图相对路径可随整个交付目录搬移。
- BaseColor为sRGB纯色及标识，不包含场景阴影；ORM为Non-Color，R=1（没有烘焙AO）、G=粗糙度、B=金属度；LineMask为Non-Color独立内线。
- Blender使用EEVEE场景光照和自阴影驱动三档明暗。内线节点`Line_Strength`可关闭；外轮廓在`OUTLINE_TOGGLE`集合，描边壳面数与各主体相同，单独计数。
- Blender保留全部原UV并追加SC_PaintUV和SC_LineUV；FBX以UV0=SC_PaintUV、UV1=SC_LineUV，其后保留原UV。护盾有四个原UV通道，另两件各三个。
- FBX基础材质用于BaseColor预览。完整三渲二光影、内线和外轮廓保留在Blender，后续引擎按各件`Parameters/*_material_parameters.json`及贴图重建。

## 无人机标识与静态接口

标识在薄长侧壁朝外的-X面，原顶面和厚侧壁旧标识已移除；飞出方向为Blender +Y / UE -Y。见[外侧效果](Previews/Drone_LaunchBay_exterior_hero.png)、[用户标注](References/Drone_LaunchBay_User_ExteriorMarking_20260917.png)、[可编辑SVG](Parameters/Drone_LaunchBay_launch_marking.svg)及[落点记录](Parameters/Drone_LaunchBay_launch_markings.json)。图标只在贴图中，不增加模型或Socket。

三件均为静态模型，原网格没有骨骼或Socket。每件`Parameters/*_Sockets.json`与[原型快照](Parameters/source_snapshot_v1.json)保留原Blueprint、模型引用、安装变换和兼容挂点。无人机的`wingman_bay_1/2`是Blueprint兼容舰体挂点名，不是网格Socket；另两件该数组为空。FBX在`GuLiStrike_Sockets_JSON`记录空Socket列表，在`GuLiStrike_CompatibleSockets_JSON`保留兼容名。

## 检查与版本

[FBX回读](Validation/FBX_Readback_Summary.json)复用既有项目工具，三件尺寸、原点、几何、UV、面数、材质槽和静态接口均通过。最大几何误差{error:.9f}米，原点误差和UV数值误差均为0。[可迁移Blender回读](Validation/Portable_Blender_Readback.json)确认三件与总览、内嵌/外部贴图和三渲二节点有效。

获批Production/v3源与审核页保持原哈希。交付Blender仅调整批准元数据及相对资源路径；Parameters中的历史候选清单和ApprovedSource报告保留当时状态，当前状态以B决定及Delivery_Manifest为准。历史清单路径相对于项目Batch03制作根，交付本身的当前路径以本页和最终清单为准。Tools保存制作与导出脚本副本，重建时从项目Scripts入口运行。

本轮UE正式接入及运行验证未运行。CIWS与第二批成品未修改。
'''
    (OUT / 'README.md').write_text(text, encoding='utf-8')


def write_page():
    page = (ROOT / 'Review_B_v3.html').read_text(encoding='utf-8')
    page = page.replace('Ship 第三批 · 实际材质审核 B', 'Ship 第三批 · v3 最终交付')
    page = page.replace('支援组件实际材质成品', '支援组件最终交付')
    page = page.replace('成品审核 B · 材质候选 v3', '审核 B 已通过 · 材质 v3 · 三套静态 FBX 已导出')
    page = page.replace('B 待审核', 'B 已通过')
    page = page.replace('A 已通过 / B 待用户审核', 'A / B 已通过 · FBX 回读通过')
    page = page.replace('本次参考 A 由“开始实施”放行。待你审核实际成品 B 后导出最终 FBX；UE 正式接入另行安排。',
        '三件材质 v3 已获审核 B 通过，静态 FBX 已导出并完成回读；UE 正式接入另行安排。')
    page = page.replace('Production/v3/ShipComponentStyle_Batch03_Overview.blend', 'Blender/ShipComponentStyle_Overview.blend')
    page = page.replace('Production/v3/Review_B_Manifest.json', 'Delivery_Manifest.json')
    page = page.replace('Production/v3/Drone_LaunchBay_launch_marking.svg', 'Parameters/Drone_LaunchBay_launch_marking.svg')
    page = page.replace('href="approval_A_20260917.json"', 'href="Parameters/approval_A_20260917.json"')
    page = page.replace('href="Review_A_v3.html">已审参考图板', 'href="Parameters/reference_A_manifest_v3.json">参考A来源清单')
    page = page.replace('Previews/v3/', 'Previews/')
    for key in KEYS:
        page = page.replace(f'Production/v3/{key}_OriginalMesh_MaterialCandidate.blend', f'Blender/SC_{key}_Styled.blend')
        page = page.replace(f'Production/v3/{key}_material_report.json', f'Validation/{key}_ApprovedSource_Audit.json')
        for suffix in ('material_parameters', 'paint_regions'):
            page = page.replace(f'Production/v3/{key}_{suffix}.json', f'Parameters/{key}_{suffix}.json')
        page = page.replace(f'<a href="Blender/SC_{key}_Styled.blend">',
            f'<a href="FBX/SM_SC_{key}_Styled.fbx">最终静态 FBX</a><a href="Blender/SC_{key}_Styled.blend">')
    page = page.replace('<p><strong>可编辑 Blender 源</strong></p>',
        '<p><strong>最终交付文件</strong></p><div class="links"><a href="README.md">使用说明</a><a href="Parameters/approval_B_20260917.json">B通过记录</a><a href="Validation/FBX_Readback_Summary.json">FBX回读</a></div>')
    (OUT / 'Review.html').write_text(page, encoding='utf-8')


def seal():
    page = Links()
    page.feed((OUT / 'Review.html').read_text(encoding='utf-8'))
    for link in set(page.links) - {'Delivery_Manifest.json'}:
        assert (OUT / link.split('#')[0]).is_file(), link
    # Enumerate only this small, dedicated delivery directory.
    files = sorted(p for p in OUT.rglob('*') if p.is_file() and p.name != 'Delivery_Manifest.json')
    assert not any(p.suffix in ('.blend1', '.uasset', '.umap', '.uexp', '.ubulk', '.pak') for p in files)
    counts = {suffix: sum(p.suffix == suffix for p in files) for suffix in ('.blend', '.fbx', '.png', '.mp4')}
    assert counts['.blend'] == 4 and counts['.fbx'] == 3 and counts['.mp4'] == 0
    assert len(list((OUT / 'Textures').glob('*.png'))) == 9
    approval = read(ROOT / 'approval_B_20260917.json')
    parts = {}
    for key, label in zip(KEYS, LABELS):
        expected = read(OUT / f'Validation/{key}_export_expected.json')
        report = read(OUT / f'Validation/{key}_FBX_Readback.json')
        assert report['passed'] and digest(OUT / report['fbx_file']) == report['fbx_sha256']
        assert digest(OUT / expected['final_blend']) == expected['final_blend_sha256']
        assert digest(ROOT / approval['parts'][key]['approved_blend']) == approval['parts'][key]['approved_blend_sha256']
        parts[key] = {'label': label, 'version': 'v3', 'A': 'approved', 'B': 'approved',
            'approved_source': approval['parts'][key], 'final_blend': expected['final_blend'],
            'final_blend_sha256': expected['final_blend_sha256'], 'FBX_readback': report,
            'interface': read(OUT / f'Parameters/{key}_Sockets.json'),
            'material_parameters': f'Parameters/{key}_material_parameters.json', 'motion': 'not_applicable_static'}
    manifest = {'schema': 'gulistrike-ship-delivery/v1', 'work_id': 'WORK-20260917-005', 'art_revision': '1.0',
        'assembled_at': datetime.now(timezone.utc).isoformat(), 'status': 'delivered_Blender_FBX',
        'user_B': approval['user_reply'], 'version': 'v3', 'parts': parts,
        'scope': 'Original static geometry; editable Blender, 3 static FBX, 9 2K textures and render previews',
        'UE_formal_validation': 'not_run', 'counts': counts, 'html_local_reference_count': len(set(page.links)),
        'files': [{'path': p.relative_to(OUT).as_posix(), 'bytes': p.stat().st_size, 'sha256': digest(p)} for p in files]}
    dump(OUT / 'Delivery_Manifest.json', manifest)
    prefix = 'ShipComponentStyle_Batch03_v3/'
    with zipfile.ZipFile(ZIP, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in files + [OUT / 'Delivery_Manifest.json']:
            archive.write(path, prefix + path.relative_to(OUT).as_posix())
    with zipfile.ZipFile(ZIP) as archive:
        assert archive.testzip() is None
        for row in manifest['files']:
            assert hashlib.sha256(archive.read(prefix + row['path'])).hexdigest() == row['sha256']
    record = {'passed': True, 'archive': ZIP.name, 'archive_sha256': digest(ZIP), 'archive_bytes': ZIP.stat().st_size,
        'manifest_sha256': digest(OUT / 'Delivery_Manifest.json'), 'files_including_manifest': len(files) + 1,
        'zip_crc_and_all_file_sha256': 'passed', 'approved_sources_preserved': True, 'counts': counts,
        'user_A': 'approved', 'user_B': 'approved', 'UE_formal_validation': 'not_run'}
    dump(ROOT / 'delivery_validation_v3.json', record)
    print(json.dumps(record, ensure_ascii=False))


if __name__ == '__main__':
    prepare()
    seal()
