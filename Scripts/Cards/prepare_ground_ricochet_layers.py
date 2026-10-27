"""Freeze a user-supplied master and prepare six built-in imagegen requests.

No bitmap pixels are edited. This script does not call ComfyUI or modify gameplay.
"""
from pathlib import Path
from datetime import datetime
import hashlib
import json
import shutil
import sys
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
PARENT = ROOT / "ArtSource/UI/GroundMechRogueCards/GroundMech_ChainRicochet"

COMMON = """本次制作GuLiStrike肉鸽卡牌的一个独立全画布图层。
以输入图为唯一构图、像素坐标、轮廓、配色和画风依据。原图为1024×1024正方形；输出也必须为1024×1024，保留完整画布和原始透明留白。指定对象必须处在整图原来的坐标、大小、朝向和透视，不能居中排版、裁切、缩放或重新安排。只制作本次指定的一层，不要六格拼图或画板。
保持原图清楚的黑色粗细线稿、日漫三渲二、大色块平涂、亮部／中间调／暗部三档明暗和明媚日光。此任务是按原图内容分层，不改机甲设计、腿部姿态、武器装配、敌人数量、战斗动作、爆炸、弹道或环境配色，不增加写实纹理、脏点、排线或划痕。
"""

TRANSPARENT = """
输出真正带透明通道的RGBA PNG：只保留本层指定对象，其余像素alpha=0。烟尘、光晕和柔边需要时使用真实的半透明alpha，不能画白色或灰色底板；不要棋盘格、洋红底、绿幕、黑底、投影展台、标签、文字、水印、卡框或UI。不要把对象独立摆在画布中间，必须维持原图位置。
"""

SPECS = [
    (1, "近景", "01_Foreground", True,
     """只保留最近前景：画面左下方贴近镜头的灰色石块与绿色叶片、右下方贴近镜头的覆苔破碎墙体和前景岩石，以及机甲急刹蹬地掀起的近处土浪、离地碎石、颗粒和扬尘。保持它们原有的形状、位置、大小与遮挡，烟尘边缘允许半透明，泥块石块保持实色。
机甲全身、两股背部尾焰、枪口火光、白色速度线、金色弹道、三个敌人及其受击爆炸、草原大地面、远景建筑和天空全部移除。不要把白色速度线混进扬尘，不把整片草原保留为一块不透明背景。与脚部相交的泥土只按原图可见的前景泥土保留，不补画机甲脚或腿。"""),
    (2, "速度线", "02_SpeedLines", True,
     """只保留原图已有的白色及浅灰色漫画速度线、运动拖线。每条线的起止点、长度、粗细、方向、透视、密度与原图一致；保留原本被画布边缘裁断的状态，不延长或重排，不追加放射状爆炸线。
移除所有机甲、敌人、泥土、碎石、烟团、地面、建筑和天空。金色折射弹道、橙色枪火、背部火焰以及天空中的导弹尾迹都不是本层，必须移除。白线为白色实体或自然柔边，周围为真实透明，不是白底上几条灰线。"""),
    (3, "机甲", "03_PlayerMech", True,
     """只保留左侧前景的整台蓝灰／米白双足地面机甲，包含原图可见的全部躯干、肩部、双枪、关节、折叠机械腿、脚掌、装甲轮廓和所有原有机械连接。严格沿用原图这个已经确定的造型与姿态，不按想象重新设计或修正；不要把另一台机甲或模型三视图引入。
背部两个喷口、从两处喷口根部直接喷出的两股橙黄火焰及其局部尾焰，与机甲一起保留在本层。枪口火光也保留并与原炮口相接。离开枪口后的金色折射子弹轨迹不在本层，移除其跨画面的部分。
保持原图左前景的机甲位置、大小、过肩角度、开火动作与急刹支撑动作，允许原本的画布边缘裁切；不为显示完整机体而缩小或居中。不画近景泥土、游离扬尘、白色速度线、任何敌人、爆炸、草地、石墙、建筑或天空。原图被泥土遮住的少量机甲边缘只可按现有机械结构做最小补全，不改变比例。"""),
    (4, "场景", "04_Scene", True,
     """只保留中近景环境：整幅战斗所在的绿色草原地面、倾斜的地面层级、石阶与岩石地表、贴地草丛，以及原图左上方较近的大型破碎拱门、覆苔墙体和右侧中景断壁。保持它们原来的透视、布局、轮廓、原坐标和日照，简洁色块与黑线完全沿用原图。
移除玩家机甲、尾焰、枪口火光、三个敌人及其爆炸、所有弹道、速度线、近处急刹扬土与碎片，以及第一层的贴镜头前景岩石、叶片和右下角最近墙块。在本层环境区域内，按原图地势补齐被这些物体挡住的草地、石阶、墙面，不留下机甲剪影洞口、敌人残影或火焰残影。
天空、云朵、远处导弹尾迹、地平线后最远的建筑残骸、远处烟柱和火光属于第六层，不在本层。环境物体轮廓以外的所有区域为真实透明，尤其建筑之间的天空空隙。不要因为透明而删掉原图中应保留的草原平台或拱门。"""),
    (5, "折射弹道和三个敌方机甲", "05_Ricochet_Enemies", True,
     """作为一个完整关联组，只保留原图中的连续金色折射弹道、恰好三个象牙白／酒红色四足敌方机甲、三个敌人的爆炸受击光和属于这些敌人的破甲碎片、飞散零件及贴近受击点的局部烟团。三台敌人的原位置、大小、各自受击动作、可见机械腿、装甲破坏状态与所有主要零件保持原图，不能增加、减少或复制敌人，不重画成蓝色玩家机甲。
完整保留所有金色轨迹段：从左侧玩家枪口外的出膛位置开始，按原图已有顺序连接三个受击点。原图的每个起点、转折和终点、各段方向、粗细、亮度保持不变；所有线段在三个敌人的爆炸受击点准确相接，不断开，不错接，不增加横穿画面的新光束。保持三个爆炸接触原来的装甲部位，保留冲击感与破碎。
不画左侧蓝色玩家机甲、玩家背部火焰或玩家枪口闪光；弹道起点停在原炮口之外的位置即可。不保留白色速度线、草地、建筑、天空、近景扬尘、近景石头或远处整片烟柱。敌人腿之间、轨迹之间的空隙必须透明，爆炸光晕使用适度真实半透明alpha，不画一块带环境的不透明矩形。"""),
    (6, "远景", "06_FarBackground", False,
     """只制作补全的最远背景底图：保留原图蓝天、白云、明媚日照、远处地平线、最远的草原层次、地平线附近的后现代建筑残骸、远处烟柱与火光、天空中的原有导弹与尾迹。它们的位置、外形、色块、黑色线稿、光向及透视保持原图。
移除原图的机甲、背部尾焰、近处枪火、三个敌方机甲、金色折射弹道、三处中景受击爆炸、白色速度线、近景扬土碎石、贴镜头前景物和第四层的近中景建筑／地面。将被这些前层挡住的区域按原透视补成简洁连贯的天空、远处地形和安静草地，覆盖整张画布；上方原有天空区域尽量保留，只补必要遮挡区，不重新设计新的天际线。
输出整张不透明背景PNG，每个像素都有自然背景内容。不得透明、不得棋盘、不得纯色空洞、不得留下主体剪影或重复机甲；不要把中近景物体重新画回远景底图。不加文字、边框或UI。"""),
]


def main():
    source = Path(sys.argv[1])
    with Image.open(source) as image:
        canvas = list(image.size)
    assert canvas == [1024, 1024], canvas
    index = 1
    while (PARENT / f"CardProduction_20261026_v{index}").exists():
        index += 1
    folder = PARENT / f"CardProduction_20261026_v{index}"
    (folder / "Prompts").mkdir(parents=True)
    (folder / "Layers").mkdir()
    master = folder / "Master.png"
    shutil.copy2(source, master)
    digest = hashlib.sha256(source.read_bytes()).hexdigest()
    assert hashlib.sha256(master.read_bytes()).hexdigest() == digest
    requests = []
    for layer, name, slug, transparent, instruction in SPECS:
        prompt = COMMON + "\n本次指定图层：" + str(layer) + " · " + name + "\n\n" + instruction
        if transparent:
            prompt += TRANSPARENT
        path = folder / "Prompts" / (slug + ".txt")
        path.write_text(prompt, encoding="utf-8")
        requests.append({"layer": layer, "name": name, "slug": slug,
                         "prompt": prompt, "prompt_path": str(path),
                         "prompt_sha256": hashlib.sha256(prompt.encode("utf-8")).hexdigest(),
                         "referenced_image_paths": [str(master)], "transparent_background": transparent,
                         "expected_canvas": canvas, "output": str(folder / "Layers" / (slug + ".png"))})
    manifest = {"state": "prepared", "created_at": datetime.now().astimezone().isoformat(),
                "task": "折射子弹肉鸽牌：只制作六层美术与视差牌面，不做玩法逻辑",
                "generator": "built-in image_gen", "comfyui_used": False,
                "master_source": str(source), "master": str(master), "master_sha256": digest,
                "canvas": canvas, "reference_policy": "all layers reference the same frozen master",
                "layer_order_user": [r["name"] for r in requests],
                "composite_back_to_front": [6, 4, 5, 3, 2, 1],
                "layers": [], "user_art_review": "pending", "assistant_output_visual_test": False,
                "ue_imported": False, "gameplay_modified": False}
    (folder / "generation-requests.json").write_text(json.dumps(requests, ensure_ascii=False, indent=2), encoding="utf-8")
    (folder / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({"folder": str(folder), "master": str(master), "requests": requests}, ensure_ascii=False))


if __name__ == "__main__":
    main()
