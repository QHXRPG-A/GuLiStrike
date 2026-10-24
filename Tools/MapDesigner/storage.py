"""Local project bundles and atomic export batches for GuLi Map Designer."""
from __future__ import annotations

import copy
import datetime as dt
import io
import json
import os
from pathlib import Path
import uuid
import zipfile

import numpy as np
from PIL import Image

from domain import SCHEMA, canonical, density_statistics, digest, json_bytes, legacy_files, normalized_layout, terrain_dimensions, terrain_shape, validate, valid_guid
from occupancy import build as build_occupancy

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "ArtSource" / "MapDesigner"
PROJECTS = DATA / "Projects"
SNAPSHOTS = DATA / "Snapshots"
EXPORTS = DATA / "Exports"
BACKUPS = DATA / "Backups"
SOURCES = DATA / "Sources"


def init_storage():
    for path in (PROJECTS, SNAPSHOTS, EXPORTS, BACKUPS, SOURCES):
        path.mkdir(parents=True, exist_ok=True)


def ident(value):
    if not valid_guid(value):
        raise ValueError("工程身份无效")
    return str(uuid.UUID(value))


def safe_leaf(value):
    if not isinstance(value, str) or not value or Path(value).name != value or any(c in value for c in ("/", "\\", ":")) or value in {".", ".."}:
        raise ValueError("无效文件名")
    return value


def atomic_bytes(path, data):
    temporary = path.with_name(path.name + ".pending-" + uuid.uuid4().hex)
    with temporary.open("xb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())
    temporary.replace(path)


def source_directory(project):
    source = project.get("source", {})
    if source.get("snapshot_id"):
        return SNAPSHOTS / ident(source["snapshot_id"])
    return SOURCES / ident(source.get("source_id",project["id"]))


def source_files(project):
    directory = source_directory(project)
    names = project.get("source", {}).get("files", {})
    result = {}
    for name, expected in sorted(names.items()):
        path = directory / safe_leaf(name)
        data = path.read_bytes()
        if digest(data) != expected:
            raise ValueError("原始快照已变化: " + name)
        result[name] = data
    return result


def project_envelope(project):
    if not isinstance(project, dict) or project.get("schema") != SCHEMA:
        raise ValueError("不支持的工程版本")
    ident(project.get("id"))
    if not isinstance(project.get("name"), str) or not project["name"].strip() or not isinstance(project.get("map_package"), str):
        raise ValueError("工程名称或地图路径无效")
    for key in ("terrain", "layout", "editor", "source", "baseline"):
        if not isinstance(project.get(key), dict):raise ValueError("工程缺少对象: "+key)
    for key in ("markers", "types"):
        if not isinstance(project["layout"].get(key), list):raise ValueError("工程缺少列表: "+key)
    for key in ("ground_follow", "field_layouts"):
        if not isinstance(project["editor"].get(key), dict):raise ValueError("工程缺少编辑数据: "+key)
    density=project.get("density")
    if density is not None and (not isinstance(density,dict) or not isinstance(density.get("layers"),list)):
        raise ValueError("工程资源数据无效")
    if not isinstance(project["source"].get("files",{}),dict):raise ValueError("工程原始快照目录无效")


def bundle(project):
    project_envelope(project)
    heights = terrain_shape(project)
    manifest = copy.deepcopy(project)
    del manifest["terrain"]["heights_cm"]
    raw = heights.astype("<f8").tobytes()
    manifest["terrain"]["height_file"] = "heights_cm.f64"
    manifest["terrain"]["height_sha256"] = digest(raw)
    files = source_files(project)
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=5) as archive:
        for name, data in {"manifest.json": json_bytes(manifest), "heights_cm.f64": raw, **{"source/"+k: v for k, v in files.items()}}.items():
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            archive.writestr(info, data)
    return output.getvalue()


def unpack(data, install_sources=True):
    if len(data) > 256*1024*1024:
        raise ValueError("工程包过大")
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        info = archive.infolist()
        if len(info)>256 or sum(v.file_size for v in info)>512*1024*1024 or len({v.filename for v in info})!=len(info):
            raise ValueError("工程包条目无效或解压大小超限")
        for entry in info:
            name=entry.filename
            if name not in {"manifest.json","heights_cm.f64"} and not (name.startswith("source/") and safe_leaf(name[7:])):
                raise ValueError("工程包包含不支持的条目")
        p=json.loads(archive.read("manifest.json"),parse_constant=lambda value:(_ for _ in ()).throw(ValueError("工程数字无效: "+value)))
        project_envelope(p)
        p["id"]=ident(p["id"])
        nx,ny=terrain_dimensions(p["terrain"])
        if archive.getinfo("heights_cm.f64").file_size!=(nx+1)*(ny+1)*8:
            raise ValueError("工程高度文件尺寸与网格不一致")
        raw=archive.read("heights_cm.f64")
        if digest(raw)!=p["terrain"]["height_sha256"] or len(raw)%8:
            raise ValueError("工程高度校验失败")
        p["terrain"]["heights_cm"]=np.frombuffer(raw,dtype="<f8").tolist()
        p["terrain"].pop("height_file",None);p["terrain"].pop("height_sha256",None)
        terrain_shape(p)
        # External packages carry their own immutable source; no machine path is trusted.
        if install_sources:
            directory=SOURCES/p["id"];directory.mkdir(exist_ok=True)
            for name,expected in p.get("source",{}).get("files",{}).items():
                contents=archive.read("source/"+safe_leaf(name))
                if digest(contents)!=expected:raise ValueError("原始快照校验失败: "+name)
                path=directory/name
                if path.exists() and digest(path.read_bytes())!=expected:
                    raise ValueError("同一工程身份存在不同原始快照，请另存工程后导入")
                if not path.exists():atomic_bytes(path,contents)
            p.get("source",{}).pop("snapshot_id",None)
            p.setdefault("source",{})["source_id"]=p["id"]
        return p


def paths(project_id):
    project_id=ident(project_id)
    return PROJECTS/(project_id+".gmap"),PROJECTS/(project_id+".autosave.gmap")


def newest(project_id):
    candidates=[p for p in paths(project_id) if p.exists()]
    return max(candidates,key=lambda p:p.stat().st_mtime_ns) if candidates else None


def save(project, autosave=False, expected_revision=None):
    project_id=ident(project["id"]);previous=newest(project_id)
    if expected_revision is not None and previous and digest(previous.read_bytes())!=expected_revision:
        raise FileExistsError("另一个窗口已保存此工程。请重新打开最新工程，避免覆盖其修改。")
    data=bundle(project);path=paths(project_id)[1 if autosave else 0];atomic_bytes(path,data)
    return {"id":project_id,"path":str(path),"revision":digest(data),"autosave":autosave}


def open_project(project_id):
    path=newest(project_id)
    if not path:raise FileNotFoundError("工程不存在")
    data=path.read_bytes();project=unpack(data,install_sources=True)
    return {"project":project,"revision":digest(data),"path":str(path),"recovered":path.name.endswith(".autosave.gmap")}


def project_list():
    result=[]
    for path in PROJECTS.glob("*.gmap"):
        key=path.name.split(".")[0]
        if not valid_guid(key) or any(v["id"]==key for v in result):continue
        selected=newest(key)
        try:
            with zipfile.ZipFile(selected) as archive:
                p=json.loads(archive.read("manifest.json"))
            result.append({"id":key,"name":p["name"],"map_package":p["map_package"],"cells_x":p["terrain"]["cells_x"],"cells_y":p["terrain"]["cells_y"],"modified":selected.stat().st_mtime,"recovered":selected.name.endswith(".autosave.gmap")})
        except (ValueError,KeyError,zipfile.BadZipFile):continue
    return sorted(result,key=lambda v:v["modified"],reverse=True)


def import_snapshot(directory):
    directory=Path(directory).resolve()
    if directory.parent!=SNAPSHOTS.resolve():raise ValueError("快照目录不在本软件的Snapshots目录中")
    m=json.loads((directory/"manifest.json").read_text(encoding="utf-8"))
    if m.get("schema")!="guli-map-design/snapshot-v1":raise ValueError("快照版本不支持")
    raw=np.asarray(Image.open(directory/safe_leaf(m["height_file"])),dtype=np.uint16)
    nx,ny=raw.shape[1]-1,raw.shape[0]-1
    if m["scale"][:2]!=[300,300]:raise ValueError("当前地形顶点间距不是3米，请先明确重采样方案")
    heights=(raw.astype(np.float64)-32768)/128*m["scale"][2]+m["location"][2]
    layout=json.loads((directory/"layout.json").read_text(encoding="utf-8"))
    catalog=json.loads((directory/"type_catalog.json").read_text(encoding="utf-8"))
    layout["types"]=catalog["types"]
    density_path=directory/"density_layers.json";density=json.loads(density_path.read_text(encoding="utf-8")) if density_path.exists() else None
    project_id=str(uuid.uuid4());files={p.name:digest(p.read_bytes()) for p in directory.iterdir() if p.is_file()}
    m["baseline"]["height_u16"]=digest(raw.astype("<u2").tobytes())
    project={"schema":SCHEMA,"id":project_id,"name":"指挥官地图 · 当前快照","map_package":m["map_package"],
             "terrain":{"cells_x":nx,"cells_y":ny,"cell_size_cm":300,"origin_cm":m["location"][:2],"heights_cm":heights.reshape(-1).tolist()},
             "layout":layout,"density":density,"editor":{"ground_follow":{v["marker_id"]:True for v in layout["markers"]},"field_layouts":{v["marker_id"]:copy.deepcopy(next(t["fields"] for t in layout["types"] if t["type_id"]==v["type_id"])) for v in layout["markers"]}},
             "source":{**m,"snapshot_id":directory.name,"files":files},"baseline":m["baseline"]}
    settings=directory/'editor.json'
    if settings.exists():project['editor']['ground_follow'].update(json.loads(settings.read_text(encoding='utf-8')).get('ground_follow',{}))
    saved=save(project)
    return {"project":project,**saved,"warnings":m.get("warnings",[])}


def encode_heights(project, heights):
    source=project.get("source",{});t=project["terrain"]
    scale=float(source.get("scale",[300,300,100])[2]);location=float(source.get("location",[0,0,0])[2])
    if not math_is_positive(scale):scale=100
    minimum,maximum=float(heights.min()),float(heights.max())
    low=location-256*scale;high=location+(32767/128)*scale
    if minimum<low or maximum>high:
        location=(minimum+maximum)/2
        scale=max((maximum-minimum)*128/65534,0.128)
    raw=np.rint((heights-location)*128/scale+32768)
    if raw.min()<0 or raw.max()>65535:raise ValueError("高度编码超出16位范围")
    raw=raw.astype("<u2");quantized=(raw.astype(np.float64)-32768)/128*scale+location
    return raw,quantized,[*t["origin_cm"],location],[300,300,scale]


def math_is_positive(v):
    return np.isfinite(v) and v>0


def identity_diff(before, after, identity, nested=None):
    old={v[identity]:v for v in before};new={v[identity]:v for v in after}
    def comparable(item):return {k:v for k,v in item.items() if k not in {"world_transform"} or identity=="marker_id"}
    changed=[]
    for key in sorted(old.keys()&new.keys()):
        a,b=comparable(old[key]),comparable(new[key]);children=None
        if nested:
            field,child_id=nested;children=identity_diff(a.pop(field,[]),b.pop(field,[]),child_id)
        fields=sorted(k for k in a.keys()|b.keys() if a.get(k)!=b.get(k))
        if children and any(children.values()):fields.append(nested[0])
        if not fields:continue
        item={identity:key,"changed_fields":fields}
        if children is not None:item[nested[0]]=children
        changed.append(item)
    return {"created":sorted(new.keys()-old.keys()),"deleted":sorted(old.keys()-new.keys()),"updated":changed}


def authoring_diff(project, originals, density):
    old_layout=json.loads(originals.get("layout.json",b'{"markers":[]}'))
    old_catalog=json.loads(originals.get("type_catalog.json",b'{"types":[]}'))
    old_density=json.loads(originals.get("density_layers.json",b'null'))
    layout=normalized_layout(project)
    report={"markers":identity_diff(old_layout["markers"],layout["markers"],"marker_id",("regions","region_id")),
            "types":identity_diff(old_catalog["types"],project["layout"]["types"],"asset_path",("fields","field_id")),"density":[]}
    old_layers={l["layer_id"]:l for l in (old_density or {}).get("layers",[])}
    new_layers={l["layer_id"]:l for l in (density or {}).get("layers",[])}
    for key in sorted(old_layers.keys()|new_layers.keys()):
        a,b=old_layers.get(key,{}),new_layers.get(key,{})
        previous={(c["cell_x"],c["cell_y"]):c["density_u8"] for c in a.get("cells",[])}
        current={(c["cell_x"],c["cell_y"]):c["density_u8"] for c in b.get("cells",[])}
        report["density"].append({"layer_id":key,"layer_key":b.get("layer_key",a.get("layer_key")),"created_cells":len(current.keys()-previous.keys()),
                                  "deleted_cells":len(previous.keys()-current.keys()),"changed_cells":sum(previous[k]!=current[k] for k in previous.keys()&current.keys()),
                                  "weight_u8_before":sum(previous.values()),"weight_u8_after":sum(current.values())})
    report["density_cell_size_changed"]=(old_density or {}).get("cell_size_cm")!=(density or {}).get("cell_size_cm")
    old_editor=json.loads(originals.get("editor.json",b'{}'))
    report["ground_follow_changed"]={k:v for k,v in project["editor"]["ground_follow"].items() if old_editor.get("ground_follow",{}).get(k,True)!=v}
    return report


def export_project(project):
    heights=terrain_shape(project);issues=validate(project)
    if any(v["severity"]=="error" for v in issues):raise ValueError("请修复数据错误后导出: "+"；".join(v["message"] for v in issues if v["severity"]=="error")[:1000])
    density,warnings=density_statistics(project);issues.extend(warnings)
    files=legacy_files(project,density);t=project["terrain"];nx,ny=t["cells_x"],t["cells_y"]
    px,py=((nx+62)//63)*63,((ny+62)//63)*63
    padded=np.pad(heights,((0,py-ny),(0,px-nx)),mode="edge")
    raw,quantized,location,scale=encode_heights(project,padded)
    output=io.BytesIO();Image.fromarray(raw).save(output,format="PNG");files["height.png"]=output.getvalue();files["height.r16"]=raw.tobytes()
    files["heights_cm.f64"]=heights.astype("<f8").tobytes();files["heights_cm.f32"]=quantized[:ny+1,:nx+1].astype("<f4").tobytes()
    files["editor.json"]=json_bytes(project.get("editor",{}))
    catalog={"schema_version":1,"types":copy.deepcopy(project["layout"]["types"])}
    catalog["types"].sort(key=lambda v:v["type_id"]);files["type_catalog.json"]=json_bytes(catalog)
    originals=source_files(project);source=project.get("source",{});layers=[];water=None
    for original,baseline_name in (("layout.json","baseline_layout.json"),("density_layers.json","baseline_density_layers.json"),("type_catalog.json","baseline_type_catalog.json")):
        files[baseline_name]=originals.get(original,b"")
    for i,layer in enumerate(source.get("weights",[])):
        name=safe_leaf(layer["layer"])+".u8"
        if source.get("new_map"):
            values=np.full((py+1,px+1),255 if layer["layer"]=="FlatGround" else 0,dtype=np.uint8)
        else:
            original=np.asarray(Image.open(io.BytesIO(originals[layer["file"]])),dtype=np.uint8)
            # Crop/extend in world coordinates. The original terrain origin is immutable.
            yy=np.minimum(np.arange(py+1),original.shape[0]-1);xx=np.minimum(np.arange(px+1),original.shape[1]-1)
            values=original[yy[:,None],xx]
        files[name]=values.tobytes();layers.append({**layer,"file":name,"sha256":digest(files[name])})
        if layer["layer"]=="Water":water=values[:ny+1,:nx+1]
    geometry=json.loads(originals.get("static_geometry.json",b"[]"))
    occupancy,hierarchies=build_occupancy(quantized[:ny+1,:nx+1],water,np.asarray(t["origin_cm"]),geometry)
    files.update(occupancy)
    summary={"project_id":project["id"],"name":project["name"],"logical_cells":[nx,ny],"landscape_cells":[px,py],"markers":len(project["layout"]["markers"]),"density_cells":{l["layer_key"]:len(l["cells"]) for l in density["layers"]} if density else {},"issues":issues,"baseline":project.get("baseline",{}),"authoring_diff":authoring_diff(project,originals,density)}
    if "height.png" in originals:
        old_raw=np.asarray(Image.open(io.BytesIO(originals["height.png"])),dtype=np.uint16);old=(old_raw.astype(np.float64)-32768)/128*source["scale"][2]+source["location"][2]
        sx,sy=min(old.shape[1],nx+1),min(old.shape[0],ny+1);diff=heights[:sy,:sx]-old[:sy,:sx]
        summary["changed_vertices"]=int(np.count_nonzero(np.abs(diff)>1e-8));summary["height_delta_range_cm"]=[float(diff.min()),float(diff.max())]
        summary["original_cells"]=[old.shape[1]-1,old.shape[0]-1]
        summary["added_vertices"]=(nx+1)*(ny+1)-sx*sy;summary["cropped_vertices"]=old.size-sx*sy
        changed=np.argwhere(np.abs(diff)>1e-8)
        summary["changed_vertex_bounds_xy"]=None if not len(changed) else [changed.min(axis=0)[::-1].tolist(),changed.max(axis=0)[::-1].tolist()]
    files["changes.json"]=json_bytes(summary)
    manifest={"schema":"guli-map-design/export-v1","project_id":project["id"],"map_package":project["map_package"],"cell_size_cm":300,"logical_cells":[nx,ny],"logical_resolution":[nx+1,ny+1],"origin_cm":t["origin_cm"],"logical_bounds_cm":[t["origin_cm"],[t["origin_cm"][0]+nx*300,t["origin_cm"][1]+ny*300]],"landscape_cells":[px,py],"resolution":[px+1,py+1],"components":[px//63,py//63],"quads_per_component":63,"location":location,"scale":scale,"height_quantum_cm":scale[2]/128,"height_maximum_error_cm":float(np.max(np.abs(quantized-padded))),"baseline":project.get("baseline",{}),"source_location":source.get("location"),"source_scale":source.get("scale"),"source_resolution":source.get("resolution"),"material":source.get("material",""),"weights":layers,"files":{k:digest(v) for k,v in sorted(files.items())}}
    manifest["hierarchies"]=hierarchies
    manifest["source_hash"]=digest(json_bytes({"logical_cells":[nx,ny],"origin_cm":t["origin_cm"],"heights":manifest["files"]["heights_cm.f32"],"walkable":manifest["files"]["walkable.u8"],"weights":{v["layer"]:v["sha256"] for v in layers}}))
    files["manifest.json"]=json_bytes(manifest)
    ident(project["id"]);stage=EXPORTS/(project["id"]+".staging-"+uuid.uuid4().hex);stage.mkdir()
    for name,data in files.items():(stage/safe_leaf(name)).write_bytes(data)
    for name,data in files.items():
        if digest((stage/name).read_bytes())!=digest(data):raise OSError("导出读回校验失败: "+name)
    final=EXPORTS/project["id"];previous=EXPORTS/(project["id"]+".previous")
    # Only known direct children of our workspace data directory are renamed.
    if stage.resolve().parent!=EXPORTS.resolve() or final.resolve().parent!=EXPORTS.resolve():raise ValueError("导出目录越界")
    if previous.exists():previous.replace(BACKUPS/(project["id"]+"-"+uuid.uuid4().hex))
    if final.exists():final.replace(previous)
    try:stage.replace(final)
    except OSError:
        if previous.exists() and not final.exists():previous.replace(final)
        raise
    return {"directory":str(final),"manifest":manifest,"summary":summary,"files":sorted(files)}
