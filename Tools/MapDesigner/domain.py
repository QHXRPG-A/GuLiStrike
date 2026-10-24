"""Versioned, engine-independent map data, geometry, validation and legacy export.

All business coordinates are centimetres. PNG files are numerical height/weight
buffers; this module never renders or performs visual inspection.
"""
from __future__ import annotations

import copy
import csv
import hashlib
import io
import json
import math
import re
import uuid
from collections import defaultdict
from pathlib import Path

import numpy as np

SCHEMA = "guli-map-design/v1"
CANONICAL_MAP = "/Game/Maps/LVL_CommanderMassPrototype"
MAX_CELLS = 4 * 1024 * 1024
KEY = re.compile(r"^[A-Za-z_][A-Za-z_0-9]*$")
FIELD_TYPES = {"bool", "int32", "double", "name", "string", "enum", "vector", "soft_object", "soft_class"}
SHAPES = {"Cylinder", "Sphere", "Box", "PolygonPrism"}
GAMEPLAY_PROFILE = json.loads(Path(__file__).with_name("gameplay-profile.json").read_text(encoding="utf-8"))


def canonical(value):
    if value is None:
        return "null"
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, np.integer)):
        return str(int(value))
    if isinstance(value, (float, np.floating)):
        if not math.isfinite(value):
            raise ValueError("数据包含非有限数字")
        return format(float(value), ".17g")
    if isinstance(value, str):
        return json.dumps(value, ensure_ascii=False)
    if isinstance(value, dict):
        return "{" + ",".join(canonical(k) + ":" + canonical(value[k]) for k in sorted(value)) + "}"
    if isinstance(value, (list, tuple)):
        return "[" + ",".join(canonical(v) for v in value) + "]"
    raise ValueError("不支持的数据类型: " + type(value).__name__)


def json_bytes(value):
    return (canonical(value) + "\n").encode("utf-8")


def digest(data):
    return hashlib.sha256(data).hexdigest()


def valid_guid(value):
    try:
        return uuid.UUID(value).int != 0
    except (ValueError, TypeError, AttributeError):
        return False


def finite(value):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value)


def vector(value):
    return isinstance(value, list) and len(value) == 3 and all(finite(v) for v in value)


def rotation(rotator):
    p, y, r = np.radians(rotator)
    cp, sp, cy, sy, cr, sr = math.cos(p), math.sin(p), math.cos(y), math.sin(y), math.cos(r), math.sin(r)
    return np.array([[cp*cy, sr*sp*cy-cr*sy, -(cr*sp*cy+sr*sy)],
                     [cp*sy, sr*sp*sy+cr*cy, cy*sr-cr*sp*sy], [sp, -sr*cp, cr*cp]])


def rotator(matrix):
    cp = math.hypot(matrix[0, 0], matrix[1, 0])
    p = math.atan2(matrix[2, 0], cp)
    if cp > 1e-10:
        y, r = math.atan2(matrix[1, 0], matrix[0, 0]), math.atan2(-matrix[2, 1], matrix[2, 2])
    else:
        y, r = math.atan2(-matrix[0, 1], matrix[1, 1]), 0
    return [math.degrees(v) for v in (p, y, r)]


def region_transform(marker, region):
    parent, local = marker["world_transform"], region["relative_transform"]
    a, b = rotation(parent["rotation_pitch_yaw_roll"]), rotation(local["rotation_pitch_yaw_roll"])
    return {"position": (a @ np.array(local["position"]) + parent["position"]).tolist(),
            "rotation_pitch_yaw_roll": rotator(a @ b), "scale": [1, 1, 1]}


def terrain_dimensions(t):
    nx, ny = t["cells_x"], t["cells_y"]
    if (type(nx) is not int or type(ny) is not int or min(nx, ny) < 1 or max(nx,ny)>8160 or nx*ny > MAX_CELLS
            or t.get("cell_size_cm") != 300 or len(t.get("origin_cm", [])) != 2
            or not all(finite(v) for v in t["origin_cm"])):
        raise ValueError("地形长宽或3米格配置无效（上限4,194,304格）")
    return nx, ny


def terrain_shape(project):
    if project.get("schema") != SCHEMA:
        raise ValueError("不支持的工程版本")
    t = project["terrain"]
    nx, ny = terrain_dimensions(t)
    values = np.asarray(t["heights_cm"], dtype=np.float64)
    if values.size != (nx+1)*(ny+1) or not np.isfinite(values).all():
        raise ValueError("高度顶点数量或数值无效")
    return values.reshape(ny+1, nx+1)


def sample_height(t, heights, x, y, with_slope=False):
    u, v = np.clip((x-t["origin_cm"][0])/300, 0, t["cells_x"]), np.clip((y-t["origin_cm"][1])/300, 0, t["cells_y"])
    ix, iy = min(int(u), t["cells_x"]-1), min(int(v), t["cells_y"]-1)
    fx, fy = u-ix, v-iy
    h00, h10, h01, h11 = heights[iy, ix], heights[iy, ix+1], heights[iy+1, ix], heights[iy+1, ix+1]
    if fx >= fy:
        height, dx, dy = h00+(h10-h00)*fx+(h11-h10)*fy, (h10-h00)/300, (h11-h10)/300
    else:
        height, dx, dy = h00+(h11-h01)*fx+(h01-h00)*fy, (h11-h01)/300, (h01-h00)/300
    return (float(height), math.degrees(math.atan(math.hypot(dx, dy)))) if with_slope else float(height)


def in_bounds(t, x, y):
    return t["origin_cm"][0] <= x <= t["origin_cm"][0]+t["cells_x"]*300 and t["origin_cm"][1] <= y <= t["origin_cm"][1]+t["cells_y"]*300


def contains(vertices, x, y):
    hit = False
    for i, (bx, by) in enumerate(vertices):
        ax, ay = vertices[i-1]
        cross = (x-ax)*(by-ay)-(y-ay)*(bx-ax)
        if abs(cross) <= 1e-7*max(1, math.hypot(bx-ax, by-ay)) and min(ax, bx)-1e-7 <= x <= max(ax, bx)+1e-7 and min(ay, by)-1e-7 <= y <= max(ay, by)+1e-7:
            return 2
        if (ay > y) != (by > y) and x < (bx-ax)*(y-ay)/(by-ay)+ax:
            hit = not hit
    return 1 if hit else 0


def polygon_error(v):
    if not isinstance(v, list) or not 3 <= len(v) <= 4096 or not all(isinstance(p, list) and len(p) == 2 and all(finite(n) for n in p) for p in v):
        return "多边形需要3至4096个有限二维顶点"
    if abs(sum(v[i-1][0]*p[1]-p[0]*v[i-1][1] for i, p in enumerate(v))) < .001:
        return "多边形面积接近零"
    def orient(a,b,c): return (b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0])
    def on(a,b,c): return abs(orient(a,b,c)) < 1e-7 and min(a[0],b[0])-1e-7 <= c[0] <= max(a[0],b[0])+1e-7 and min(a[1],b[1])-1e-7 <= c[1] <= max(a[1],b[1])+1e-7
    for i, a in enumerate(v):
        b=v[(i+1)%len(v)]
        if math.dist(a,b)<.001:
            return "多边形包含零长度边"
        previous=v[i-1]
        if abs(orient(previous,a,b))<1e-7 and (on(previous,a,b) or on(a,b,previous)):
            return "相邻边重叠或回折"
        for j in range(i+1,len(v)):
            if j==i+1 or i==0 and j==len(v)-1: continue
            c,d=v[j],v[(j+1)%len(v)]
            if (max(min(a[0],b[0]),min(c[0],d[0])) > min(max(a[0],b[0]),max(c[0],d[0]))+1e-7
                    or max(min(a[1],b[1]),min(c[1],d[1])) > min(max(a[1],b[1]),max(c[1],d[1]))+1e-7): continue
            if orient(a,b,c)*orient(a,b,d)<0 and orient(c,d,a)*orient(c,d,b)<0 or on(a,b,c) or on(a,b,d) or on(c,d,a) or on(c,d,b):
                return "多边形自交、非相邻边接触或重叠"
    return None


def geometry_error(region):
    shape, g = region.get("shape_type"), region.get("geometry", {})
    expected={"Cylinder":{"radius","min_z","max_z"},"Sphere":{"radius"},"Box":{"half_extents"},"PolygonPrism":{"vertices","min_z","max_z"}}
    if shape not in SHAPES or set(g) != expected.get(shape): return "不支持的形状或几何字段不完整"
    if shape in {"Cylinder","Sphere"} and (not finite(g["radius"]) or g["radius"] <= .001): return "半径必须大于0.001厘米"
    if shape=="Box" and (not vector(g["half_extents"]) or min(g["half_extents"])<=.001): return "盒体半尺寸必须为正"
    if shape in {"Cylinder","PolygonPrism"} and (not finite(g["min_z"]) or not finite(g["max_z"]) or g["max_z"]-g["min_z"]<=.001): return "上下高度无效"
    return polygon_error(g["vertices"]) if shape=="PolygonPrism" else None


def region_xy_bounds(marker, region):
    world=region_transform(marker,region);center=np.asarray(world["position"]);r=rotation(world["rotation_pitch_yaw_roll"])
    g=region["geometry"];shape=region["shape_type"]
    if shape=="PolygonPrism":
        points=np.asarray([[x,y,z] for x,y in g["vertices"] for z in (g["min_z"],g["max_z"])])@r.T+center
        return points[:,:2].min(axis=0),points[:,:2].max(axis=0)
    if shape=="Sphere":extent=np.full(3,g["radius"])
    elif shape=="Box":extent=np.abs(r)@np.asarray(g["half_extents"])
    else:
        center=center+r[:,2]*(g["min_z"]+g["max_z"])/2
        extent=np.hypot(r[:,0],r[:,1])*g["radius"]+np.abs(r[:,2])*(g["max_z"]-g["min_z"])/2
    return (center-extent)[:2],(center+extent)[:2]


def value_error(field,value):
    kind=field.get("field_type")
    good=(kind=="bool" and type(value) is bool or kind=="int32" and type(value) is int and -2147483648<=value<=2147483647
          or kind=="double" and finite(value) or kind=="vector" and vector(value)
          or kind in {"name","string","enum","soft_object","soft_class"} and isinstance(value,str))
    if not good: return "字段值与类型不符"
    if kind=="enum" and field.get("enum_values") and value not in field["enum_values"]: return "枚举值不在定义中"
    if field.get("required") and isinstance(value,str) and not value.strip(): return "必填字段为空"
    if kind in {"int32","double"} and ("minimum" in field and value<field["minimum"] or "maximum" in field and value>field["maximum"]): return "数字超过字段允许范围"
    return None


def validate(project):
    heights=terrain_shape(project); t=project["terrain"]; issues=[]
    def add(message,severity="error",marker=None,region=None,field=None):
        issue={"severity":severity,"message":message}
        if marker: issue["marker_id"]=marker.get("marker_id","")
        if region: issue["region_id"]=region.get("region_id","")
        if field: issue["field"]=field
        issues.append(issue)
    types={}; field_ids=set()
    for kind in project["layout"]["types"]:
        key=kind.get("type_id","")
        if not KEY.fullmatch(key) or key.casefold() in {k.casefold() for k in types}: add("类型业务键无效或重复: "+key)
        types[key]=kind; names=set(); ids=set()
        if not set(kind.get("allowed_shapes",[]))<=SHAPES: add("类型包含不支持的形状: "+key)
        for f in kind.get("fields",[]):
            fk=f.get("field_key","");fid=f.get("field_id","")
            if not KEY.fullmatch(fk) or fk.casefold() in names or not valid_guid(fid) or fid in ids or f.get("field_type") not in FIELD_TYPES: add("类型字段身份、键或类型无效: "+key+"."+fk)
            names.add(fk.casefold());ids.add(fid)
            if f.get("field_type") in {"enum","vector"} and not f.get("value_type_object") and f.get("field_type")=="enum": add("枚举字段缺少UE枚举类型路径: "+key+"."+fk)
            try:
                error=value_error(f,f.get("default"))
                if error:add(key+"."+fk+": 默认"+error)
            except (TypeError,ValueError):add(key+"."+fk+": 默认值或范围无效")
        for region in kind.get("default_regions",[]):
            error=geometry_error(region)
            if error: add(key+": 默认区域"+error)
    keys=set(); ids=set(); region_ids=set(); outposts=[]
    for marker in project["layout"]["markers"]:
        key=marker.get("marker_key",""); mid=marker.get("marker_id","")
        if not KEY.fullmatch(key) or key.casefold() in keys: add("标记业务键无效或重复: "+key,marker=marker)
        if not valid_guid(mid) or mid in ids:add("标记GUID无效或重复: "+key,marker=marker)
        keys.add(key.casefold());ids.add(mid); kind=types.get(marker.get("type_id"))
        if not kind:add("标记类型不存在: "+key,marker=marker);continue
        tr=marker.get("world_transform",{})
        if not vector(tr.get("position")) or not vector(tr.get("rotation_pitch_yaw_roll")) or tr.get("scale") != [1,1,1]:add("标记变换无效，Scale须为1: "+key,marker=marker);continue
        if not in_bounds(t,*tr["position"][:2]):add("标记位于设计边界外: "+key,"warning",marker)
        tags=marker.get("tags",[])
        if not isinstance(tags,list) or any(not isinstance(v,str) or not v for v in tags) or len({v.casefold() for v in tags})!=len(tags):add("标签为空或重复: "+key,marker=marker)
        old=project.get("editor",{}).get("field_layouts",{}).get(mid,kind["fields"])
        if {(f["field_id"],f["field_key"],f["field_type"],f.get("value_type_object","")) for f in old}!={(f["field_id"],f["field_key"],f["field_type"],f.get("value_type_object","")) for f in kind["fields"]}:add("字段布局尚未同步: "+key,marker=marker)
        if set(marker.get("parameters",{}))!={f["field_key"] for f in kind["fields"]}:add("参数键与类型定义不一致: "+key,marker=marker)
        for f in kind["fields"]:
            error=value_error(f,marker.get("parameters",{}).get(f["field_key"]))
            if error:add(key+"."+f["field_key"]+": "+error,marker=marker,field=f["field_key"])
        rkeys=set()
        for region in marker["regions"]:
            rk=region.get("region_key","");rid=region.get("region_id","")
            if not KEY.fullmatch(rk) or rk.casefold() in rkeys or not valid_guid(rid) or rid in region_ids:add("区域键或GUID无效/重复: "+key+"."+rk,marker=marker,region=region)
            rkeys.add(rk.casefold());region_ids.add(rid);rt=region.get("relative_transform",{})
            if not vector(rt.get("position")) or not vector(rt.get("rotation_pitch_yaw_roll")) or rt.get("scale")!=[1,1,1]:add("区域变换无效: "+key+"."+rk,marker=marker,region=region);continue
            if region["shape_type"] not in kind["allowed_shapes"]:add("类型不允许此区域形状: "+key+"."+rk,marker=marker,region=region)
            error=geometry_error(region)
            if error:add(key+"."+rk+": "+error,marker=marker,region=region)
            else:
                low,high=region_xy_bounds(marker,region);origin=np.asarray(t["origin_cm"]);maximum=origin+np.asarray([t["cells_x"],t["cells_y"]])*300
                if np.any(low<origin-1e-5) or np.any(high>maximum+1e-5):add("区域超出设计边界，数据已保留: "+key+"."+rk,"warning",marker,region)
            if rk=="Territory" and marker.get("enabled") and region.get("enabled"):
                world=region_transform(marker,region);up=rotation(world["rotation_pitch_yaw_roll"])[:,2]
                if region["shape_type"]!="PolygonPrism" or abs(up[2]-1)>1e-6:add("有效辖区须为水平多边形柱体: "+key,marker=marker,region=region)
        if marker.get("type_id")=="Outpost" and marker.get("enabled"):
            outposts.append(marker)
            if not any(r["region_key"]=="Territory" and r["enabled"] for r in marker["regions"]):add("据点缺少有效辖区: "+key,"warning",marker)
            if project["map_package"]==CANONICAL_MAP:
                match=re.fullmatch(r"Outpost_R([1-7])C([1-7])",key)
                params=marker.get("parameters",{})
                if not match or (params.get("BoardRow"),params.get("BoardColumn"))!=(int(match[1]),int(match[2])):add("当前玩法要求据点键与棋盘行列匹配: "+key,"warning",marker)
                if match:
                    row,col=int(match[1]),int(match[2])
                    expected_blue=GAMEPLAY_PROFILE["blue_budgets"][row-1][col-1];expected_red=GAMEPLAY_PROFILE["red_budgets"][row-1][col-1]
                    if (params.get("BlueClusterBudget"),params.get("RedClusterBudget"))!=(expected_blue,expected_red):add(f"当前7×7玩法固定此辖区预算为蓝{expected_blue}簇、红{expected_red}簇: "+key,"warning",marker)
                    expected_x=(int(match[2])-4)*24300;expected_y=(4-int(match[1]))*24300
                    if abs(tr["position"][0]-expected_x)>=12150 or abs(tr["position"][1]-expected_y)>=12150:add("据点锚点超出当前玩法逻辑辖区: "+key,"warning",marker)
                x,y=tr["position"][:2]
                if in_bounds(t,x-1500,y-1500) and in_bounds(t,x+1500,y+1500):
                    samples=[sample_height(t,heights,x+dx,y+dy,True) for dx in (-1500,0,1500) for dy in (-1500,0,1500)]
                    if any(h<=0 or slope>3.0001 for h,slope in samples):add("据点需要海拔大于0、坡度≤3°的30×30米台面: "+key,"warning",marker)
                else:add("据点30×30米台面超出设计边界: "+key,"warning",marker)
    if project["map_package"]==CANONICAL_MAP and len(outposts)!=49:add(f"当前玩法需要49个启用据点，当前{len(outposts)}个。","warning")
    density=project.get("density")
    if density:
        if not valid_guid(density.get("density_map_id")) or not finite(density.get("cell_size_cm")) or density["cell_size_cm"]<=0:add("资源图身份或格尺寸无效");return issues
        layerkeys=set();layerids=set()
        for layer in density["layers"]:
            if layer["layer_key"].casefold() in layerkeys or layer["layer_id"] in layerids or not valid_guid(layer["layer_id"]):add("资源图层键或身份无效/重复")
            layerkeys.add(layer["layer_key"].casefold());layerids.add(layer["layer_id"]);coords=set();outside=0;cell=density["cell_size_cm"]
            for c in layer["cells"]:
                x,y,n=c.get("cell_x"),c.get("cell_y"),c.get("density_u8")
                if any(type(v) is not int for v in (x,y,n)) or not -2147483648<=x<=2147483647 or not -2147483648<=y<=2147483647 or not 0<=n<=255:add("资源格坐标或密度无效");continue
                if (x,y) in coords:add("资源图层内出现重复格")
                coords.add((x,y));outside+=not in_bounds(t,(x+.5)*cell,(y+.5)*cell)
            if outside:add(f"{layer['display_name']}有{outside}格资源位于设计边界外，数据已保留。","warning")
    return issues


def normalized_layout(project):
    layout=copy.deepcopy(project["layout"]);layout["map_package"]=project["map_package"]
    used={m["type_id"] for m in layout["markers"]}
    layout["types"]=sorted((t for t in layout["types"] if t["type_id"] in used),key=lambda t:t["type_id"])
    for t in layout["types"]:
        t["allowed_shapes"].sort();t["fields"].sort(key=lambda f:f["field_key"])
        for f in t["fields"]:f.pop("enum_values",None)
        for r in t["default_regions"]:r["world_transform"]=copy.deepcopy(r["relative_transform"])
    layout["markers"].sort(key=lambda m:m["marker_key"])
    for m in layout["markers"]:
        m["tags"].sort();m["regions"].sort(key=lambda r:r["region_key"])
        for r in m["regions"]:r["world_transform"]=region_transform(m,r)
    return layout


def density_statistics(project):
    d=copy.deepcopy(project.get("density"))
    if not d:return None,[]
    territories=[]
    for m in project["layout"]["markers"]:
        if not m.get("enabled") or m["type_id"]!=d["territory_type_id"]:continue
        for r in m["regions"]:
            if not r.get("enabled") or r["region_key"]!=d["territory_region_key"] or r["shape_type"]!="PolygonPrism":continue
            tr=region_transform(m,r);matrix=rotation(tr["rotation_pitch_yaw_roll"])
            vertices=[(matrix@np.array([*v,0])+tr["position"])[:2].tolist() for v in r["geometry"]["vertices"]]
            if len(vertices)<3:continue
            territories.append({"marker_id":m["marker_id"],"marker_key":m["marker_key"],"region_id":r["region_id"],"vertices":vertices,"bounds":(min(v[0] for v in vertices),min(v[1] for v in vertices),max(v[0] for v in vertices),max(v[1] for v in vertices))})
    territories.sort(key=lambda t:(t["marker_key"],t["region_id"]));summaries=[];anomalies=[];warnings=[];cell=d["cell_size_cm"]
    for layer in sorted(d["layers"],key=lambda l:(l["layer_key"],l["layer_id"])):
        layer["cells"]=sorted((c for c in layer["cells"] if c["density_u8"]),key=lambda c:(c["cell_y"],c["cell_x"]))
        totals=defaultdict(lambda:[0,0]);lookup={t["region_id"]:t for t in territories}
        for c in layer["cells"]:
            x,y=(c["cell_x"]+.5)*cell,(c["cell_y"]+.5)*cell;strict=[];boundary=[]
            for t in territories:
                x0,y0,x1,y1=t["bounds"]
                if x0-1e-7<=x<=x1+1e-7 and y0-1e-7<=y<=y1+1e-7:
                    hit=contains(t["vertices"],x,y)
                    if hit==1:strict.append(t)
                    elif hit==2:boundary.append(t)
            candidates=strict or boundary
            kind="overlap" if len(strict)>1 else candidates[0]["region_id"] if candidates else "unassigned"
            totals[kind][0]+=1;totals[kind][1]+=c["density_u8"]
            if kind in {"overlap","unassigned"}:
                anomalies.append({"assignment_kind":kind,"layer_key":layer["layer_key"],**c,"candidates":[{k:t[k] for k in ("marker_id","marker_key","region_id")} for t in candidates]})
        for key in [t["region_id"] for t in territories]+["unassigned","overlap"]:
            count,total=totals[key];target=lookup.get(key,{})
            summaries.append({"assignment_kind":"territory" if key in lookup else key,"marker_id":target.get("marker_id",""),"marker_key":target.get("marker_key",""),"region_id":target.get("region_id",""),"layer_id":layer["layer_id"],"layer_key":layer["layer_key"],"cell_count":count,"density_weight_sum":total/255,"weighted_area_m2":total/255*cell*cell/10000})
        for kind,label in (("unassigned","未归属"),("overlap","严格重叠")):
            if totals[kind][0]:warnings.append({"severity":"warning","message":f"{layer['display_name']}有{totals[kind][0]}格{label}。"})
        budget_key={"BlueOre":"blue_budgets","RedOre":"red_budgets"}.get(layer["layer_key"])
        if budget_key and project["map_package"]==CANONICAL_MAP:
            for territory in territories:
                match=re.fullmatch(r"Outpost_R([1-7])C([1-7])",territory["marker_key"])
                if not match:continue
                budget=GAMEPLAY_PROFILE[budget_key][int(match[1])-1][int(match[2])-1]
                if budget and not totals[territory["region_id"]][1]:warnings.append({"severity":"warning","marker_id":territory["marker_id"],"region_id":territory["region_id"],"message":f"{territory['marker_key']} 的{layer['display_name']}预算为{budget}簇，但辖区没有有效密度。"})
    d["layers"].sort(key=lambda l:(l["layer_key"],l["layer_id"]));d["map_package"]=project["map_package"];d["territory_summary"]=summaries;d["anomalies"]=anomalies
    return d,warnings


def csv_bytes(rows):
    out=io.StringIO(newline="");writer=csv.writer(out,lineterminator="\n")
    for row in rows:
        writer.writerow([canonical(v) if isinstance(v,(bool,int,float)) else v for v in row])
    return out.getvalue().encode("utf-8")


def legacy_files(project,density=None):
    layout=normalized_layout(project)
    rows={
        "markers.csv":["marker_id,marker_key,type_id,display_name,enabled,note,world_x_cm,world_y_cm,world_z_cm,pitch_deg,yaw_deg,roll_deg".split(",")],
        "regions.csv":["marker_id,region_id,region_key,display_name,enabled,shape_type,local_x_cm,local_y_cm,local_z_cm,local_pitch_deg,local_yaw_deg,local_roll_deg,world_x_cm,world_y_cm,world_z_cm,world_pitch_deg,world_yaw_deg,world_roll_deg,radius_cm,half_x_cm,half_y_cm,half_z_cm,min_z_cm,max_z_cm".split(",")],
        "vertices.csv":["marker_id,region_id,vertex_index,local_x_cm,local_y_cm,world_x_cm,world_y_cm,world_z_cm".split(",")],
        "properties.csv":["marker_id,region_id,field_id,field_key,field_type,value,x,y,z".split(",")],"tags.csv":[["marker_id","tag"]]}
    types={t["type_id"]:t for t in project["layout"]["types"]}
    for m in layout["markers"]:
        mid=m["marker_id"];tr=m["world_transform"];rows["markers.csv"].append([mid,m["marker_key"],m["type_id"],m["display_name"],m["enabled"],m["note"],*tr["position"],*tr["rotation_pitch_yaw_roll"]])
        for f in sorted(types[m["type_id"]]["fields"],key=lambda f:f["field_key"]):
            value=m["parameters"][f["field_key"]];vector_value=value if f["field_type"]=="vector" else ["","",""]
            rows["properties.csv"].append([mid,"",f["field_id"],f["field_key"],f["field_type"],"" if f["field_type"]=="vector" else value if isinstance(value,str) else canonical(value),*vector_value])
        for tag in m["tags"]:rows["tags.csv"].append([mid,tag])
        for r in m["regions"]:
            rid=r["region_id"];g=r["geometry"];local=r["relative_transform"];world=r["world_transform"]
            rows["regions.csv"].append([mid,rid,r["region_key"],r["display_name"],r["enabled"],r["shape_type"],*local["position"],*local["rotation_pitch_yaw_roll"],*world["position"],*world["rotation_pitch_yaw_roll"],g.get("radius",""),*g.get("half_extents",["","",""]),g.get("min_z",""),g.get("max_z","")])
            if r["shape_type"]=="PolygonPrism":
                matrix=rotation(world["rotation_pitch_yaw_roll"])
                for i,v in enumerate(g["vertices"]):rows["vertices.csv"].append([mid,rid,i,*v,*(matrix@np.array([*v,0])+world["position"]).tolist()])
            for key in sorted(g):rows["properties.csv"].append([mid,rid,"",key,"shape_json",canonical(g[key]),"","",""])
    files={name:csv_bytes(value) for name,value in rows.items()};files["layout.json"]=json_bytes(layout)
    if density:
        files["density_layers.json"]=json_bytes(density);cell=density["cell_size_cm"]
        rows=[["layer_id","layer_key","cell_x","cell_y","world_center_x_cm","world_center_y_cm","density_u8","density_01"]]
        for layer in density["layers"]:
            for c in layer["cells"]:rows.append([layer["layer_id"],layer["layer_key"],c["cell_x"],c["cell_y"],(c["cell_x"]+.5)*cell,(c["cell_y"]+.5)*cell,c["density_u8"],c["density_u8"]/255])
        files["density_cells.csv"]=csv_bytes(rows)
        columns="assignment_kind,marker_id,marker_key,region_id,layer_id,layer_key,cell_count,density_weight_sum,weighted_area_m2".split(",")
        files["density_territories.csv"]=csv_bytes([columns]+[[v[k] for k in columns] for v in density["territory_summary"]])
    return files
