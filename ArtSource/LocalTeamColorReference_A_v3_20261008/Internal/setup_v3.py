"""Historical initial 13-model scaffold. Later user-authorized WM01 addition is recorded in catalogue.json."""
import copy
import hashlib
import json
import shutil
from pathlib import Path

OUT = Path(__file__).resolve().parents[1]
ROOT = OUT.parents[1]
OLD = ROOT / 'ArtSource/LocalTeamColorReference_A_v2_20261008'
if any((OUT / 'Boards').glob('*.png')):
    raise SystemExit('Initial scaffolding already used; do not overwrite completed boards or the revised 14-model catalogue.')
for name in ['Boards', 'Prompts', 'References', 'Sources', 'Configs', 'Internal']:
    (OUT / name).mkdir(parents=True, exist_ok=True)
catalogue = json.loads((OLD / 'Internal/catalogue.json').read_text(encoding='utf8'))
catalogue['version'] = OUT.name
catalogue['art_revision'] = '1.6'
catalogue['previous_version'] = OLD.name
catalogue['user_basis'] = [
    '护盾顶部三个色点改为队色区；空军基地顶部增加可变色区',
    '用户选择加入固定米砂辅色并重做全部十三模型，包括指挥中心和战略中心',
    '用户再次明确：使用此前四张色卡的固定配色；已批准执行A_v3计划',
    '参考审核后再施工，A_v3具体模型尚无通过决定',
]
palette = catalogue['palette']
palette['fixed_secondary'] = {'hex': '#D5C09C', 'card': 1, 'label': '固定米砂'}
palette['blue_secondary'] = {'hex': '#274E61', 'card': 4, 'label': '深蓝灰队色'}
palette['red_secondary'] = {'hex': '#662249', 'card': 2, 'label': '深莓队色'}
catalogue['design_limits'] = [
    '全部基础涂装选自用户四张色卡，不新增色系；深浅队色均为可变区',
    '乳白目标为模型可见表面的20–30%，不计背景/投影；按实际结构调整，二维图不作精确覆盖率测量',
    '米砂、乳白、深灰在蓝红版中一致；使用完整分件或区域遮罩，保留造型/装配/活动关系/动画/LOD',
    '护盾顶部三点属于队色发光区，蓝方蓝、红方红；其余固定功能区不随阵营变化',
    '三档明暗与两档队色分别管理，减少碎纹，不增加几何装饰或杂色',
    '全部A_v3待用户具体版本审核；Blender B及正式UE更新未开始',
]
# Each record: fixed cream, fixed sand, light team, dark team.
zones = {
    'DefaultSoldier': ('六腿主护甲及关节护盖', '中央主壳', '两侧完整肩部盒形护板', '后部弧形围带、六腿对应外侧识别护板'),
    'BiZhiMao': ('四腿主护甲', '长炮上部装甲及中央固定主壳', '炮身两侧连续大护板及散热框', '既有炮口外套、液压护套及四足踝护板'),
    'ShieldGenerator': ('三个柱下部护板、顶框及基座边框', '三个柱身大面及连接顶盖', '上部完整检修护盖、三个顶部色点', '现有底座环带、检修盖窄边框'),
    'ManualOutpost': ('连续下段叠板', '连续中段叠板', '连续上段叠板', '原腰部围笼'),
    'MissileTurret': ('发射仓内侧及边框、转台主护甲', '中部箱体、现有基座护甲', '两侧发射仓外侧完整大护板', '大护板中的既有长检修盖、现有转台环带'),
    'SentryTurret': ('炮塔正面护框和顶盖、转台护甲', '现有四向基座装甲', '炮塔两侧连续大护板及六孔通风盖框', '既有后上检修盖、现有转台窄环'),
    'ResourceFactory': ('现有门组护板、屋顶外框', '主墙、坡道及侧柜主体', '屋顶中央盖板、入口两侧完整翼板', '侧柜原有横向饰带'),
    'SSF_AirBase': ('原径向主压条、窄外框和雷达塔护框', '其余穹顶、塔体固定主壳', '穹顶前左/后右两组现有对称大扇形护板、雷达塔侧柜盖', '穹顶中心盖、原压条端面、下缘环带'),
    'SSF_CloningCenter': ('入口、顶盖及固定边框', '竖向背架和固定主体', '四舱现有外罩', '舱框、侧护盖及顶口框'),
    'SSF_CommandCenter': ('现有底足护甲及窄边框', '现有塔冠固定护甲', '现有上环完整外护板、下部支撑外护板', '次环及现有连接护盖'),
    'SSF_MilitaryFactory': ('原门板及入口边框', '完整大墙面', '现有屋顶拱架护板、入口立面板', '侧后墙现有腰带'),
    'SSF_Reactor': ('四支撑腿外护甲', '容器未分配队色的固定壳体', '主壳现有两侧连续完整护板', '原顶环、现有观察口边框'),
    'SSF_StrategyCenter': ('原支撑护甲及固定窄边框', '主体固定壳体', '现有主体腰部完整大护板', '阵列原边框、现有上环'),
}
for m in catalogue['models']:
    cream, sand, light, dark = zones[m['id']]
    old_fixed = copy.deepcopy(m['fixed'])
    m['fixed'] = [f'{cream}：乳白 #FEE4D9', f'{sand}：米砂 #D5C09C', '原有枪管/机构/支承/电缆/工作面/深槽：深灰 #2C3735，保持各模型既有分件']
    m['team_primary'] = [light]
    m['team_secondary'] = [dark]
    m['team'] = [f'浅队色：{light}', f'深队色：{dark}']
    m['paint_zones'] = {'fixed_armor': cream, 'fixed_secondary': sand, 'team_primary': light, 'team_secondary': dark, 'mechanisms': old_fixed[-1]}
    m['team_lights'] = []
    if m['id'] == 'ShieldGenerator':
        m['team_lights'] = ['三个原顶部色点：随浅队色变化（蓝灰/莓红），低强度、无明显光晕']
        m['function'] = ['顶部三点已纳入可变区；固定机构保留深灰。']
    if m['id'] == 'SSF_AirBase':
        m['structure'] = '保留原扁圆穹顶、分板、径向压条和相邻雷达塔；原顶部现有分板区域增加队色，不添加新结构。'
    if m['id'] == 'SSF_Reactor':
        m['structure'] = '保留原容器、四支撑、观察口、软管及背盒；主壳指定连续护板改为队色，其余固定米砂。'
    if m['id'] == 'SSF_MilitaryFactory':
        m['structure'] = '保留原宽体厂房、拱架、门口、墙面和雷达；固定大墙米砂，现有拱架及入口护板增加队色。'
    m['source'] = [s.replace(OLD.name, OUT.name) for s in m['source']]
    m['previous_boards'] = {team: f'ArtSource/{OLD.name}/Boards/{m["id"]}_{team}.png' for team in ('blue', 'red')}
    m['cream_coverage_target'] = {'range_percent': [20, 30], 'basis': 'visible model surfaces; excludes background, outline and cast shadow', 'is_measured': False}

for p in (OLD / 'References').glob('Palette_*.jpg'):
    shutil.copy2(p, OUT / 'References' / p.name)
for p in (OLD / 'Sources').glob('*.png'):
    shutil.copy2(p, OUT / 'Sources' / p.name)
for name in ['source-capture.json', 'source-capture-added-buildings.json', 'source-capture-turret-front.json']:
    shutil.copy2(OLD / name, OUT / name)
feedback = Path('C:/Users/a/AppData/Local/Temp/codex-clipboard-57a95256-512d-4895-aa2d-28f06c8adaf8.png')
if feedback.is_file():
    shutil.copy2(feedback, OUT / 'References/UserFeedback_ShieldTop_A_v2.png')
    catalogue['user_feedback_image'] = {'source_path': str(feedback), 'path': 'References/UserFeedback_ShieldTop_A_v2.png', 'sha256': hashlib.sha256(feedback.read_bytes()).hexdigest()}
source_map = json.loads((OLD / 'palette-source-map.json').read_text(encoding='utf8'))
source_map['version'] = OUT.name
for r in source_map['records']:
    r['destination'] = str(OUT / r['relative_destination'])
(OUT / 'palette-source-map.json').write_text(json.dumps(source_map, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
(OUT / 'Internal/catalogue.json').write_text(json.dumps(catalogue, ensure_ascii=False, indent=2) + '\n', encoding='utf8')
for name in ['index.html', 'overview.html', 'gallery.css', 'gallery.js', 'Internal/build_reference_gallery.py']:
    text = (OLD / name).read_text(encoding='utf8').replace('A_v2', 'A_v3')
    text = text.replace('浅色装甲，清楚的队色。', '米砂与深浅队色，重做装甲层次。')
    text = text.replace('功能区两队一致。', '固定功能区两队一致；护盾顶部三点随队色变化。')
    if name.endswith('build_reference_gallery.py'):
        text = text.replace('新增防空炮、哨戒炮、矿厂已纳入。', '全部十三模型重新设计，包含指挥中心和战略中心。')
        text = text.replace('本轮只从用户四张色卡选取：奶油白 #FEE4D9 作主装甲，深灰 #2C3735 作机构，蓝灰 #6AA4BE／莓红 #A34053 作队色。仅已有功能位置允许少量 #EE9D58 或 #0D9099；护盾顶部青灯两队固定一致。色阶是体积表达，基础涂装色以配置 HEX 为准。', '所有基础涂装只选用户四张色卡：乳白 #FEE4D9、固定米砂 #D5C09C、机构深灰 #2C3735；蓝方浅/深队色 #6AA4BE/#274E61，红方浅/深队色 #A34053/#662249。乳白目标降至可见模型表面的20–30%，参考图不作精确覆盖率测量。护盾顶部三个色点属于队色灯，蓝方蓝、红方红；其余原有固定功能色保留。两档队色与三档明暗分别管理。')
        text = text.replace("m['enemy_non_blue']='#A34053'", "m['enemy_non_blue']='#A34053'\n    m['enemy_non_blue_secondary']='#662249'")
    (OUT / name).write_text(text, encoding='utf8')
print(json.dumps({'version': OUT.name, 'models': len(catalogue['models']), 'boards_requested': 26, 'palette': {k:v['hex'] for k,v in palette.items()}}, ensure_ascii=False))
