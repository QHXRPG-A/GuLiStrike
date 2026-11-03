"""Publish the component lookup after all sequence and asset checks pass."""
import collections
import json
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[3]
ROOT = PROJECT / 'ArtSource/UI/HUDuiPRO/Components20261030'
MANIFEST = json.loads((ROOT / 'manifest.json').read_text(encoding='utf-8'))
SEQUENCES = json.loads((ROOT / 'sequence-verification.json').read_text(encoding='utf-8'))
ASSETS = json.loads((ROOT / 'ue-verification.json').read_text(encoding='utf-8'))
RENDER = json.loads((ROOT / 'render-summary.json').read_text(encoding='utf-8'))
assert SEQUENCES['ok'] and ASSETS['ok'] and RENDER['completionVerified']
CLEANUP = json.loads((ROOT / 'cleanup-report.json').read_text(encoding='utf-8')) if (ROOT / 'cleanup-report.json').exists() else None

lines = [
    '# HUDuiPRO 独立组件动画索引', '',
    '81 个组件各有独立媒体链，UE 根目录为 `/Game/Assets/UI/HUDuiPRO/Animations`。'
    '保留原合成尺寸、24 fps、完整时长和透明通道；共 111,426 帧 RGBA8 PNG。', '',
    '动画内容由 AE 烘焙，数字和文字随源合成播放；玩法数值绑定可另接 UMG 文本层。'
    '3 个全局颜色控制合成不属于可播放组件；Final 组的完整驾驶舱静态纹理与 1080p 整段动画沿用原交付入口。', '',
    '## 单独调用', '',
    '1. 从下面目录中选取组件，将 `M_UI_HUDC_<组件键>` 设置为 UMG Image 的 Brush Material。',
    '2. Widget 创建或需要播放时，对同目录 `MP_HUDC_<组件键>` 调用 `Open Source`，传入 `IMS_HUDC_<组件键>`；播放器已设打开即播和循环。',
    '3. 需要暂停时调用该 MP 的 `Pause`；从头播放可重新打开 IMS，或在媒体就绪后 `Seek(0)` 再 `Play`。',
    '4. Widget 移除或该组件停用时调用对应 MP 的 `Close`，释放序列读取；其他组件使用各自 MP。', '',
    '每个目录另含 `MPL_HUDC_<组件键>`（单源播放列表）和 `MT_HUDC_<组件键>`（材质已绑定）。'
    '同一 MP 的多个显示实例共享播放时间；需要同一组件的不同播放进度时，分别创建播放器、媒体纹理，并在各自的材质中绑定对应纹理。', '',
    '例如 `Arrows 05` 的组件键为 `Arrows_05`，目录为 '
    '`/Game/Assets/UI/HUDuiPRO/Animations/Basic_Elements/Arrows/Arrows_05`，'
    '使用 `MP_HUDC_Arrows_05`、`IMS_HUDC_Arrows_05` 与 `M_UI_HUDC_Arrows_05`。', '',
    '完整的 405 个 UE 对象路径见 [manifest.json](manifest.json) 的 `components[].assets`；'
    '媒体序列位于 `Content/Movies/HUDuiPRO/Components`，IMS 使用项目相对路径。', '',
    '## 组件清单', '',
]
categories = collections.defaultdict(list)
for component in MANIFEST['components']:
    categories[component['categories'][0]].append(component)
for category, components in sorted(categories.items()):
    lines.extend([f'### {category}（{len(components)}）', '',
                  '| AE 合成 / 组件键 | 尺寸 | 秒 / 帧 | UE 目录 |', '|---|---|---|---|'])
    for component in sorted(components, key=lambda c: c['name'].casefold()):
        lines.append(f"| {component['name']} / `{component['key']}` | {component['width']}×{component['height']} | "
                     f"{component['duration']:.3f} / {component['frames']} | `{component['assetDirectory']}` |")
    lines.append('')
lines.extend([
    '## 保存与核对', '',
    f"序列占用 {SEQUENCES['bytes']:,} 字节，约 {SEQUENCES['bytes'] / 1024**3:.2f} GiB。"
    '各帧名称连续，PNG 容器的尺寸、RGBA8、头部 CRC 和结束块已核对；405 个 UE 对象的类型与媒体／材质引用已回读。', '',
    ('按用户要求，导入后已清理原始、适配及批渲染 AEP 和原始 MOV／PNG 素材；'
     'UE 资源、运行序列及 UE 贴图重导入原图继续保留。源文件哈希与清理记录见 [cleanup-report.json](cleanup-report.json)。'
     if CLEANUP and CLEANUP['ok'] else
     '源适配 AEP 保持不变；本批渲染队列单独保存于 [HUDuiPRO_ComponentRender.aep](HUDuiPRO_ComponentRender.aep)。') +
    '结构证据见 [sequence-verification.json](sequence-verification.json)、'
    '[ue-verification.json](ue-verification.json) 与 [render-summary.json](render-summary.json)。', '',
    '按项目约定，视觉、实际播放与性能由用户核验。本次未运行 PIE、Standalone、播放、性能测试或打包。'
    '打包复用 [资源说明](../README.md) 中的 ImgMedia 与非资产序列暂存要求。', '',
])
(ROOT / 'Catalog.md').write_text('\n'.join(lines), encoding='utf-8')
summary = {'componentCount': MANIFEST['componentCount'], 'frameCount': SEQUENCES['frames'],
           'sequenceBytes': SEQUENCES['bytes'], 'newAssetCount': ASSETS['registryTotal'],
           'rootAssetTotal': ASSETS['rootAssetTotal'], 'registryCounts': ASSETS['registryCounts'],
           'assetRoot': '/Game/Assets/UI/HUDuiPRO/Animations',
           'sequenceRoot': 'Content/Movies/HUDuiPRO/Components',
           'sourceProject': MANIFEST['sourceProject'], 'sourceSha256': SEQUENCES['sourceSha256'],
           'sourceUnchanged': SEQUENCES['sourceUnchanged'], 'renderJobSha256': SEQUENCES['renderJobSha256'],
           'aeSourcesRemovedAfterImport': bool(CLEANUP and CLEANUP['ok']),
           'renderExitCode': RENDER['exitCode'], 'renderCompletionVerified': RENDER['completionVerified'], 'structureVerification': 'passed',
           'visualVerification': 'pending_user', 'playbackVerification': 'not_run',
           'performanceVerification': 'not_run', 'packagingVerification': 'not_run'}
(ROOT / 'delivery-summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(summary, ensure_ascii=False))
