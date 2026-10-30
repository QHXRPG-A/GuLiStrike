"""Excel-owned card artwork deployment, readback and shared review entry.

CLI: python Scripts/Cards/card_artwork_pipeline.py preflight|deploy|apply|verify|review
     [--ids 1 2 ...] [--reimport]
The CLI sends the same functions to the live editor through Scripts/ue_exec.py.
Direct editor execution defaults to the nonmutating preflight action.
"""
import argparse
import hashlib
import json
import math
import re
import subprocess
import sys
import traceback
import uuid
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Tools/DataPipeline"))
from card_artwork_catalog import TABLES, validate_catalog

DATA = "/Game/GuLiStrike/Data"
REVIEW_MAP = "/Game/GuLiStrike/CardSystem/RevealDemo/Maps/LVL_CardArtworkCatalogReview"
REVIEW_ACTOR = "CardArtwork_CatalogDirector"
SYSTEM = "/Game/GuLiStrike/CardSystem/RevealDemo"
OUT = ROOT / "Artifacts/CardArtworkPipeline"
IDENTITY = "GuLi.CardArtwork.Id"
EVENTS = []


def _ue():
    import unreal
    return unreal


def load_catalog():
    manifest = json.loads((ROOT / "data/Json/manifest.json").read_text(encoding="utf-8"))
    tables = {}
    for name in TABLES:
        assert manifest["tables"][name]["excel"] == "GuLiStrikeCardArtwork.xlsx", name
        assert manifest["tables"][name].get('source_sha256') == _sha(ROOT / 'data/Excel/GuLiStrikeCardArtwork.xlsx'), 'Artwork Excel changed; run the data exporter before deployment'
        path = ROOT / 'data/Json' / (name + '.json')
        assert manifest['tables'][name].get('data_sha256') == _sha(path), 'Generated artwork JSON changed; regenerate it from Excel'
        tables[name] = json.loads(path.read_text(encoding="utf-8"))
    return tables, validate_catalog(tables), manifest


def selected_ids(catalog, ids):
    ids = list(catalog["artworks"]) if ids is None else list(dict.fromkeys(ids))
    assert ids and all(ident in catalog["artworks"] for ident in ids), "Unknown or empty artwork selection"
    return ids


def parameter_values(catalog, ident):
    """The one mapping from authored layer fields to the existing shader API."""
    values = {key: (row["Kind"], row["Texture"] if row["Kind"] == "Texture" else
                    row["ScalarValue"] if row["Kind"] == "Scalar" else
                    [row[k] for k in ("VectorR", "VectorG", "VectorB", "VectorA")])
              for key, row in catalog["parameters"][ident].items()}
    for index, row in catalog["layers"][ident].items():
        prefix = f"Layer {index} "
        motion=row
        while motion.get('MotionOwnerLayer',0):
            motion=catalog['layers'][ident][motion['MotionOwnerLayer']]
        values[prefix + "Depth"] = ("Scalar", motion["Depth"])
        values[prefix + "Opacity"] = ("Scalar", row["Opacity"])
        values[prefix + "Scale"] = ("Vector", [row[k] for k in ("ScaleU", "ScaleV", "ScaleB", "ScaleA")])
        values[prefix + "Offset"] = ("Vector", [row[k] for k in ("OffsetU", "OffsetV", "OffsetB", "OffsetA")])
    return values


def _source(row):
    value = row["SourceFile"]
    return (ROOT / value).resolve() if value else None


def _sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def _asset_path(obj):
    return obj.get_path_name() if obj else ""


def _actual(mi, kind, key):
    edit = _ue().MaterialEditingLibrary
    if kind == "Texture":
        return _asset_path(edit.get_material_instance_texture_parameter_value(mi, key))
    if kind == "Scalar":
        return float(edit.get_material_instance_scalar_parameter_value(mi, key))
    value = edit.get_material_instance_vector_parameter_value(mi, key)
    return [value.r, value.g, value.b, value.a]


def _equal(a, b):
    if isinstance(b, (float, int)) and not isinstance(b, bool):
        return isinstance(a, (float, int)) and math.isclose(a, b, rel_tol=5e-6, abs_tol=1e-6)
    if isinstance(b, list):
        return isinstance(a, list) and len(a) == len(b) and all(_equal(x, y) for x, y in zip(a, b))
    return a == b


def _snake(name):
    name = re.sub(r"([A-Z]+)([A-Z][a-z])", r"\1_\2", name)
    return re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", name).lower()


def native_schema_issues(tables, manifest):
    unreal = _ue()
    issues = []
    for name, rows in tables.items():
        path = manifest["tables"][name]["struct"]
        cls = getattr(unreal, path.rsplit(".", 1)[-1], None)
        if not unreal.find_object(None, path) or cls is None:
            issues.append(name + ": native row struct not loaded; compile/reload before table deployment")
            continue
        value = cls()
        for key in dict.fromkeys(k for row in rows for k in row if k != "Name"):
            try:
                value.get_editor_property(_snake(key))
            except Exception:
                issues.append(name + ": loaded schema lacks " + key)
    return issues


def preflight(ids=None, require_native=False, mutation=False, reimport=False):
    unreal = _ue()
    lib, edit = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary
    assert Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve() == ROOT.resolve(), "Wrong UE project"
    tables, catalog, manifest = load_catalog()
    ids = selected_ids(catalog, ids)
    issues = native_schema_issues(tables, manifest)
    report = {"artwork_ids": ids, "native_schema_ready": not issues, "native_schema_issues": issues,
              "missing_sources": [], "adoptions": [], "source_hashes": {}}
    if require_native:
        assert not issues, "\n".join(issues)
    dirty = {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    if mutation:
        assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(), "Preserve active PIE; stop it before applying artwork"
    texture_sources = {}
    for ident in ids:
        row = catalog["artworks"][ident]
        template = catalog["templates"][row["TemplateId"]]
        parent = unreal.load_asset(template["ParentMaterial"])
        assert isinstance(parent, unreal.Material), "Missing material template: " + template["ParentMaterial"]
        mi = unreal.load_asset(row["Material"])
        if mi:
            assert isinstance(mi, unreal.MaterialInstanceConstant), row["Material"]
            owner = lib.get_metadata_tag(mi, IDENTITY)
            assert not owner or owner == str(ident), "Material belongs to another ArtworkId: " + row["Material"]
            if not owner:
                report["adoptions"].append(row["Material"])
        if mutation:
            assert row["Material"].split(".")[0] not in dirty, "Preserve unsaved material: " + row["Material"]
        names = {kind: set(map(str, getter(parent))) for kind, getter in
                 (("Scalar", edit.get_scalar_parameter_names), ("Vector", edit.get_vector_parameter_names),
                  ("Texture", edit.get_texture_parameter_names))}
        independent = all(f'Layer {index} Map' in names['Texture'] for index in range(1, 7))
        masked = 'Foreground Front Mask' in names['Texture']
        expected = {'ModelComicAtlasV9': (False, False), 'RegisteredSix': (True, False),
                    'RegisteredSixForegroundMask': (True, True), 'RegisteredSixRigid': (True, True)}[template['CompositePreset']]
        assert (independent, masked) == expected, 'Parent material does not implement the selected composite template'
        for key, (kind, _) in parameter_values(catalog, ident).items():
            assert key in names[kind], f"Artwork {ident}: {key} is not a {kind} parameter of its template"
        hashes = {}
        for key, param in catalog["parameters"][ident].items():
            if param["Kind"] != "Texture":
                continue
            texture = unreal.load_asset(param["Texture"])
            assert not texture or isinstance(texture, unreal.Texture), param["Texture"]
            path = _source(param)
            exists = bool(path and path.is_file())
            if exists:
                digest = _sha(path)
                hashes[param["Texture"]] = digest
                previous = texture_sources.get(param["Texture"])
                assert previous is None or previous == digest, "Conflicting source files for " + param["Texture"]
                texture_sources[param["Texture"]] = digest
            else:
                report["missing_sources"].append({"artwork_id": ident, "parameter": key, "texture": param["Texture"], "source": param["SourceFile"]})
            managed = param['Texture'].startswith(row['AssetRoot'].rstrip('/') + '/')
            if not texture or (reimport and managed):
                assert exists, "Source required for import/reimport: " + param["Texture"]
                assert managed, "Shared texture is read-only: " + param["Texture"]
            if mutation:
                assert param["Texture"].split(".")[0] not in dirty, "Preserve unsaved texture: " + param["Texture"]
        report["source_hashes"][ident] = hashes
    if mutation and require_native:
        for name in TABLES:
            assert DATA + "/" + name not in dirty, "Preserve unsaved catalog DataTable: " + name
    return report


def verify(ids=None):
    unreal = _ue()
    _, catalog, _ = load_catalog()
    result = []
    for ident in selected_ids(catalog, ids):
        row = catalog["artworks"][ident]
        mi = unreal.load_asset(row["Material"])
        assert mi, row["Material"]
        parent = catalog["templates"][row["TemplateId"]]["ParentMaterial"]
        assert _asset_path(mi.get_editor_property("parent")) == parent
        checked = 0
        for key, (kind, expected) in parameter_values(catalog, ident).items():
            actual = _actual(mi, kind, key)
            assert _equal(actual, expected), f"Artwork {ident}: {key}: actual={actual!r}; expected={expected!r}"
            checked += 1
        result.append({"artwork_id": ident, "material": row["Material"], "parent": parent, "parameters_checked": checked})
    return result


def _set_value(mi, key, kind, value):
    unreal = _ue()
    service = unreal.MaterialService
    if kind == "Texture":
        ok = service.set_instance_texture_parameter(mi.get_path_name(), key, value)
    elif kind == "Scalar":
        ok = service.set_instance_scalar_parameter(mi.get_path_name(), key, value)
    else:
        ok = service.set_instance_vector_parameter(mi.get_path_name(), key, *value)
    assert ok and _equal(_actual(mi, kind, key), value), key


def apply_artworks(ids=None, reimport=False):
    unreal = _ue()
    lib, edit = unreal.EditorAssetLibrary, unreal.MaterialEditingLibrary
    report = preflight(ids, mutation=True, reimport=reimport)
    _, catalog, _ = load_catalog()
    imported, changed = set(), []
    for ident in report["artwork_ids"]:
        row = catalog["artworks"][ident]
        mi = unreal.load_asset(row["Material"])
        old_hashes = json.loads(lib.get_metadata_tag(mi, "GuLi.CardArtwork.SourceHashes") or "{}") if mi else {}
        adopt = bool(mi and not lib.get_metadata_tag(mi, IDENTITY))
        modified = False
        for key, param in catalog["parameters"][ident].items():
            if param["Kind"] != "Texture":
                continue
            path = _source(param)
            tex_path = param["Texture"]
            new_texture = not lib.does_asset_exist(tex_path)
            managed = tex_path.startswith(row['AssetRoot'].rstrip('/') + '/')
            current_hash = report["source_hashes"][ident].get(tex_path)
            needs_import = new_texture or (managed and (reimport or (
                bool(current_hash and tex_path in old_hashes) and old_hashes[tex_path] != current_hash)))
            if needs_import and tex_path not in imported:
                assert path and path.is_file(), tex_path
                assert tex_path.startswith(row["AssetRoot"].rstrip("/") + "/"), "Do not reimport a shared texture"
                package = tex_path.split(".")[0]
                task = unreal.AssetImportTask()
                for name, value in {"filename": str(path), "destination_path": package.rsplit("/", 1)[0],
                                    "destination_name": package.rsplit("/", 1)[1], "automated": True,
                                    "replace_existing": True, "save": False}.items():
                    task.set_editor_property(name, value)
                unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
                texture = unreal.load_asset(tex_path)
                assert texture
                # Existing texture settings are retained on reimport. New textures
                # use the role recorded in Excel, including the binary mask sampler.
                if new_texture:
                    mask = param["TextureRole"] == "Mask"
                    texture.set_editor_property("srgb", not mask)
                    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS if mask else unreal.TextureCompressionSettings.TC_BC7)
                    texture.set_editor_property("address_x", unreal.TextureAddress.TA_CLAMP)
                    texture.set_editor_property("address_y", unreal.TextureAddress.TA_CLAMP)
                    if mask:
                        texture.set_editor_property("filter", unreal.TextureFilter.TF_NEAREST)
                assert lib.save_loaded_asset(texture, False)
                EVENTS.append({'kind': 'texture', 'asset': tex_path, 'saved': True})
                imported.add(tex_path)
                modified = True
        parent = unreal.load_asset(catalog["templates"][row["TemplateId"]]["ParentMaterial"])
        if not mi:
            package = row["Material"].split(".")[0]
            mi = unreal.AssetToolsHelpers.get_asset_tools().create_asset(package.rsplit("/", 1)[1], package.rsplit("/", 1)[0], unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
            assert mi, row["Material"]
            modified = True
        if mi.get_editor_property("parent") != parent:
            edit.set_material_instance_parent(mi, parent)
            modified = True
        for key, (kind, expected) in parameter_values(catalog, ident).items():
            if not _equal(_actual(mi, kind, key), expected):
                _set_value(mi, key, kind, expected)
                modified = True
        definitions = {"artwork": row, "template": catalog["templates"][row["TemplateId"]],
                       "layers": catalog["layers"][ident], "parameters": catalog["parameters"][ident]}
        digest = hashlib.sha256(json.dumps(definitions, sort_keys=True).encode()).hexdigest()
        tags = {IDENTITY: str(ident), "GuLi.CardArtwork.TemplateId": str(row["TemplateId"]),
                "GuLi.CardArtwork.DefinitionSHA256": digest,
                "GuLi.CardArtwork.SourceHashes": json.dumps({**old_hashes, **report["source_hashes"][ident]}, sort_keys=True),
                "GuLi.CardArtwork.Source": "GuLiStrikeCardArtwork.xlsx"}
        for key, value in tags.items():
            if lib.get_metadata_tag(mi, key) != value:
                lib.set_metadata_tag(mi, key, value)
                modified = True
        if modified:
            edit.update_material_instance(mi)
            assert lib.save_loaded_asset(mi, False)
            changed.append(row["Material"])
            EVENTS.append({'kind': 'material', 'asset': row['Material'], 'saved': True})
    report.update(changed_materials=changed, imported_textures=sorted(imported), readback=verify(report["artwork_ids"]))
    return report


def _table_matches(dt, source):
    actual = json.loads(_ue().DataTableFunctionLibrary.export_data_table_to_json_string(dt))
    actual = {row["Name"]: row for row in actual}
    if set(actual) != {row["Name"] for row in source}:
        return False
    for row in source:
        for key, expected in row.items():
            value = actual[row["Name"]].get(key)
            if isinstance(expected, str):
                # UE may decorate a soft object reference with its class name.
                if expected.startswith(("/Game/", "/Engine/")):
                    if not (value == expected or str(value).endswith("'" + expected + "'")):
                        return False
                elif ("" if value in (None, "None") else value) != expected:
                    return False
            elif not _equal(value, expected):
                return False
    return True


def import_tables():
    unreal = _ue()
    preflight(require_native=True, mutation=True)
    tables, _, manifest = load_catalog()
    result = []
    for name, rows in tables.items():
        path = DATA + "/" + name
        struct = unreal.find_object(None, manifest["tables"][name]["struct"])
        dt = unreal.load_asset(path)
        changed = False
        if not dt:
            factory = unreal.DataTableFactory()
            factory.set_editor_property("struct", struct)
            dt = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, DATA, unreal.DataTable, factory)
            assert dt, name
            changed = True
        assert isinstance(dt, unreal.DataTable) and dt.get_editor_property("row_struct") == struct, "Wrong row type: " + name
        if not _table_matches(dt, rows):
            assert unreal.DataTableFunctionLibrary.fill_data_table_from_json_string(dt, json.dumps(rows, ensure_ascii=False), struct), name
            changed = True
        assert _table_matches(dt, rows), "DataTable readback mismatch: " + name
        if changed:
            assert unreal.EditorAssetLibrary.save_loaded_asset(dt, False), name
            EVENTS.append({'kind': 'table', 'asset': path, 'saved': True})
        result.append({"table": path, "rows": len(rows), "changed": changed, "saved_readback": True})
    return result


def prepare_review(ident):
    unreal = _ue()
    preflight([ident], mutation=True)
    _, catalog, _ = load_catalog()
    lib = unreal.EditorAssetLibrary
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), "Preserve unsaved map work"
    if lib.does_asset_exist(REVIEW_MAP):
        assert level.load_level(REVIEW_MAP)
        world = editor.get_editor_world()
        assert lib.get_metadata_tag(world, "GuLi.CardArtwork.Review") == "1", "Unrelated review map"
        director = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == REVIEW_ACTOR)
        note = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == "CardArtwork_CatalogInstructions")
    else:
        world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        director = actors.spawn_actor_from_class(lib.load_blueprint_class(SYSTEM + "/Blueprints/BP_CardRevealDirector"), unreal.Vector())
        director.set_actor_label(REVIEW_ACTOR)
        note = actors.spawn_actor_from_class(unreal.Note, unreal.Vector(0, 0, -100))
        note.set_actor_label("CardArtwork_CatalogInstructions")
        start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 140, 0), unreal.Rotator(yaw=-90))
        start.set_actor_label("CardArtwork_PlayerStart")
        light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 250, 120), unreal.Rotator(pitch=-25, yaw=-70)).light_component
        light.set_mobility(unreal.ComponentMobility.MOVABLE)
        light.set_intensity(2.5)
        sky = actors.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 100)).light_component
        sky.set_mobility(unreal.ComponentMobility.MOVABLE)
        sky.set_editor_property("source_type", unreal.SkyLightSourceType.SLS_SPECIFIED_CUBEMAP)
        sky.set_editor_property("cubemap", unreal.load_asset("/Engine/MapTemplates/Sky/DaylightAmbientCubemap"))
        sky.set_intensity(0.7)
    row = catalog["artworks"][ident]
    artwork = unreal.load_asset(row["Material"])
    assert artwork
    # Shared frame comes from the existing project settings, not a second path table.
    settings_class = unreal.load_class(None, '/Script/GuLiStrike.GuLiRogueCardSettings')
    assert settings_class, 'Rogue card project settings are not loaded'
    settings = unreal.get_default_object(settings_class)
    frame_value = settings.get_editor_property('FrameMaterial')
    frame_path = str(frame_value)
    frame = frame_value if isinstance(frame_value, unreal.MaterialInterface) else unreal.load_asset(frame_path)
    assert frame, frame_path
    for key, value in {"CardFrontMaterials": [artwork] * 3, "CardTextMaterials": [frame] * 3,
                       "CardTextDataTable": None, "CardTextRowNames": [unreal.Name("None")] * 3,
                       "UseLiveCardData": False, "ExternalConfirmation": False, "ActiveCardCount": 1,
                       "SimpleCardFrame": True, "MaximumTilt": 12., "CardAreaMultiplier": 2., "CardThicknessMultiplier": 2.}.items():
        director.set_editor_property(key, value)
    gm = lib.load_blueprint_class(SYSTEM + "/Blueprints/BP_CardRevealGameMode")
    world.get_world_settings().set_editor_property("default_game_mode", gm)
    camera = director.get_component_by_class(unreal.CameraComponent)
    pp = camera.post_process_settings
    for key, value in {"override_auto_exposure_method": True, "auto_exposure_method": unreal.AutoExposureMethod.AEM_MANUAL,
                       "override_auto_exposure_apply_physical_camera_exposure": True, "auto_exposure_apply_physical_camera_exposure": False,
                       "override_auto_exposure_bias": True, "auto_exposure_bias": 0.,
                       "override_motion_blur_amount": True, "motion_blur_amount": 0.,
                       "override_bloom_intensity": True, "bloom_intensity": .25}.items():
        pp.set_editor_property(key, value)
    camera.set_editor_property("post_process_settings", pp)
    camera.set_editor_property("post_process_blend_weight", 1.)
    camera.set_editor_property("constrain_aspect_ratio", False)
    camera.set_editor_property("override_aspect_ratio_axis_constraint", True)
    camera.set_editor_property("aspect_ratio_axis_constraint", unreal.AspectRatioAxisConstraint.ASPECT_RATIO_MAINTAIN_XFOV)
    note.set_editor_property("text", f"ArtworkId={ident} {row['Title']}\n按ArtworkId从美术目录选择。PIE中移动鼠标查看视差；单击翻面，再次单击退场，点击重播。仅美术预览。")
    lib.set_metadata_tag(world, "GuLi.CardArtwork.Review", "1")
    lib.set_metadata_tag(world, IDENTITY, str(ident))
    editor.set_level_viewport_camera_info(unreal.Vector(0, 140, 0), unreal.Rotator(yaw=-90))
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, REVIEW_MAP)
    EVENTS.append({'kind': 'map', 'asset': REVIEW_MAP, 'saved': True})
    assert level.load_level(REVIEW_MAP)
    world = editor.get_editor_world()
    director = next(a for a in actors.get_all_level_actors() if a.get_actor_label() == REVIEW_ACTOR)
    assert [_asset_path(m) for m in director.get_editor_property("CardFrontMaterials")] == [row["Material"]] * 3
    assert director.get_editor_property("ActiveCardCount") == 1
    assert not director.get_editor_property("UseLiveCardData") and not director.get_editor_property("ExternalConfirmation")
    return {"map": REVIEW_MAP, "artwork_id": ident, "director": director.get_path_name(),
            "material": row["Material"], "saved_readback": True, "pie_started": False, "user_visual_review": "pending"}


def test_parameter_update():
    """Exercise the real apply path against an isolated material and a copied row."""
    import copy
    unreal = _ue()
    preflight([6], mutation=True)
    real_loader = load_catalog
    tables, catalog, manifest = real_loader()
    test_path = "/Game/GuLiStrike/Cards/_Tests/CardArtworkPipeline/MI_ArtworkPipeline_ParameterTest"
    lib = unreal.EditorAssetLibrary
    test_mi = unreal.load_asset(test_path)
    if test_mi:
        assert lib.get_metadata_tag(test_mi, 'GuLi.CardArtwork.TestFixture') == '1', 'Unrelated test asset'
    else:
        test_mi = lib.duplicate_asset(catalog['artworks'][6]['Material'], test_path)
        assert test_mi
        lib.set_metadata_tag(test_mi, 'GuLi.CardArtwork.TestFixture', '1')
        assert lib.save_loaded_asset(test_mi, False)
    key = 'Layer 1 Depth'
    original = parameter_values(catalog, 6)[key][1]
    _set_value(test_mi, key, 'Scalar', original)
    lib.set_metadata_tag(test_mi, 'GuLi.CardArtwork.TestFixture', '1')
    assert lib.save_loaded_asset(test_mi, False)
    before = _actual(test_mi, 'Scalar', key)
    fixture = copy.deepcopy(tables)
    for row in fixture['DT_GuLiStrikeCardArtwork_Artworks']:
        if row['Id'] == 6:
            row['Material'] = _asset_path(test_mi)
            row['AssetRoot'] = test_path.rsplit('/Materials/', 1)[0] if '/Materials/' in test_path else test_path.rsplit('/', 1)[0]
    for row in fixture['DT_GuLiStrikeCardArtwork_Layers']:
        if row['ArtworkId'] == 6 and row['LayerIndex'] == 1:
            row['Depth'] = original + .125
    test_catalog = validate_catalog(fixture)
    try:
        globals()['load_catalog'] = lambda: (fixture, test_catalog, manifest)
        result = apply_artworks([6])
        after = _actual(test_mi, 'Scalar', key)
        assert _equal(after, original + .125) and not _equal(before, after)
        second = apply_artworks([6])
        assert not second['changed_materials'] and not second['imported_textures']
    finally:
        globals()['load_catalog'] = real_loader
    verify([6])
    return {'material': _asset_path(test_mi), 'parameter': key, 'before': before, 'after': after,
            'source_workbook_modified': False, 'production_artwork_unchanged': True,
            'repeated_apply_changed_materials': second['changed_materials'], 'saved_readback': bool(result['readback'])}


def execute(action="preflight", ids=None, reimport=False, run_id=None):
    EVENTS.clear()
    report = {"success": False, "action": action, "run_id": run_id,
              "assistant_visual_test": False, "gameplay_modified": False}
    try:
        if action == "preflight":
            report["preflight"] = preflight(ids, reimport=reimport)
        elif action == "apply":
            report["application"] = apply_artworks(ids, reimport)
        elif action == "verify":
            report["readback"] = verify(ids)
        elif action == "tables":
            report["tables"] = import_tables()
        elif action == "deploy":
            preflight(ids, require_native=True, mutation=True, reimport=reimport)
            report["application"] = apply_artworks(ids, reimport)
            report["tables"] = import_tables()
        elif action == "review":
            assert ids and len(ids) == 1, "review requires one --ids value"
            report["review"] = prepare_review(ids[0])
        elif action == 'test':
            report['parameter_update_test'] = test_parameter_update()
        else:
            raise ValueError("Unknown action: " + action)
        report["success"] = True
    except Exception:
        report["error"] = traceback.format_exc()
    report['completed_changes'] = list(EVENTS)
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT / (action + "-report.json")).write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    _ue().MCPythonHelper.submit_result(json.dumps({"success": report["success"], "action": action,
                                                  "error": report.get("error"), "report": str(OUT / (action + "-report.json"))}, ensure_ascii=False))
    return report


def cli():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("preflight", "apply", "verify", "tables", "deploy", "review", "test"))
    parser.add_argument("--ids", type=int, nargs="+")
    parser.add_argument("--reimport", action="store_true")
    args = parser.parse_args()
    run_id = uuid.uuid4().hex
    code = "import sys\nsys.path.insert(0," + repr(str(Path(__file__).parent)) + ")\nimport card_artwork_pipeline as pipeline\nimport importlib\nimportlib.reload(pipeline)\npipeline.execute(" + repr(args.action) + "," + repr(args.ids) + "," + repr(args.reimport) + "," + repr(run_id) + ")"
    response = subprocess.run([sys.executable, "-X", "utf8", str(ROOT / "Scripts/ue_exec.py"), "-"], input=code,
                              text=True, capture_output=True, encoding="utf-8", cwd=ROOT)
    print(response.stdout)
    if response.returncode:
        print(response.stderr, file=sys.stderr)
        return response.returncode
    path = OUT / (args.action + "-report.json")
    result = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
    return 0 if result.get("run_id") == run_id and result.get("success") else 1


if __name__ == "__main__":
    try:
        import unreal
    except ImportError:
        raise SystemExit(cli())
    else:
        execute(globals().get("GULI_CARD_ART_ACTION", "preflight"), globals().get("GULI_CARD_ART_IDS"),
                globals().get("GULI_CARD_ART_REIMPORT", False))
