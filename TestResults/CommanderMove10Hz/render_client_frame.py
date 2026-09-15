"""Draw a clipped wall-clock slice from the recorded Insights events (no synthetic timing)."""
import csv
import html
from pathlib import Path
import xml.etree.ElementTree as ET

base = Path(__file__).resolve().parent
source = base / "locked-profile-n1200-c2/client1/insights/frame-58.4468732.csv"
start, end = 58.4468732, 58.4629727
with source.open(encoding="utf-8-sig", newline="") as stream:
    events = list(csv.DictReader(stream))

lanes = [
    ("GT · FEngineLoop::Tick", "GameThread", lambda n: n == "FEngineLoop::Tick", "#718096"),
    ("GT · 士兵实例更新", "GameThread", lambda n: n == "GuLiCommanderPresentation_RebuildLocalInstances", "#2563eb"),
    ("GT · ISM 批量变换（上行子项）", "GameThread", lambda n: n == "UInstancedStaticMeshComponent::BatchUpdateInstancesTransforms", "#0d9488"),
    ("GT · 小地图绘制", "GameThread", lambda n: n.startswith("GuLiCommanderMiniMapWidget ") and n.endswith("_Paint"), "#7c3aed"),
    ("GT · Game thread idle time", "GameThread", lambda n: n == "Game thread idle time", "#d97706"),
    ("RT · WaitForTasks", "RenderThread 0", lambda n: n == "WaitForTasks", "#d97706"),
    ("GPU Graphics · ShadowDepths", "GPU0-Graphics0", lambda n: n == "ShadowDepths", "#0891b2"),
    ("GPU Graphics · TSR", "GPU0-Graphics0", lambda n: n.startswith("TemporalSuperResolution("), "#7c3aed"),
]
width, height = 1500, 630
x0, x1, y0, pitch = 340, 1435, 160, 45
duration = (end - start) * 1000
scale = (x1 - x0) / duration
svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
       '<rect width="1500" height="630" fill="#f8fafc"/>',
       '<g font-family="Microsoft YaHei,Segoe UI,sans-serif" fill="#0f172a">',
       '<text x="40" y="51" font-size="28" font-weight="700">1,200 人 · 同机双客户端 · 时间线采样</text>',
       '<text x="40" y="88" font-size="17">客户端 1，采集开始约 20 秒后的一个完整游戏帧：16.10 ms（此帧不是 P95）</text>',
       '<text x="40" y="118" font-size="15" fill="#475569">时间片 58.4468732–58.4629727 s；GPU 异步执行，行间有嵌套，耗时不得相加。橙色代表等待。</text>']
for tick in range(0, 17, 2):
    x = x0 + tick * scale
    svg.append(f'<line x1="{x:.2f}" y1="140" x2="{x:.2f}" y2="525" stroke="#cbd5e1"/>')
    svg.append(f'<text x="{x:.2f}" y="550" text-anchor="middle" font-size="14">{tick} ms</text>')
for i, (label, thread, matches, color) in enumerate(lanes):
    y = y0 + i * pitch
    svg.append(f'<text x="40" y="{y + 21}" font-size="16">{html.escape(label)}</text>')
    for event in events:
        if event["ThreadName"] != thread or not matches(event["TimerName"]):
            continue
        a, b = max(start, float(event["StartTime"])), min(end, float(event["EndTime"]))
        if b <= a:
            continue
        x, w = x0 + (a - start) * 1000 * scale, (b - a) * 1000 * scale
        timing = (b - a) * 1000
        tooltip = html.escape(f'{event["TimerName"]}: visible {timing:.3f} ms')
        svg.append(f'<rect x="{x:.3f}" y="{y}" width="{w:.3f}" height="30" rx="3" fill="{color}"><title>{tooltip}</title></rect>')
        if w > 65:
            svg.append(f'<text x="{x + w / 2:.2f}" y="{y + 20}" text-anchor="middle" font-size="14" fill="white">{timing:.2f} ms</text>')
svg += ['<text x="40" y="594" font-size="15" fill="#475569">来源：Unreal Insights 实测事件；仅展示选定范围，不是完整调用树或 RenderDoc draw call 截帧。</text>', '</g></svg>']
document = "\n".join(svg)
ET.fromstring(document)
(base / "client-frame.svg").write_text(document, encoding="utf-8")
print(base / "client-frame.svg")
