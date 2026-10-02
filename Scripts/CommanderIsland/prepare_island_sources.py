"""Derive an editable 1800m Gaea project and explicit 7x7 landed anchors.

Run after capture_before.py. Numerical terrain inputs are rebuilt; the original
2300m project and its UE map remain available as the source revision.
"""

from pathlib import Path
import json
import math
import shutil
import copy
import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parents[2]
NAME = "GuLiStrike_CommanderIsland_1800m_v1"
BASE = Path.home() / "Documents/Gaea/MCP/Projects/GuLiStrike_CombatIsland_2300m_v1"
PROJECT = BASE.parent / NAME
DELIVERY = REPO / "ArtSource/Environment" / NAME
BEFORE = DELIVERY / "Before"

BLUE = [[0,2,2,4,6,4,0],[2,4,5,6,5,6,4],[2,2,6,6,6,7,3],
        [3,5,6,8,6,5,3],[3,7,6,6,6,2,2],[4,6,5,6,5,4,2],[0,4,6,4,2,2,0]]
RED = [[0,0,0,1,0,0,0],[0,0,1,2,1,0,0],[0,1,1,3,1,0,0],
       [1,2,3,6,3,2,1],[0,0,1,3,1,1,0],[0,0,1,2,1,0,0],[0,0,0,1,0,0,0]]


def source_module():
    text = (BASE / "scripts/prepare_sources.py").read_text(encoding="utf-8")
    text = text.replace("np.hypot((x-20)/805,(y-25)/790)",
        "(np.abs((x-20)/805)**4 + np.abs((y-25)/790)**4)**(1/4)")
    text = text.replace("radius=1+.055*np.sin(3*theta+.4)+.035*np.cos(5*theta-.7)+.025*np.sin(7*theta)+.025*np.cos(theta+.4)",
        "radius=1.07+.018*np.sin(3*theta+.4)+.012*np.cos(5*theta-.7)+.009*np.sin(7*theta)+.01*np.cos(theta+.4)")
    text = text.replace("260*np.exp(-((x-630)/240)**2-((y+480)/190)**2)",
        "140*np.exp(-((x-710)/170)**2-((y+340)/160)**2)")
    text = text.replace("900-np.abs(x),900-np.abs(y)", "790-10*np.sin(y/145+.7)-np.abs(x),790-10*np.cos(x/165-.3)-np.abs(y)")
    text = text.replace("distance+=55*np.exp(-((x+720)/170)**2-((y+130)/200)**2)",
        "distance+=55*np.exp(-((x+720)/170)**2-((y+130)/200)**2)\n    distance+=45*np.exp(-((y+740)/190)**2-((x-30)/470)**2)")
    text = text.replace("smooth(-170,0,distance)", "smooth(-90,0,distance)")
    text = text.replace("[620,-500,-5]]", "[620,-500,-5],[730,-540,-8],[840,-570,-16]]")
    text = text.replace("route['points'],endpoint_pads=True", "route['points'],endpoint_pads=route.get('endpoint_pads',True)")
    # New connector segments need a shorter flat tangent than the original long trunks.
    text = text.replace("min(.27*length,55)", "min(.12*length,25)")
    text = text.replace("105*np.exp", "130*np.exp")
    text = text.replace("protection=np.zeros_like(base)", """# One continuous grade surface prevents crossings of independent routes
    # from creating steps. It is 9m, rising gently to the northeast platform.
    ne_dx=np.maximum(np.abs(x-390)-80,0)
    ne_dy=np.maximum(np.abs(y-380)-80,0)
    ne_distance=np.hypot(ne_dx,ne_dy)
    home_distance=np.full_like(base,np.inf)
    for home in PADS:
        if home['name'] in ('NorthDeployment','SouthDeployment','NorthBase','SouthBase'):
            hx,hy=home['center_m']; hsx,hsy=home['size_m']
            home_distance=np.minimum(home_distance,np.hypot(np.maximum(np.abs(x-hx)-hsx/2,0),np.maximum(np.abs(y-hy)-hsy/2,0)))
    road_surface=9+11*np.minimum(1-smooth(0,110,ne_distance),smooth(0,85,home_distance))
    road_grade=np.zeros_like(base)
    protection=np.zeros_like(base)
    pad_core=np.zeros_like(base)""")
    text = text.replace("base=base*(1-grade)+levels*grade", "road_grade=np.maximum(road_grade,grade)")
    text = text.replace("    for pad in PADS:\n", "    base=base*(1-road_grade)+road_surface*road_grade\n    for pad in PADS:\n", 1)
    text = text.replace("grade=(1-smooth(0,150,edge))*(1-road_core)", "grade=(1-smooth(4,65,edge))*(1-road_core)")
    text = text.replace("grade=np.maximum(grade,(edge<=0).astype(np.float32))", "grade=np.maximum(grade,(edge<=4).astype(np.float32))")
    text = text.replace("base=base*(1-grade)+pad['height_m']*grade",
        "base=base*(1-grade)+pad['height_m']*grade\n        pad_core=np.maximum(pad_core,1-smooth(4,45,edge))")
    text = text.replace("base=coast_profile+(base-coast_profile)*smooth(35,120,distance)",
        "base=coast_profile+(base-coast_profile)*np.maximum(smooth(35,120,distance),np.maximum(pad_core,road_core))*smooth(0,15,distance)")
    module = {"__file__": str(PROJECT / "scripts/prepare_sources.py"), "__name__": "island_layout"}
    exec(compile(text, str(BASE / "scripts/prepare_sources.py"), "exec"), module)
    module.update(ROOT=PROJECT, NAME=NAME, SPAN=1800.0)
    return text, module


def island_project_copy():
    if PROJECT.exists():
        assert (PROJECT / (NAME + ".terrain")).is_file()
        assert json.loads((PROJECT / "layout.json").read_text(encoding="utf-8"))["name"] == NAME
        return
    PROJECT.mkdir(parents=True)
    (PROJECT / "scripts").mkdir()
    (PROJECT / "inputs").mkdir()
    shutil.copy2(BASE / "GuLiStrike_CombatIsland_2300m_v1.terrain", PROJECT / (NAME + ".terrain"))
    for filename in ("graph-manifest.json", "REFERENCES.md"):
        shutil.copy2(BASE / filename, PROJECT / filename)
    for path in (BASE / "scripts").iterdir():
        if path.is_file():
            text = path.read_text(encoding="utf-8").replace("GuLiStrike_CombatIsland_2300m_v1", NAME)
            text = text.replace("2300", "1800").replace("[-900,900]", "[-800,800]")
            (PROJECT / "scripts" / path.name).write_text(text, encoding="utf-8")


def sample(field, x, y):
    n = field.shape[0]
    ix = int(round((x + 900) / 1800 * (n - 1)))
    iy = int(round((900 - y) / 1800 * (n - 1)))
    return float(field[iy, ix])


def choose_anchors(module, height):
    n = height.shape[0]
    step = 1800 / (n - 1)
    gy, gx = np.gradient(height, step)
    slopes = np.degrees(np.arctan(np.hypot(gx, gy)))
    coast = np.asarray(Image.open(PROJECT/'inputs/land_outline.png'),dtype=np.float32)/65535
    before = json.loads((BEFORE / "editor_snapshot.json").read_text(encoding="utf-8"))
    army = before["army_layout"]["slots"]
    # Same runtime configuration and formation; assembly changes from 725m to 690m.
    slots = [(s["x"] / 100, s["y"] / 100 + (-35 if s["team"] == 1 else 35), s["radius_cm"] / 100)
             for s in army]
    chosen = []
    cell = 1800 / 7
    for row in range(1, 8):
        for col in range(1, 8):
            cx, cy = (col - 4) * cell, (4 - row) * cell
            candidates = []
            for y in np.arange(cy-cell/2+1, cy+cell/2-1, 5):
                for x in np.arange(cx-cell/2+1, cx+cell/2-1, 5):
                    h = sample(height, x, y)
                    if h < 2 or sample(slopes, x, y) > 15:
                        continue
                    # Pad footprint and land buffer must exist before grading.
                    if any(sample(coast, x+dx, y+dy) <= .501 for dx in (-45,0,45) for dy in (-45,0,45)):
                        continue
                    if any(sample(height,x+dx,y+dy)<=.1 for dx in (-18,0,18) for dy in (-18,0,18)):
                        continue
                    if any(math.hypot(x-sx,y-sy) < 65+sr for sx,sy,sr in slots):
                        continue
                    if any(math.hypot(x,y-sy) < 70 for sy in (740,-740)):
                        continue
                    # All footprints lie wholly on one level of the shared
                    # grade field; the northeast footprint is the higher one.
                    ne_dx=max(abs(x-390)-80,0); ne_dy=max(abs(y-380)-80,0)
                    in_ne=abs(x-390)<=61 and abs(y-380)<=61
                    if not in_ne and math.hypot(ne_dx,ne_dy)<145:
                        continue
                    cost = (x-cx)**2+(y-cy)**2+20*max(0,h-32)**2
                    candidates.append((cost, y, x, h))
            assert candidates, f"The enlarged coast has no legal placement in R{row}C{col}"
            _, y, x, h = min(candidates)
            pad_height = 20 if abs(x-390)<=61 and abs(y-380)<=61 else 9
            chosen.append({"name":f"Outpost_R{row}C{col}","row":row,"column":col,
                "logical_center_m":[cx,cy],"center_m":[round(x,4),round(y,4)],
                "size_m":[30,30],"height_m":pad_height,
                "initial_owner":"Red" if (row,col)==(1,4) else "Blue" if (row,col)==(7,4) else "Neutral",
                "blue_cluster_budget":BLUE[row-1][col-1],"red_cluster_budget":RED[row-1][col-1]})
    return chosen


def connectors(anchors, trunks):
    """Grow a deterministic route tree; height-aware cost avoids short steep links."""
    nodes = [list(p) for r in trunks for p in r["points"]]
    pending = list(anchors)
    routes = []
    while pending:
        choices = []
        for i,a in enumerate(pending):
            ax,ay=a["center_m"]; az=a["height_m"]
            for j,b in enumerate(nodes):
                distance=math.hypot(ax-b[0],ay-b[1])
                rise=abs(az-b[2])
                length_required=rise*1.5/math.tan(math.radians(10))+50
                cost=distance+max(0,length_required-distance)*3
                choices.append((cost,i,j,length_required))
        _,i,j,required=min(choices)
        a=pending.pop(i); b=nodes[j]
        start=[*a["center_m"],a["height_m"]]
        distance=math.hypot(start[0]-b[0],start[1]-b[1])
        points=[start,list(b)]
        if distance < required and abs(start[2]-b[2]) > 1:
            # A bent approach supplies the requested grading distance.
            vx,vy=b[0]-start[0],b[1]-start[1]
            offset=math.sqrt(max(0,(required/2)**2-(distance/2)**2))
            mid=[(start[0]+b[0])/2-vy/distance*offset,(start[1]+b[1])/2+vx/distance*offset,
                 (start[2]+b[2])/2]
            assert max(abs(mid[0]),abs(mid[1])) < 780, "Connector needs an inland bend"
            points=[start,mid,list(b)]
        routes.append({"name":a["name"]+"_Link","width_m":48,"minimum_width_m":40,
            "endpoint_pads":False,"points":points})
        nodes.append(start)
    return routes


def generate():
    island_project_copy()
    source, module = source_module()
    # Broad deployment ground accounts for the existing 500-unit formation,
    # while the original southwest/northeast gathering spaces remain available.
    team_pads = [
        {"name":"NorthDeployment","center_m":[0,560],"size_m":[450,230],"height_m":9},
        {"name":"SouthDeployment","center_m":[0,-560],"size_m":[450,230],"height_m":9},
        {"name":"NorthBase","center_m":[0,720],"size_m":[240,80],"height_m":9},
        {"name":"SouthBase","center_m":[0,-720],"size_m":[240,80],"height_m":9},
    ]
    trunks=module["ROUTES"]
    trunks += [
        {"name":"NorthAccess","width_m":60,"minimum_width_m":50,"endpoint_pads":False,
         "points":[[0,740,9],[0,560,9],[390,380,26]]},
        {"name":"SouthAccess","width_m":60,"minimum_width_m":50,"endpoint_pads":False,
         "points":[[0,-740,9],[0,-560,9],[-390,-350,9]]},
    ]
    module["PADS"] += team_pads
    next(p for p in module['PADS'] if p['name']=='Northeast')['height_m']=20
    module["generate"](size=1024,design_size=1024)
    height=np.asarray(Image.open(PROJECT / "inputs/foundation_height.png"),dtype=np.float32)/65535*140-20
    anchors=choose_anchors(module,height)
    module["PADS"] += anchors
    module["ROUTES"] += connectors(anchors,trunks)
    actual_anchors=copy.deepcopy(anchors)
    next(p for p in actual_anchors if p['name']=='Outpost_R4C4')['center_m']=[100,110]
    actual_pads=copy.deepcopy(module['PADS'])
    next(p for p in actual_pads if p['name']=='Outpost_R4C4')['center_m']=[100,110]
    authoring = dict(outposts=actual_anchors,pads=actual_pads,board_dimension=7,
        routes=[r for r in module['ROUTES'] if r['name']!='Outpost_R4C4_Link'],
        terrain_grading_routes=module['ROUTES'],
        terrain_grading_pads=module['PADS'],
        factory_anchors_m={"Red":[0,740],"Blue":[0,-740]},
        assembly_anchors_m={"Red":[0,690],"Blue":[0,-690]},
        budget_method="Area-weighted 9-to-7 resampling; central even budget; rotational pairs; largest fractional remainder then row-major index.",
        blue_budgets=BLUE,red_budgets=RED,source_revision="GuLiStrike_CombatIsland_2300m_v1")
    # Persist one standalone source generator instead of depending on the source module.
    source=source.replace("NAME = 'GuLiStrike_CombatIsland_2300m_v1'",f"NAME = '{NAME}'")
    source=source.replace("2300.0, -20.0", "1800.0, -20.0")
    start=source.index("ROUTES = ["); end=source.index("\ndef smooth(",start)
    source=source[:start]+"ROUTES = "+repr(module["ROUTES"])+"\nPADS = "+repr(module["PADS"])+"\nAUTHORING = "+repr(authoring)+"\n"+source[end:]
    source=source.replace("'land_limit_m':[-900,900]", "'land_limit_m':[-800,800]")
    source=source.replace("    (ROOT/'layout.json').write_text", "    metadata.update(AUTHORING)\n    (ROOT/'layout.json').write_text")
    (PROJECT / "scripts/prepare_sources.py").write_text(source,encoding="utf-8")
    module["generate"](size=4096,design_size=1024)
    layout=json.loads((PROJECT / "layout.json").read_text(encoding="utf-8"))
    layout.update(authoring,land_limit_m=[-800,800])
    (PROJECT / "layout.json").write_text(json.dumps(layout,ensure_ascii=False,indent=2),encoding="utf-8")
    DELIVERY.mkdir(parents=True,exist_ok=True)
    (DELIVERY / "layout.json").write_text(json.dumps(layout,ensure_ascii=False,indent=2),encoding="utf-8")
    print(json.dumps({"success":True,"project":str(PROJECT),"outposts":len(anchors),
        "routes":len(module["ROUTES"]),"blue_clusters":sum(map(sum,BLUE)),"red_clusters":sum(map(sum,RED))}))


if __name__ == "__main__":
    generate()
