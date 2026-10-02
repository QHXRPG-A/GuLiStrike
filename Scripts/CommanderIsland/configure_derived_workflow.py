"""Extend the retained Gaea validation workflow to the 49-anchor derivative."""
from pathlib import Path

ROOT = Path.home() / 'Documents/Gaea/MCP/Projects/GuLiStrike_CommanderIsland_1800m_v1'
BASE = ROOT.parent / 'GuLiStrike_CombatIsland_2300m_v1'

text = (BASE/'scripts/analyze_exports.py').read_text(encoding='utf-8').replace('2300','1800')
text = text.replace("for route,color in zip(LAYOUT['routes'],route_colors):", "for i,route in enumerate(LAYOUT['routes']):\n        color=route_colors[i] if i<3 else (185,219,201)")
text = text.replace("label={'Central':'中央主路','West':'西侧绕行','East':'东侧峡谷'}[route['name']]", "label={'Central':'中央主路','West':'西侧绕行','East':'东侧峡谷'}.get(route['name'],'')")
text = text.replace("label={'Southwest':'西南集结区','Northeast':'东北集结区'}[pad['name']]", "label={'Southwest':'西南集结区','Northeast':'东北集结区'}.get(pad['name'],pad['name'].replace('Outpost_',''))")
text = text.replace("label+' 160×160m'", "label")
text = text.replace("abs(x)>900+2*step", "abs(x)>800+2*step").replace("abs(y)>900+2*step", "abs(y)>800+2*step")
text = text.replace("np.abs(x)>900+2*step", "np.abs(x)>800+2*step").replace("np.abs(y)>900+2*step", "np.abs(y)>800+2*step")
text = text.replace("checks['connected_pads']=bool(visited[end])", """checks['connected_pads']=bool(visited[end])
    checks['connected_all_outposts_and_bases']=all(bool(visited[cell(p['center_m'])]) for p in LAYOUT['pads'])
    anchors=LAYOUT['outposts']
    checks['unique_49_outposts']=len(anchors)==49 and len({(p['row'],p['column']) for p in anchors})==49
    checks['anchors_within_canonical_cell']=all(abs(p['center_m'][0]-p['logical_center_m'][0])<SPAN/14 and abs(p['center_m'][1]-p['logical_center_m'][1])<SPAN/14 for p in anchors)
    checks['budget_200_blue_40_red']=sum(map(sum,LAYOUT['blue_budgets']))==200 and sum(map(sum,LAYOUT['red_budgets']))==40
    dry_xy=np.where(dry)
    land_bounds=[float(axis_value) for axis_value in (x[dry].min(),x[dry].max(),y[dry].min(),y[dry].max())]""")
text = text.replace("'gentle_land_fraction':ratio", "'land_bounds_m':land_bounds,'gentle_land_fraction':ratio")
(ROOT/'scripts/analyze_exports.py').write_text(text,encoding='utf-8')
print('Updated derivative terrain QA and reconstruction workflow.')
