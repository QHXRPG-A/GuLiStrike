"""Create the standalone 1800m UE import scripts from the retained pipeline."""
from pathlib import Path
REPO=Path(__file__).resolve().parents[2]
NAME='GuLiStrike_CommanderIsland_1800m_v1'
PROJECT=Path.home()/'Documents/Gaea/MCP/Projects'/NAME
BASE=PROJECT.parent/'GuLiStrike_CombatIsland_2300m_v1'

text=(REPO/'Scripts/prepare_combat_island_ue_sources.py').read_text(encoding='utf-8')
text=text.replace('GuLiStrike_CombatIsland_2300m_v1',NAME).replace('CombatIsland_2300m_v1','CommanderIsland_1800m_v1')
text=text.replace('parents[1]','parents[2]').replace('230000.0','180000.0').replace('115000','90000').replace('[2300, 2300]','[1800, 1800]')
text=text.replace('/Game/Maps/LVL_CommanderIsland_1800m_v1','/Game/Maps/LVL_CommanderMassPrototype')
text=text.replace('-1100, -1100','-850, -850')
(REPO/'Scripts/CommanderIsland/prepare_ue_sources.py').write_text(text,encoding='utf-8')
readme=(BASE/'README.md').read_text(encoding='utf-8').replace('GuLiStrike_CombatIsland_2300m_v1',NAME).replace('2300','1800')
readme+='''

## 指挥官派生版本

- 来源为 2300 米海岛；全图 1800 米，陆地限于 X/Y ±800 米，海平面 0 米，编码范围 −20～120 米。
- `layout.json` 的 `outposts` 保存 49 个实际落地点；逻辑格中心独立记录，不用实际落地点移动领土边界。
- 原 81 个 Marker 保留 R1..7/C1..7 的 49 个身份，显式移除其余 32 个。保留地区 ID 与 25 米密度网格身份。
- 为避免路口高差，所有道路使用共同的高度面：一般 9 米，东北平台 20 米，平滑连接；原三条路线加基地入口及 49 条接入支路。
- 原有 500 个单位的出生阵列保持数量、间距和兵种配置，集结锚点变为 Y ±690 米，基地 Y ±740 米。
- `prepare_sources.py` 独立包含全部参数，可用 `Rebuild-Gaea.ps1 -Stage final -RefreshLayout` 重建，不需要修改来源工程。
- 正式 UE 地图为 `/Game/Maps/LVL_CommanderMassPrototype`。原 2300 米地图与 Gaea 工程保留。
'''
(PROJECT/'README.md').write_text(readme,encoding='utf-8')
print('Prepared independent UE import manifest workflow and reconstruction notes.')
