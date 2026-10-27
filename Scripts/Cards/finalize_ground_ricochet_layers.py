"""Audit generated PNG metadata and package the artwork without editing pixels."""
from pathlib import Path
from datetime import datetime
import hashlib
import json
import sys
import zipfile
from PIL import Image


production = Path(sys.argv[1]).resolve()
requests = json.loads((production / "generation-requests.json").read_text(encoding="utf-8"))
provenance = json.loads((production / "generation-results.json").read_text(encoding="utf-8"))
manifest_path = production / "manifest.json"
manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
if manifest.get("state") == "source_pixel_repair_ready":
    raise SystemExit("Current artwork uses original-pixel extraction. Use repair_ricochet_source_layers.py; do not republish rejected generated layers.")
layers = []
errors = []


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


assert digest(production / "Master.png") == manifest["master_sha256"]
with Image.open(production / "Master.png") as image:
    reference_canvas = list(image.size)

for request in requests:
    index = request["layer"]
    result, = [item for item in provenance if item["layer"] == index]
    output = Path(request["output"])
    source = Path(result["source"])
    assert output.parent == production / "Layers"
    output_hash = digest(output)
    assert output_hash == digest(source), "Raw generated output was modified"
    prompt_path = Path(request["prompt_path"])
    prompt_text = prompt_path.read_text(encoding="utf-8")
    assert prompt_text == request["prompt"]
    assert hashlib.sha256(prompt_text.encode("utf-8")).hexdigest() == request["prompt_sha256"]
    with Image.open(output) as image:
        size = list(image.size)
        alpha = image.getchannel("A") if "A" in image.getbands() else None
        histogram = alpha.histogram() if alpha else None
        total = image.width * image.height
        metadata = {
            "mode": image.mode,
            "alpha_extrema": list(alpha.getextrema()) if alpha else None,
            "transparent_pixels": histogram[0] if histogram else 0,
            "partial_alpha_pixels": sum(histogram[1:255]) if histogram else 0,
            "opaque_pixels": histogram[255] if histogram else total,
            "near_opaque_pixels_alpha_ge_250": sum(histogram[250:]) if histogram else total,
        }
        if index <= 5:
            valid = alpha is not None and histogram[0] > 0 and sum(histogram[1:]) > 0
        else:
            valid = alpha is None or alpha.getextrema() == (255, 255)
        if not valid:
            errors.append(f"Layer {index}: unexpected transparency metadata")
    layers.append({
        "index": index, "name": request["name"],
        "file": output.relative_to(production).as_posix(),
        "canvas": size, "sha256": output_hash,
        "original_generated_output": str(source),
        "raw_output_preserved": True,
        "prompt": Path(request["prompt_path"]).relative_to(production).as_posix(),
        "prompt_sha256": request["prompt_sha256"],
        "prompt_hash_encoding": "UTF-8 content with LF newlines",
        "prompt_file_sha256": digest(prompt_path),
        "reference": "Master.png", "reference_sha256": manifest["master_sha256"],
        "requested_canvas": request["expected_canvas"],
        "transparent_background_requested": request["transparent_background"],
        "metadata": metadata,
    })

canvases = {tuple(layer["canvas"]) for layer in layers}
if len(layers) != 6 or len(canvases) != 1:
    errors.append("The package must contain six layers with a common canvas")
canvas = layers[0]["canvas"]
manifest.update(
    state="artwork_preview_ready" if not errors else "metadata_validation_failed",
    finalized_at=datetime.now().astimezone().isoformat(),
    reference_canvas=reference_canvas, canvas=canvas, layers=layers,
    requested_dimensions_met=all(layer["canvas"] == layer["requested_canvas"] for layer in layers),
    canvas_note="源图1024×1024；六次生成实际均为1254×1254。保留全部原始输出；六层共用完整画布与归一化UV，没有逐层裁切、缩放或位置补偿。",
    technical_validation={
        "passed": not errors,
        "scope": "File count, common actual dimensions, alpha metadata, raw file and prompt hashes only",
        "errors": errors,
        "raw_output_hashes_match": True,
        "reference_hash_unchanged": True,
    },
    visual_validation={
        "pixel_registration": "pending_user_review",
        "object_counts_and_connections": "pending_user_review",
        "neutral_composite": "pending_user_review",
        "extreme_tilt_and_edge_coverage": "pending_user_review",
        "assistant_output_visual_test": False,
    },
    preview={"file": "preview.html", "canvas_mapping": "common normalized full canvas", "composite_back_to_front": [6,4,5,3,2,1]},
    ue_delivery={"import_script": "ImportToUE.py", "imported": False, "reason": "Editor connection unavailable during production"},
)
manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
if errors:
    raise SystemExit("; ".join(errors))

readme = """# 折射子弹 · 六层视差牌面

本包用内置 imagegen，以同一张固定原图为参考生成六张独立图层。没有使用 ComfyUI，也没有新增技能、卡表、奖励或玩法逻辑。

## 查看牌面

打开 preview.html，移动鼠标或拖动牌面查看视差。可切换“原位叠合”和“原图”，并逐层开关、调透明度或下载 PNG。
预览中的标题和短语属于独立网页标识，未写入游戏文本管线或烘焙进图片。

## 六层

1. 近景：贴镜头残垣、石块、叶片和急刹扬土。
2. 速度线：白色、浅灰色漫画运动线。
3. 机甲：完整玩家机甲，连同附着的双股尾焰与枪口火光。
4. 场景：中近景草原、石阶与建筑残骸，补绘主体遮挡区域。
5. 折射弹道和三个敌方机甲：金色弹道、敌人、受击爆炸及关联碎片。
6. 远景：天空、远处地形、建筑、烟柱和导弹尾迹；不透明底图。

叠放顺序（后到前）：6 → 4 → 5 → 3 → 2 → 1。

## 实际文件与验收状态

原图 Master.png 为1024×1024。六张生成图实际均为1254×1254；前五张具有真实透明通道，第六张为不透明RGB。
保留生成原文件，没有像素修改、单层裁切或单层位置补偿；所有图层共用完整归一化画布。
尺寸、alpha及来源哈希已经核验。提示词中的坐标、敌人数量、机械连接与弹道连续性是生成约束，不代表已经通过视觉验收。
按项目规则，助手没有看结果图、截图对比或检查UE视口；请用户检查原位叠合、主要部件、弹道接点以及倾斜露边。
生成请求、完整提示词、原始输出路径、实际尺寸和SHA-256见 generation-requests.json、generation-results.json、manifest.json 与 Prompts/。

## UE交付

制作时UE编辑器未连接，尚未实际导入游戏。ImportToUE.py 是待执行的资产导入脚本，静态语法检查已通过，尚未在当前编辑器运行验证。
打开 D:/UE5.7/test1/GuLiStrike.uproject，退出PIE，通过项目 Scripts/ue_exec.py 执行本包 ImportToUE.py。
脚本仅导入六张纹理和独立牌面父材质/实例，目标为 /Game/GuLiStrike/Cards/GroundMech/Ricochet。
它不会新增卡牌数据行、技能效果、玩法蓝图或地图，也不改共享卡牌父材质。运行结果记录在 ue-import.json。

允许的制作范围、实际验证结果与用户美术验收分别记录；网页预览不等同UE最终呈现。
"""
(production / "README.md").write_text(readme, encoding="utf-8")
zip_path = production / "RicochetCard_Package.zip"
files = [path for path in production.rglob("*") if path.is_file() and path != zip_path]
with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
    for path in sorted(files):
        archive.write(path, "RicochetCard/" + path.relative_to(production).as_posix())
with zipfile.ZipFile(zip_path) as archive:
    assert archive.testzip() is None
print(json.dumps({"canvas": canvas, "layers": len(layers), "technical_passed": True, "zip": str(zip_path), "zip_bytes": zip_path.stat().st_size}, ensure_ascii=False))
