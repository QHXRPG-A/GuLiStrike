"""Build a read-only environment comparison from the editor snapshot."""

import html
import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent
DATA = json.loads((ROOT / "environment-snapshot.json").read_text(encoding="utf-8"))
TARGET, SOURCE = DATA["maps"]
MISSING = "[未捕获 / 不适用]"


def actor(world, class_name):
    return next(a for a in world["environment_actors"] if a["class"] == class_name)


def component(world, class_name):
    return actor(world, class_name)["components"][0]["properties"]


def equal(a, b):
    if isinstance(a, bool) or isinstance(b, bool):
        return type(a) is type(b) and a == b
    if isinstance(a, (float, int)) and isinstance(b, (float, int)):
        return math.isclose(a, b, rel_tol=1e-7, abs_tol=1e-8)
    return a == b


def display(v):
    if v is None:
        return "None（无资源）"
    if isinstance(v, bool):
        return "开启" if v else "关闭"
    if isinstance(v, float):
        return format(v, ".7g")
    if isinstance(v, dict) and "object_path" in v:
        return v["object_path"]
    if isinstance(v, (list, dict)):
        return json.dumps(v, ensure_ascii=False, indent=2)
    return str(v)


def esc(v):
    return html.escape(display(v))


LABELS = {
    "intensity": "强度",
    "mobility": "移动性",
    "light_color": "灯光颜色",
    "cast_shadows": "投射阴影",
    "cast_cloud_shadows": "云阴影",
    "atmosphere_sun_light": "大气太阳光",
    "dynamic_shadow_distance_movable_light": "CSM 动态阴影距离（cm）",
    "dynamic_shadow_cascades": "CSM 级联数量",
    "shadow_bias": "阴影偏移",
    "volumetric_scattering_intensity": "体积散射强度",
    "real_time_capture": "实时天空捕获",
    "source_type": "天空捕获来源",
    "cubemap": "指定 Cubemap",
    "lower_hemisphere_is_black": "下半球颜色开关",
    "fog_density": "高度雾密度",
    "fog_height_falloff": "高度雾衰减",
    "fog_max_opacity": "高度雾最大不透明度",
    "fog_inscattering_luminance": "高度雾散射颜色",
    "directional_inscattering_luminance": "方向光散射颜色",
    "enable_volumetric_fog": "组件体积雾开关",
    "volumetric_fog_distance": "体积雾距离（cm）",
    "volumetric_fog_scattering_distribution": "体积雾散射分布",
    "volumetric_fog_albedo": "体积雾反照率（8 位 RGB）",
    "volumetric_fog_extinction_scale": "体积雾消光比例",
    "override_light_colors_with_fog_inscattering_colors": "雾颜色覆盖灯光颜色",
    "auto_exposure_method": "曝光测光模式",
    "auto_exposure_min_brightness": "最小 EV100",
    "auto_exposure_max_brightness": "最大 EV100",
    "auto_exposure_bias": "曝光补偿",
    "bloom_intensity": "泛光强度",
    "indirect_lighting_intensity": "间接光强度",
    "motion_blur_amount": "运动模糊量",
    "ambient_occlusion_intensity": "环境遮蔽强度",
    "ambient_occlusion_radius": "环境遮蔽半径参数",
    "ambient_occlusion_bias": "环境遮蔽偏移",
    "ambient_occlusion_quality": "环境遮蔽质量",
    "ambient_occlusion_fade_distance": "环境遮蔽淡出距离（cm）",
    "vignette_intensity": "暗角强度",
    "color_contrast": "颜色对比度",
    "white_temp": "白平衡温度",
    "white_tint": "白平衡色调",
    "force_no_precomputed_lighting": "强制关闭预计算光照",
    "lightmass_settings": "Lightmass 设置",
    "world_to_meters": "世界单位与米的比例",
}

NOTES = {
    "dynamic_shadow_distance_movable_light": "当前启用虚拟阴影贴图；此处是存储的 CSM 参数，不能等同实际 VSM 覆盖距离。",
    "dynamic_shadow_cascades": "当前启用虚拟阴影贴图；CSM 存储设置。",
    "relative_location": "原生坐标为 cm。高度雾的 Z 位置参与高度密度计算；太阳与天空光位置不代表局部点光源位置。",
    "bloom_tint": "Light Shaft Bloom 的颜色；两图 enable_light_shaft_bloom 均为关闭。",
    "light_function_fade_distance": "两图未指定 light_function_material。",
    "auto_exposure_method": "源图未覆盖，存储值是 Histogram；当前图明确覆盖为 Histogram。",
    "auto_exposure_min_brightness": "项目已启用 ExtendDefaultLuminanceRange，此属性用 EV100 表示。当前编辑器另有固定 EV100=0 的视口覆盖。",
    "auto_exposure_max_brightness": "当前图 min=max=0，关闭自动明暗适应；源图限在 0.5–0.6。",
    "ambient_occlusion_radius": "两图 ambient_occlusion_radius_in_ws 均为关闭；不要把这个半径直接解释为固定世界半径。",
    "indirect_lighting_intensity": "当前项目使用 Lumen。此处对照属性及覆盖开关，不宣称画面整体亮度因此为 4 倍。",
    "enable_volumetric_fog": "两组件均开启，但项目 / 当前编辑器 r.VolumetricFog=0，体积雾渲染关闭。普通高度雾是另一项。",
    "lower_hemisphere_is_black": "对应 UE 的 Lower Hemisphere Is Solid Color 开关；两图自定义下半球颜色也相同。",
}


def row(key, source, target, note="", source_override=None, target_override=None):
    return {
        "parameter": key,
        "label": LABELS.get(key, key),
        "source": source,
        "current": target,
        "source_override": source_override,
        "current_override": target_override,
        "equal": equal(source, target) and source_override == target_override,
        "note": note or NOTES.get(key, ""),
    }


def pp_override(props, key):
    if key == "mega_lights":
        return props.get("override_b_mega_lights")
    if "override_" + key in props:
        return props["override_" + key]
    if "b_override_" + key in props:
        return props["b_override_" + key]
    return None


def compare(source, target, post_process=False, notes=None):
    rows = []
    for key in sorted(set(source) | set(target)):
        if post_process and (key.startswith("override_") or key.startswith("b_override_")):
            continue
        note = (notes or {}).get(key, "")
        if key.startswith("volumetric_fog_"):
            note = "体积雾存储参数；当前全局 r.VolumetricFog=0，所以不参与体积雾渲染。"
        rows.append(row(key, source.get(key, MISSING), target.get(key, MISSING), note,
                        pp_override(source, key) if post_process else None,
                        pp_override(target, key) if post_process else None))
    return rows


SD, TD = component(SOURCE, "DirectionalLight"), component(TARGET, "DirectionalLight")
SS, TS = component(SOURCE, "SkyLight"), component(TARGET, "SkyLight")
SF, TF = component(SOURCE, "ExponentialHeightFog"), component(TARGET, "ExponentialHeightFog")
SP, TP = actor(SOURCE, "PostProcessVolume")["post_process"], actor(TARGET, "PostProcessVolume")["post_process"]
SBP = actor(SOURCE, "BP_Sky_Sphere_C")["blueprint_properties"]
SAT = component(TARGET, "SkyAtmosphere")

sections = []
sections.append(("太阳光 / DirectionalLight", compare(SD, TD)))
sections.append(("天空光 / SkyLight", compare(SS, TS)))
sections.append(("高度雾与体积雾 / ExponentialHeightFog", compare(SF, TF)))
sections.append(("后期 / PostProcessSettings（数值 + 覆盖开关）", compare(SP, TP, post_process=True)))
sections.append(("后期覆盖开关原始字段（含未暴露对应值的旧属性）", compare(
    {k: v for k, v in SP.items() if k.startswith("override_") or k.startswith("b_override_")},
    {k: v for k, v in TP.items() if k.startswith("override_") or k.startswith("b_override_")})))
sections.append(("后期 Volume 的范围与混合", compare(
    {k: actor(SOURCE, "PostProcessVolume")["properties"][k] for k in ["enabled", "unbound", "blend_weight", "blend_radius", "priority", "is_spatially_loaded"]},
    {k: actor(TARGET, "PostProcessVolume")["properties"][k] for k in ["enabled", "unbound", "blend_weight", "blend_radius", "priority", "is_spatially_loaded"]})))
sections.append(("源图天空球 / BP_Sky_Sphere（当前图无对应对象）", compare(SBP, {})))
sky_material = actor(SOURCE, "BP_Sky_Sphere_C")["components"][0]["materials"][0]
sky_material_rows = [row("parent", sky_material["parent"], MISSING)]
for kind, params in sky_material["parameters"].items():
    sky_material_rows.extend(row(kind + ": " + k, v, MISSING,
                                 "读取的是 MID 当前值；BP 的 Sun height=0.3355215，MID 的 Sun height=0，二者不同。" if k == "Sun height" else "")
                             for k, v in sorted(params.items()))
sections.append(("源图天空材质 / M_Sky_Panning_Clouds2（当前图无对应材质）", sky_material_rows))
sections.append(("当前图大气 / SkyAtmosphere（源图无对应对象）", compare({}, SAT)))
world_keys = ["lightmass_settings", "world_to_meters", "volumetric_lightmap_loading_range", "default_game_mode", "enable_navigation_system"]
world_source = {k: actor(SOURCE, "WorldSettings")["properties"].get(k, MISSING) for k in world_keys}
world_target = {k: actor(TARGET, "WorldSettings")["properties"].get(k, MISSING) for k in world_keys}
world_source.update(DATA["world_flag_details"][1]["properties"])
world_target.update(DATA["world_flag_details"][0]["properties"])
sections.append(("世界设置 / WorldSettings", compare(world_source, world_target)))

summary = [
    row("天空实现", "BP_Sky_Sphere + 天空球材质", "SkyAtmosphere", "天空实现不同，颜色、云与太阳盘参数不能直接逐项拷贝到另一类组件。"),
    row("体积云 / VolumetricCloud", "无组件", "无组件", "全局 r.VolumetricCloud=1 只代表功能允许；场景没有体积云组件。"),
    row("天空中的云", "天空球材质云；速度 2；不透明度 0.32", "无对应云组件 / 天空球材质", "源图云来自 M_Sky_Panning_Clouds2；不是体积云。"),
    row("太阳光强度（lux）", SD["intensity"], TD["intensity"], "当前数值为源图的 15 倍；最终像素亮度还受曝光、天空光、材质等影响。"),
    row("太阳方向 Pitch / Yaw / Roll（°）", actor(SOURCE, "DirectionalLight")["rotation"], actor(TARGET, "DirectionalLight")["rotation"]),
    row("太阳光颜色", SD["light_color"], TD["light_color"]),
    row("太阳投射阴影", SD["cast_shadows"], TD["cast_shadows"]),
    row("天空光强度", SS["intensity"], TS["intensity"], "数值相同，但捕获天空的内容和捕获模式不同。"),
    row("天空光实时捕获", SS["real_time_capture"], TS["real_time_capture"]),
    row("天空光投射阴影", SS["cast_shadows"], TS["cast_shadows"]),
    row("高度雾 Z（m）", actor(SOURCE, "ExponentialHeightFog")["location"][2] / 100, actor(TARGET, "ExponentialHeightFog")["location"][2] / 100, "雾基准高度不同，不应只比较密度数值。"),
    row("高度雾密度", SF["fog_density"], TF["fog_density"]),
    row("高度雾衰减", SF["fog_height_falloff"], TF["fog_height_falloff"]),
    row("高度雾最大不透明度", SF["fog_max_opacity"], TF["fog_max_opacity"]),
    row("体积雾组件开关", SF["enable_volumetric_fog"], TF["enable_volumetric_fog"], "当前全局 r.VolumetricFog=0：两图体积雾渲染均关闭。"),
    row("体积雾距离（m）", SF["volumetric_fog_distance"] / 100, TF["volumetric_fog_distance"] / 100, "存储值为 60m / 3000m；当前未生效。"),
    row("曝光范围 EV100", [SP["auto_exposure_min_brightness"], SP["auto_exposure_max_brightness"]], [TP["auto_exposure_min_brightness"], TP["auto_exposure_max_brightness"]], "两图均覆盖最小 / 最大值；源图 0.5–0.6，当前图锁定 0。"),
    row("bloom_intensity", SP["bloom_intensity"], TP["bloom_intensity"], source_override=SP["override_bloom_intensity"], target_override=TP["override_bloom_intensity"]),
    row("indirect_lighting_intensity", SP["indirect_lighting_intensity"], TP["indirect_lighting_intensity"], source_override=SP["override_indirect_lighting_intensity"], target_override=TP["override_indirect_lighting_intensity"]),
    row("motion_blur_amount", SP["motion_blur_amount"], TP["motion_blur_amount"], source_override=SP["override_motion_blur_amount"], target_override=TP["override_motion_blur_amount"]),
    row("ambient_occlusion_intensity", SP["ambient_occlusion_intensity"], TP["ambient_occlusion_intensity"], source_override=True, target_override=True),
    row("vignette_intensity", SP["vignette_intensity"], TP["vignette_intensity"], source_override=True, target_override=True),
]

globals_now = dict(DATA["global_editor_cvars"])
globals_now.update(DATA["global_editor_cvars_integer"])
globals_now["r.AllowStaticLighting（项目配置）"] = False
global_notes = {
    "r.VolumetricFog": "项目 Config/DefaultEngine.ini:95 为 0；两个地图共用此项目设置。",
    "r.VolumetricCloud": "允许渲染体积云，但两图没有体积云组件。",
    "r.DynamicGlobalIlluminationMethod": "1 = Lumen。",
    "r.ReflectionMethod": "1 = Lumen。",
    "r.Shadow.Virtual.Enable": "虚拟阴影贴图开启。",
    "r.DefaultFeature.AutoExposure.ExtendDefaultLuminanceRange": "最小 / 最大曝光等属性使用 EV100。",
    "r.AntiAliasingMethod": "1 = FXAA。",
    "r.ScreenPercentage": "这是控制台原始值，不直接解释为实际渲染分辨率比例。",
    "r.AllowStaticLighting（项目配置）": "Config/DefaultEngine.ini:76；项目禁用静态光照，不能仅看 WorldSettings 的 ForceNoPrecomputedLighting 开关判断。",
}
sections.append(("共用项目 / 当前编辑器渲染开关", [row(k, v, v, global_notes.get(k, "两图共用项目状态；不是独立地图资产里的设置。")) for k, v in sorted(globals_now.items())]))


def cell(v, override):
    state = ""
    if override is True:
        state = '<span class="override on">已覆盖</span>'
    elif override is False:
        state = '<span class="override off">未覆盖 · 仅存储值</span>'
    return "<pre>" + esc(v) + "</pre>" + state


def table(rows):
    body = []
    for r in rows:
        status = "相同" if r["equal"] else "不同"
        body.append('<tr class="' + ("same" if r["equal"] else "diff") + '">'
                    + '<th scope="row">' + html.escape(r["label"])
                    + ('<small>' + html.escape(r["parameter"]) + '</small>' if r["label"] != r["parameter"] else "") + '</th>'
                    + '<td>' + cell(r["source"], r["source_override"]) + '</td>'
                    + '<td>' + cell(r["current"], r["current_override"]) + '</td>'
                    + '<td><b>' + status + '</b><p>' + html.escape(r["note"]) + '</p></td></tr>')
    return '<div class="table-scroll"><table><thead><tr><th>参数</th><th>GroundMech_Demo（源图）</th><th>CommanderMassPrototype（当前图）</th><th>状态 / 说明</th></tr></thead><tbody>' + ''.join(body) + '</tbody></table></div>'


parts = ['''<!DOCTYPE html><html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>GuLiStrike 场景环境参数对照</title><style>
:root{color-scheme:light}*{box-sizing:border-box}body{margin:0;background:#f5f6f7;color:#1d2831;font:15px/1.6 "Segoe UI","Microsoft YaHei",sans-serif}main{max-width:1240px;margin:0 auto;padding:36px 24px 56px}header{margin-bottom:26px}h1{font-size:30px;margin:0 0 12px}h2{font-size:21px;margin:30px 0 12px}p{margin:8px 0}.meta{color:#59636c;font-size:13px}.notice{background:#eaf1f6;border-left:4px solid #477593;padding:14px 18px;margin:18px 0}.warning{background:#fff5df;border-color:#ae7b27}table{width:100%;border-collapse:collapse;background:white;table-layout:fixed}th,td{border:1px solid #dce1e5;padding:10px 12px;text-align:left;vertical-align:top}thead th{background:#e9edf0;font-size:13px}thead th:first-child{width:23%}thead th:nth-child(2),thead th:nth-child(3){width:25%}thead th:last-child{width:27%}tbody th{font-weight:500}pre{font:12px/1.5 Consolas,"Microsoft YaHei",monospace;white-space:pre-wrap;overflow-wrap:anywhere;margin:0}td p{font-size:12px;color:#59636c;margin:4px 0 0}.diff>th{box-shadow:inset 3px 0 #c18b32}.diff>td:last-child>b{color:#875f20}.same>td:last-child>b{color:#436d5c}small{display:block;color:#77828c;font:11px/1.5 Consolas,monospace;overflow-wrap:anywhere;margin-top:5px}.override{display:inline-block;font-size:11px;border-radius:4px;margin-top:7px;padding:1px 5px}.on{background:#e7f2ec;color:#38654f}.off{background:#eef0f3;color:#69727b}details{background:white;border:1px solid #dce1e5;margin:12px 0}summary{cursor:pointer;padding:12px 16px;font-weight:600}details>.table-scroll,details>p{margin:0 16px 16px}.table-scroll{overflow-x:auto}a{color:#275d82}code{font:12px Consolas,monospace;overflow-wrap:anywhere}.paths{font:12px/1.5 Consolas,monospace;overflow-wrap:anywhere}footer{font-size:12px;color:#59636c;margin-top:28px}.camera-table td{font-size:13px}nav{display:flex;flex-wrap:wrap;gap:14px;font-size:13px;margin-top:16px}@media(max-width:800px){main{padding:20px 12px}h1{font-size:24px}table{min-width:900px}}@media print{body{background:white}main{max-width:none;padding:0}details{break-inside:avoid}.table-scroll{overflow:visible}summary{background:#f3f3f3}}
</style></head><body><main><header><p class="meta">GuLiStrike · UE 5.7 · 2026-10-01 · 只读参数检查</p><h1>当前场景与地面机甲演示关卡的环境对照</h1>
<p>两图的环境配置存在明显差异：太阳光强度为 45 / 3，天空系统不同，雾与曝光设置也不同。两图都没有 VolumetricCloud 组件；源图的云来自天空球材质。</p>''',
         '<p class="paths">源图：' + html.escape(SOURCE["world"]) + '<br>当前图：' + html.escape(TARGET["world"]) + '</p>',
         '''<div class="notice warning"><strong>两项容易混淆的状态</strong><p>两图高度雾组件都勾选了体积雾，但项目 r.VolumetricFog=0，当前体积雾渲染关闭。源图天空材质的平面云与体积雾、体积云是三种不同的内容。</p><p>当前编辑器视口固定 EV100=0；它会覆盖视口中的关卡曝光表现。不能只凭这个视口的画面推断源图 0.5–0.6 的自动曝光效果。</p></div>
<nav><a href="#key">关键对照</a><a href="#all">逐项参数</a><a href="#camera">相机与检查范围</a><a href="environment-snapshot.json">原始读数 JSON</a><a href="important-parameters.json">结构化对照 JSON</a></nav></header>
<h2 id="key">关键对照</h2><p class="meta">后期参数的数值必须与覆盖开关一起看。“未覆盖”表示该 Volume 不主动指定此值，最终值继承其他来源。</p>''', table(summary),
         '<h2 id="all">逐项参数</h2><p class="meta">下方按组件配对；显示全部已捕获字段。“不同”包括覆盖开关不同。无同类组件的字段标为不适用，不把天空球颜色当成大气散射系数。浮点数显示约 7 位有效数字；完整精度在原始 JSON。</p>']

for title, rows in sections:
    diffs = [r for r in rows if not r["equal"]]
    is_paired = not ("无对应" in title)
    if diffs and is_paired:
        parts.append('<details open><summary>' + html.escape(title) + ' · 差异 ' + str(len(diffs)) + ' 项</summary>' + table(diffs) + '</details>')
    parts.append('<details><summary>' + html.escape(title) + ' · 全部 ' + str(len(rows)) + ' 项</summary>' + table(rows) + '</details>')

parts.append('''<h2 id="camera">相机与检查范围</h2><p>检查了关卡中现有相机的后期覆盖开关：源图 2 个相机组件、当前图 17 个，均未启用后期参数覆盖，PostProcessBlendWeight 均为 1。相机对象并非一一对应，因此按地图分别列出。运行时创建的玩家相机或 CameraManager 未在本次检查范围。</p>
<div class="table-scroll"><table class="camera-table"><thead><tr><th>地图</th><th>相机 Actor</th><th>组件 / FOV</th><th>后期覆盖</th></tr></thead><tbody>''')
for world in (SOURCE, TARGET):
    for c in world["cameras"]:
        active = [k for k, v in c["post_process"].items() if (k.startswith("override_") or k.startswith("b_override_")) and v]
        parts.append('<tr><td>' + ("GroundMech_Demo" if world is SOURCE else "CommanderMassPrototype") + '</td><td>' + html.escape(c["actor"]) + '<small>' + html.escape(c["name"]) + '</small></td><td>' + html.escape(c["component"]) + ' / ' + esc(c["field_of_view"]) + '°</td><td>' + (html.escape(", ".join(active)) if active else "无已启用覆盖") + '</td></tr>')
parts.append('</tbody></table></div><details><summary>编辑器视口原始状态</summary><p class="paths">' + html.escape(DATA["before"]["viewport"]) + '</p></details>')
parts.append('''<div class="notice"><p>本次读取当前编辑器世界和源关卡已加载的资产，不切换关卡、不保存资产、不启动 PIE、不编译代码。读取前后当前世界、视口、显示模式、未保存地图和内容包列表完全相同。</p><p>项目共享渲染开关是当前进程的读数，不能当成两次独立游戏运行的测试结果。此次不作像素亮度的等比例结论，不验证运行时 Construction Script、天气脚本或玩家相机的动态变化。</p></div>
<footer><p>数据来源：UnrealMCP / 编辑器 Python 属性回读；完整对象属性、后期开关、相机设置及组件库存见 environment-snapshot.json。配置核对：Config/DefaultEngine.ini:68–76、95。</p><p>字段语义参考：<a href="https://dev.epicgames.com/documentation/unreal-engine/auto-exposure-in-unreal-engine?lang=en-US">Epic 自动曝光文档</a>（EV100 与相同最小 / 最大值关闭自动曝光）；<a href="https://dev.epicgames.com/documentation/unreal-engine/using-physical-lighting-units-in-unreal-engine?lang=en-US">Epic 物理光照单位文档</a>（Directional Light 使用 lux）。文档当前页面版本为 5.8；本报告中的数值来自此 UE 5.7 项目。</p></footer></main></body></html>''')

report = ''.join(parts)
(ROOT / "environment-comparison.html").write_text(report, encoding="utf-8")
structured = {
    "source": SOURCE["world"], "current": TARGET["world"], "read_only": True,
    "editor_state_unchanged": DATA["editor_state_unchanged"],
    "summary": summary,
    "sections": [{"title": title, "rows": rows} for title, rows in sections],
    "cameras": [{"world": m["world"], "rows": [{k: v for k, v in c.items() if k != "post_process"} for c in m["cameras"]]} for m in [SOURCE, TARGET]],
}
(ROOT / "important-parameters.json").write_text(json.dumps(structured, ensure_ascii=False, indent=2), encoding="utf-8")
print(json.dumps({"report": str(ROOT / "environment-comparison.html"), "summary_rows": len(summary), "sections": [{"title": title, "rows": len(rows), "differences": sum(not r["equal"] for r in rows)} for title, rows in sections], "editor_state_unchanged": DATA["editor_state_unchanged"]}, ensure_ascii=True, indent=2))
