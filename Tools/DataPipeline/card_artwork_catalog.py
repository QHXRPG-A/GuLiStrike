"""Pure validation/resolution for the Excel-owned card artwork catalog."""
import math
import re
from pathlib import Path

STEM = "GuLiStrikeCardArtwork"
SHEETS = ("Templates", "Artworks", "Layers", "Parameters")
TABLES = tuple("DT_" + STEM + "_" + sheet for sheet in SHEETS)
PRESETS = {
    "ModelComicAtlasV9": "Atlas3x2",
    "RegisteredSix": "Independent6",
    "RegisteredSixForegroundMask": "Independent6",
}
COMMON_PARAMETER = re.compile(r"Layer [1-6] (?:Depth|Opacity|Scale|Offset)$")


class CatalogError(ValueError):
    pass


def _fail(sheet, row, message):
    raise CatalogError(f"{STEM}.xlsx / {sheet} id={row.get('Id', '?')} ({row.get('Name', '?')}): {message}")


def object_path(value):
    return isinstance(value, str) and bool(re.fullmatch(r"/(?:Game|Engine)/(?:[A-Za-z0-9_]+/)*([A-Za-z0-9_]+)\.\1", value))


def _rows(tables, sheet):
    entry = tables.get("DT_" + STEM + "_" + sheet)
    if entry is None:
        raise CatalogError(f"Missing {STEM}.xlsx / {sheet}")
    return entry["rows"] if isinstance(entry, dict) else entry


def _index(rows, sheet):
    result, names = {}, set()
    for row in rows:
        ident = row.get("Id")
        if not isinstance(ident, int) or isinstance(ident, bool) or ident <= 0 or ident in result:
            _fail(sheet, row, "id must be a unique positive integer")
        if not row.get("Name") or row["Name"] in names:
            _fail(sheet, row, "name must be nonempty and unique")
        result[ident] = row
        names.add(row["Name"])
    return result


def validate_catalog(tables):
    """Return indexed data; never touch source files, generated files or UE assets."""
    templates = _index(_rows(tables, "Templates"), "Templates")
    artworks = _index(_rows(tables, "Artworks"), "Artworks")
    layers = _index(_rows(tables, "Layers"), "Layers")
    parameters = _index(_rows(tables, "Parameters"), "Parameters")
    if not templates or not artworks:
        raise CatalogError("Artwork and template catalogs must not be empty")
    for row in templates.values():
        if row["CompositePreset"] not in PRESETS or PRESETS[row["CompositePreset"]] != row["Layout"]:
            _fail("Templates", row, "unsupported template/layout combination")
        if not object_path(row["ParentMaterial"]):
            _fail("Templates", row, "ParentMaterial requires a full object path")
    materials = set()
    for row in artworks.values():
        if row["TemplateId"] not in templates:
            _fail("Artworks", row, "unknown TemplateId")
        if not object_path(row["Material"]) or row["Material"] in materials:
            _fail("Artworks", row, "Material requires a unique full object path")
        root = row["AssetRoot"].rstrip("/")
        if not root.startswith("/Game/GuLiStrike/Cards/") or not row["Material"].startswith(root + "/"):
            _fail("Artworks", row, "Material must belong to its project card AssetRoot")
        materials.add(row["Material"])
    by_parameters = {ident: {} for ident in artworks}
    texture_roles = {}
    for row in parameters.values():
        ident, key, kind = row["ArtworkId"], row["Key"], row["Kind"]
        if ident not in artworks:
            _fail("Parameters", row, "unknown ArtworkId")
        if not key or key in by_parameters[ident] or COMMON_PARAMETER.fullmatch(key):
            _fail("Parameters", row, "duplicate key or layer parameter also maintained in Layers")
        if kind not in ("Scalar", "Vector", "Texture"):
            _fail("Parameters", row, "Kind must be Scalar, Vector or Texture")
        values = [row[k] for k in ("ScalarValue", "VectorR", "VectorG", "VectorB", "VectorA")]
        if not all(isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v) for v in values):
            _fail("Parameters", row, "parameter values must be finite numbers")
        if kind == "Texture":
            if not object_path(row["Texture"]) or row["TextureRole"] not in ("Color", "Mask"):
                _fail("Parameters", row, "texture requires a full asset path and Color/Mask role")
            if row['SourceFile'] and Path(row['SourceFile']).suffix.lower() not in {'.png', '.jpg', '.jpeg', '.tga', '.bmp', '.tif', '.tiff', '.exr', '.hdr', '.webp'}:
                _fail('Parameters', row, 'SourceFile must be a raster image, never a UE binary package')
            previous_role = texture_roles.setdefault(row['Texture'], row['TextureRole'])
            if previous_role != row['TextureRole']:
                _fail('Parameters', row, 'one texture asset cannot have conflicting Color/Mask import roles')
        elif row["Texture"] or row["SourceFile"] or row["TextureRole"]:
            _fail("Parameters", row, "nontexture parameter contains texture fields")
        if kind != "Vector" and any(row[k] != 0 for k in ("VectorR", "VectorG", "VectorB", "VectorA")):
            _fail("Parameters", row, "nonvector parameter contains vector values")
        if kind != "Scalar" and row["ScalarValue"] != 0:
            _fail("Parameters", row, "nonscalar parameter contains ScalarValue")
        by_parameters[ident][key] = row
    by_layers = {ident: {} for ident in artworks}
    for row in layers.values():
        ident, index = row["ArtworkId"], row["LayerIndex"]
        if ident not in artworks:
            _fail("Layers", row, "unknown ArtworkId")
        if not isinstance(index, int) or isinstance(index, bool) or index not in range(1, 7) or index in by_layers[ident]:
            _fail("Layers", row, "duplicate or invalid LayerIndex (expected 1..6)")
        keys = ("Depth", "Opacity", "ScaleU", "ScaleV", "ScaleB", "ScaleA", "OffsetU", "OffsetV", "OffsetB", "OffsetA")
        if not all(isinstance(row[k], (int, float)) and not isinstance(row[k], bool) and math.isfinite(row[k]) for k in keys):
            _fail("Layers", row, "layer values must be finite")
        if row["ScaleU"] <= 0 or row["ScaleV"] <= 0 or not 0 <= row["Opacity"] <= 1:
            _fail("Layers", row, "scale must be positive and opacity in [0,1]")
        parameter = by_parameters[ident].get(row["TextureParameter"])
        if not parameter or parameter["Kind"] != "Texture":
            _fail("Layers", row, "TextureParameter must reference a texture parameter of this artwork")
        expected = "BaseColor Map" if templates[artworks[ident]["TemplateId"]]["Layout"] == "Atlas3x2" else f"Layer {index} Map"
        if row["TextureParameter"] != expected:
            _fail("Layers", row, f"template requires TextureParameter={expected}")
        by_layers[ident][index] = row
    for ident, artwork in artworks.items():
        if set(by_layers[ident]) != set(range(1, 7)):
            _fail("Artworks", artwork, "exactly six logical layers are required, including disabled layers")
        params = by_parameters[ident]
        for key in ("Layers global depth", "Ability behind machinery", "Foreground behind machinery"):
            if key not in params or params[key]["Kind"] != "Scalar":
                _fail("Artworks", artwork, f"missing scalar {key}")
        preset = templates[artwork["TemplateId"]]["CompositePreset"]
        if preset == "RegisteredSixForegroundMask":
            mask = params.get("Foreground Front Mask")
            if not mask or mask["Kind"] != "Texture" or mask["TextureRole"] != "Mask":
                _fail("Artworks", artwork, "foreground template requires Foreground Front Mask")
        if preset != "ModelComicAtlasV9" and "Background Margin Map" not in params:
            _fail("Artworks", artwork, "registered template requires Background Margin Map")
    return {"templates": templates, "artworks": artworks, "layers": by_layers, "parameters": by_parameters}


def resolve_artwork_references(tables):
    """Only Cards.ArtworkId becomes the existing serialized FrontMaterial field."""
    catalog = validate_catalog(tables)
    entry = tables.get("DT_GuLiStrikeRogueCards_Cards")
    if entry:
        for row in entry["rows"]:
            ident = row.get("ArtworkId")
            if not isinstance(ident, int) or isinstance(ident, bool) or ident not in catalog["artworks"]:
                raise CatalogError(f"GuLiStrikeRogueCards.xlsx / Cards {row['Id']}: unknown ArtworkId={ident}")
            # Rebuild in place to retain the old field order as well as its identity.
            values = {("FrontMaterial" if key == "ArtworkId" else key):
                      (catalog["artworks"][ident]["Material"] if key == "ArtworkId" else value)
                      for key, value in row.items()}
            row.clear()
            row.update(values)
    return catalog
