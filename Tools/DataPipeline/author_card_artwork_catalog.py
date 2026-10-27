"""One-time adoption of captured UE artwork; all XLSX writes use excelize-cli.

Never reinitialize an existing catalog. Daily edits belong in Excel.
"""
import hashlib
import json
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "Artifacts/CardArtworkPipeline"
CLI = Path("D:/UE5.7/excelize-cli/bin/xlsx.exe")
BOOK = ROOT / "data/Excel/GuLiStrikeCardArtwork.xlsx"


def cli(*args):
    result = subprocess.run([str(CLI), *map(str, args)], check=True, capture_output=True, encoding="utf-8")
    return result.stdout


def column(number):
    value = ""
    while number:
        number, remainder = divmod(number - 1, 26)
        value = chr(65 + remainder) + value
    return value


def write_sheet(book, sheet, fields, records):
    cells = []
    for index, (name, kind, required) in enumerate(fields, 1):
        for row, value in ((1, name), (2, kind), (3, "Necessary" if required else "Optional")):
            cells.append({"cell": column(index) + str(row), "value": value, "type": "string"})
    for index, record in enumerate(records, 4):
        for n, (name, kind, _) in enumerate(fields, 1):
            default = 0.0 if kind == "float" else 0 if kind == "int" else ""
            cells.append({"cell": column(n) + str(index), "value": record.get(name, default),
                          "type": kind if kind in ("int", "float", "bool") else "string"})
    payload = OUT / (sheet + "-cells.json")
    payload.write_text(json.dumps(cells, ensure_ascii=False), encoding="utf-8")
    cli("write", book, "--sheet", sheet, "--data-file", payload)
    cli("style", book, "--sheet", sheet, "--range", f"A1:{column(len(fields))}1", "--bold", "--bg", "D9E1F2")
    cli("col-width", book, "--sheet", sheet, "--col", "A", "--to", column(len(fields)), "--width", "22")


def source_file(raw):
    value = raw.replace("\\", "/")
    # Import metadata from the old editor can contain engine-relative paths.
    # Resolve the exact recorded ArtSource suffix against this project, no search.
    if "ArtSource/" in value:
        candidate = ROOT / value[value.index("ArtSource/"):]
        if candidate.is_file():
            return candidate.relative_to(ROOT).as_posix()
    path = Path(raw) if raw else None
    if path and path.is_file():
        try:
            return path.resolve().relative_to(ROOT).as_posix()
        except ValueError:
            return str(path)
    return raw


def main():
    assert not BOOK.exists(), "Catalog already exists. Edit it instead of reseeding UE values."
    baseline_path = OUT / "baseline.json"
    snapshot = json.loads(baseline_path.read_text(encoding="utf-8"))
    assert snapshot["success"] and len(snapshot["artworks"]) == 6
    cards_path = ROOT / "data/Excel/GuLiStrikeRogueCards.xlsx"
    cards_hash = hashlib.sha256(cards_path.read_bytes()).hexdigest()
    rows = json.loads(cli("read", cards_path, "--sheet", "Cards", "--format", "json", "--raw"))["rows"]
    assert "FrontMaterial" in rows[0] and "ArtworkId" not in rows[0]
    baseline_cards = {row["Id"]: row for row in snapshot["gameplay_cards"]}
    id_column, material_column = rows[0].index("id"), rows[0].index("FrontMaterial")
    material_ids = {a["material"]: a["id"] for a in snapshot["artworks"]}
    changes = [{"cell": column(material_column + 1) + "1", "value": "ArtworkId", "type": "string"},
               {"cell": column(material_column + 1) + "2", "value": "int", "type": "string"}]
    for index, row in enumerate(rows[3:], 4):
        if not row or not row[id_column]:
            continue
        assert row[id_column] in baseline_cards
        assert row[material_column] == baseline_cards[row[id_column]]["FrontMaterial"]
        changes.append({"cell": column(material_column + 1) + str(index),
                        "value": material_ids[row[material_column]], "type": "int"})
    standard = [("id", "int", True), ("name", "str", True), ("Note", "str", False)]
    schemas = {
        "Templates": standard + [("ParentMaterial", "softobject", True), ("Layout", "str", True), ("CompositePreset", "str", True)],
        "Artworks": standard + [("Title", "str", True), ("TemplateId", "int", True), ("Material", "softobject", True),
                                ("AssetRoot", "str", True), ("SourceRoot", "str", False), ("ReferenceFile", "str", False), ("PreviewUrl", "str", False)],
        "Layers": standard + [("ArtworkId", "int", True), ("LayerIndex", "int", True), ("Label", "str", True),
                              ("TextureParameter", "str", True)] +
                  [(key, "float", True) for key in ("Depth", "Opacity", "ScaleU", "ScaleV", "ScaleB", "ScaleA", "OffsetU", "OffsetV", "OffsetB", "OffsetA")],
        "Parameters": standard + [("ArtworkId", "int", True), ("Key", "str", True), ("Kind", "str", True)] +
                      [(key, "float", False) for key in ("ScalarValue", "VectorR", "VectorG", "VectorB", "VectorA")] +
                      [("Texture", "softobject", False), ("SourceFile", "str", False), ("TextureRole", "str", False)],
    }
    records = {sheet: [] for sheet in schemas}
    for ident, name, layout, preset in ((1, "ModelComicAtlasV9", "Atlas3x2", "ModelComicAtlasV9"),
                                         (2, "RegisteredSix", "Independent6", "RegisteredSix"),
                                         (3, "RegisteredSixForegroundMask", "Independent6", "RegisteredSixForegroundMask")):
        parents = {a["parent"] for a in snapshot["artworks"] if a["template_id"] == ident}
        assert len(parents) == 1
        records["Templates"].append({"id": ident, "name": name, "ParentMaterial": parents.pop(), "Layout": layout,
                                     "CompositePreset": preset, "Note": "保留现有着色器算法与固定合成规则；本表选择模板，不重写HLSL。"})
    resolution = []
    for artwork in snapshot["artworks"]:
        ident = artwork["id"]
        files = [source_file(v["source"]) for v in artwork["textures"].values()]
        own_sources = [f for f in files if f.startswith("ArtSource/") and (ROOT / f).is_file()]
        source_root = Path(own_sources[0]).parent if own_sources else Path("")
        if source_root.name in ("Layers", "Masks"):
            source_root = source_root.parent
        reference = source_root / "Master.png"
        records["Artworks"].append({"id": ident, "name": artwork["name"], "Title": artwork["title"],
                                   "TemplateId": artwork["template_id"], "Material": artwork["material"],
                                   "AssetRoot": artwork["material"].split("/Materials/")[0],
                                   "SourceRoot": source_root.as_posix() if own_sources else "",
                                   "ReferenceFile": reference.as_posix() if (ROOT / reference).is_file() else "",
                                   "PreviewUrl": "http://127.0.0.1:8191/preview.html" if ident == 6 else "",
                                   "Note": "仅美术资源；尚未加入玩法卡表。" if ident == 6 else "从当前UE实际引用与参数纳管，保留原视觉配置。"})
        labels = ["近景", "能力效果", "主体", "近场景", "中景", "远景"] if ident != 6 else ["近景", "速度线", "机甲", "场景", "折射弹道和三个敌方机甲", "远景"]
        common = set()
        for index in range(1, 7):
            prefix = f"Layer {index} "
            common.update(prefix + key for key in ("Depth", "Opacity", "Scale", "Offset"))
            scale, offset = artwork["vectors"][prefix + "Scale"], artwork["vectors"][prefix + "Offset"]
            records["Layers"].append({"id": ident * 10 + index, "name": artwork["name"] + f"_Layer{index}",
                                      "ArtworkId": ident, "LayerIndex": index, "Label": labels[index - 1],
                                      "TextureParameter": "BaseColor Map" if artwork["template_id"] == 1 else prefix + "Map",
                                      "Depth": artwork["scalars"][prefix + "Depth"], "Opacity": artwork["scalars"][prefix + "Opacity"],
                                      **dict(zip(("ScaleU", "ScaleV", "ScaleB", "ScaleA"), scale)),
                                      **dict(zip(("OffsetU", "OffsetV", "OffsetB", "OffsetA"), offset))})
        values = [(key, "Scalar", value) for key, value in artwork["scalars"].items() if key not in common]
        values += [(key, "Vector", value) for key, value in artwork["vectors"].items() if key not in common]
        values += [(key, "Texture", value) for key, value in artwork["textures"].items()]
        for n, (key, kind, value) in enumerate(sorted(values), 1):
            row = {"id": ident * 1000 + n, "name": artwork["name"] + f"_Parameter{n}", "ArtworkId": ident, "Key": key, "Kind": kind}
            if kind == "Scalar":
                row["ScalarValue"] = value
            elif kind == "Vector":
                row.update(zip(("VectorR", "VectorG", "VectorB", "VectorA"), value))
            else:
                source = source_file(value["source"])
                row.update(Texture=value["asset"], SourceFile=source,
                           TextureRole="Mask" if key == "Foreground Front Mask" else "Color")
                resolution.append({"artwork_id": ident, "parameter": key, "asset": value["asset"],
                                   "import_metadata_source": value["source"], "registered_source": source,
                                   "exists": bool(source and (ROOT / source).is_file())})
            records["Parameters"].append(row)
    cli("new", BOOK, "--sheet", "Templates")
    for sheet in schemas:
        if sheet != "Templates":
            cli("add-sheet", BOOK, "--name", sheet)
        write_sheet(BOOK, sheet, schemas[sheet], records[sheet])
    assert hashlib.sha256(cards_path.read_bytes()).hexdigest() == cards_hash, "Cards Excel changed concurrently"
    payload = OUT / "Cards-artwork-migration-cells.json"
    payload.write_text(json.dumps(changes, ensure_ascii=False), encoding="utf-8")
    cli("write", cards_path, "--sheet", "Cards", "--data-file", payload)
    (OUT / "source-resolution.json").write_text(json.dumps(resolution, ensure_ascii=False, indent=2), encoding="utf-8")
    print(json.dumps({sheet: len(rows) for sheet, rows in records.items()}))


if __name__ == "__main__":
    main()
