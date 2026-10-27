"""Regression coverage for authored references, legacy compatibility and failure atomicity."""
import copy
import importlib.util
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = pathlib.Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "Tools/DataPipeline"))
sys.path.insert(0, str(ROOT / "Scripts/Cards"))
from card_artwork_catalog import CatalogError, TABLES, resolve_artwork_references, validate_catalog
from card_artwork_pipeline import parameter_values, load_catalog


class ArtworkCatalogTests(unittest.TestCase):
    def setUp(self):
        self.tables = {name: {"rows": json.loads((ROOT / "data/Json" / (name + ".json")).read_text(encoding="utf-8"))} for name in TABLES}

    def table(self, sheet):
        return self.tables["DT_GuLiStrikeCardArtwork_" + sheet]["rows"]

    def test_existing_six_and_three_templates(self):
        catalog = validate_catalog(self.tables)
        self.assertEqual(set(catalog["artworks"]), set(range(1, 7)))
        self.assertEqual(len(catalog["templates"]), 3)
        self.assertEqual(sum(map(len, catalog["layers"].values())), 36)

    def test_gameplay_paths_and_all_other_fields_are_preserved(self):
        baseline = json.loads((ROOT / "Artifacts/CardArtworkPipeline/baseline.json").read_text(encoding="utf-8"))
        rows = copy.deepcopy(baseline["gameplay_cards"])
        mapping = {a["material"]: a["id"] for a in baseline["artworks"]}
        for row in rows:
            row["ArtworkId"] = mapping[row.pop("FrontMaterial")]
        self.tables["DT_GuLiStrikeRogueCards_Cards"] = {"rows": rows}
        resolve_artwork_references(self.tables)
        self.assertEqual(rows, baseline["gameplay_cards"])
        # ArtworkId on the other native tables is not rewritten into FrontMaterial.
        self.assertEqual(self.table("Layers")[0]["ArtworkId"], 1)
        self.assertNotIn("FrontMaterial", self.table("Layers")[0])

    def test_unknown_artwork_reference(self):
        self.tables["DT_GuLiStrikeRogueCards_Cards"] = {"rows": [{"Id": "01.01", "ArtworkId": 999}]}
        with self.assertRaisesRegex(CatalogError, "unknown ArtworkId"):
            resolve_artwork_references(self.tables)

    def test_wrong_template(self):
        self.table("Artworks")[0]["TemplateId"] = 999
        with self.assertRaisesRegex(CatalogError, "TemplateId"):
            validate_catalog(self.tables)

    def test_duplicate_layer(self):
        self.table("Layers")[1]["LayerIndex"] = 1
        with self.assertRaisesRegex(CatalogError, "LayerIndex"):
            validate_catalog(self.tables)

    def test_missing_layer_including_disabled_layer(self):
        self.table("Layers").pop(0)
        with self.assertRaisesRegex(CatalogError, "six logical layers"):
            validate_catalog(self.tables)

    def test_missing_foreground_mask(self):
        rows = self.table("Parameters")
        rows[:] = [r for r in rows if not (r["ArtworkId"] == 6 and r["Key"] == "Foreground Front Mask")]
        with self.assertRaisesRegex(CatalogError, "Foreground Front Mask"):
            validate_catalog(self.tables)

    def test_wrong_parameter_type(self):
        self.table("Parameters")[0]["Kind"] = "Text"
        with self.assertRaisesRegex(CatalogError, "Kind"):
            validate_catalog(self.tables)

    def test_wrong_layer_binding_type(self):
        self.table("Layers")[0]["TextureParameter"] = "Layers global depth"
        with self.assertRaisesRegex(CatalogError, "TextureParameter"):
            validate_catalog(self.tables)

    def test_nonfinite_layer_and_invalid_opacity(self):
        for field, value in (("Depth", float("nan")), ("Opacity", 1.01), ("ScaleU", 0)):
            with self.subTest(field=field):
                tables = copy.deepcopy(self.tables)
                tables[TABLES[2]]["rows"][0][field] = value
                with self.assertRaises(CatalogError):
                    validate_catalog(tables)

    def test_material_parameters_reproduce_actual_ue_baseline(self):
        catalog = validate_catalog(self.tables)
        snapshot = json.loads((ROOT / "Artifacts/CardArtworkPipeline/baseline.json").read_text(encoding="utf-8"))
        for artwork in snapshot["artworks"]:
            expected = {key: ("Scalar", value) for key, value in artwork["scalars"].items()}
            expected.update({key: ("Vector", value) for key, value in artwork["vectors"].items()})
            expected.update({key: ("Texture", value["asset"]) for key, value in artwork["textures"].items()})
            self.assertEqual(parameter_values(catalog, artwork["id"]), expected)

    def test_reject_duplicate_parameter_authority(self):
        self.table("Parameters")[0]["Key"] = "Layer 1 Depth"
        with self.assertRaisesRegex(CatalogError, "also maintained in Layers"):
            validate_catalog(self.tables)

    def test_reject_conflicting_import_roles_for_one_texture(self):
        rows = self.table('Parameters')
        color = next(r for r in rows if r['Kind'] == 'Texture' and r['TextureRole'] == 'Color')
        mask = next(r for r in rows if r['Key'] == 'Foreground Front Mask')
        mask['Texture'] = color['Texture']
        with self.assertRaisesRegex(CatalogError, 'conflicting Color/Mask'):
            validate_catalog(self.tables)

    def test_refuses_stale_export_before_touching_ue(self):
        with mock.patch('card_artwork_pipeline._sha', return_value='stale'):
            with self.assertRaisesRegex(AssertionError, 'Excel changed'):
                load_catalog()

    def test_never_reads_ue_binary_as_texture_source(self):
        row = next(r for r in self.table('Parameters') if r['Kind'] == 'Texture')
        row['SourceFile'] = 'Content/SomeAsset.uasset'
        with self.assertRaisesRegex(CatalogError, 'raster image'):
            validate_catalog(self.tables)

    def test_failed_export_leaves_all_outputs_untouched(self):
        with tempfile.TemporaryDirectory(prefix="guli-artwork-export-") as temp:
            temp = pathlib.Path(temp)
            excel = temp / "Excel"
            excel.mkdir()
            for source in (ROOT / "data/Excel").glob("*.xlsx"):
                shutil.copy2(source, excel / source.name)
            subprocess.run(["D:/UE5.7/excelize-cli/bin/xlsx.exe", "write", str(excel / "GuLiStrikeCardArtwork.xlsx"),
                            "--sheet", "Artworks", "--cell", "E4", "--value", "999", "--type", "int"], check=True, capture_output=True)
            # E is TemplateId in the authored schema. Invalid references must be
            # rejected before even unrelated tables, headers or manifest are written.
            outputs, headers = temp / "Json", temp / "Generated"
            outputs.mkdir()
            headers.mkdir()
            (outputs / "manifest.json").write_text("sentinel", encoding="utf-8")
            (headers / "sentinel.h").write_text("sentinel", encoding="utf-8")
            code = ("import sys,pathlib;sys.path.insert(0," + repr(str(ROOT / "Tools/DataPipeline")) + ");"
                    "import export_data_from_excel as e;"
                    "e.EXCEL_DIR=pathlib.Path(" + repr(str(excel)) + ");"
                    "e.JSON_DIR=pathlib.Path(" + repr(str(outputs)) + ");"
                    "e.GEN_HEADER_DIR=pathlib.Path(" + repr(str(headers)) + ");e.main()")
            result = subprocess.run([sys.executable, "-X", "utf8", "-c", code], capture_output=True, encoding="utf-8")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("TemplateId", result.stderr)
            self.assertEqual(list(outputs.iterdir()), [outputs / "manifest.json"])
            self.assertEqual((outputs / "manifest.json").read_text(), "sentinel")
            self.assertEqual(list(headers.iterdir()), [headers / "sentinel.h"])


if __name__ == "__main__":
    unittest.main()
