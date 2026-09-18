"""Merge every secondary-unit active skill from exported Excel data into the runtime catalog.

Source: data/Excel/GuLiStrikeSecondaryUnitSkills.xlsx. No skill numbers live in this script.
Global tactics and definitions outside this source table are preserved.
"""
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'outputs/wm01_q'
CATALOG = '/Game/GuLiStrike/Commander/Skills/DA_CommanderSkills_V1'
TABLES = ['DT_GuLiStrikeSecondaryUnitSkills_Skills', 'DT_GuLiStrikeSecondaryUnitSkills_UnitSkills']

def author():
    rows = json.loads((ROOT/'data/Json'/f'{TABLES[0]}.json').read_text(encoding='utf8'))
    units = json.loads((ROOT/'data/Json'/f'{TABLES[1]}.json').read_text(encoding='utf8'))
    catalog = unreal.load_asset(CATALOG)
    assert isinstance(catalog, unreal.GuLiCommanderSkillCatalog)
    target_modes = {'Self':unreal.GuLiActiveSkillTargetMode.SELF, 'GroundPoint':unreal.GuLiActiveSkillTargetMode.GROUND_POINT}
    definitions, changed_configs = [], []
    for row in rows:
        executor = unreal.load_class(None, row['ExecutorClass'])
        assert executor, row['Name']+': executor class missing'
        config = None
        path = row['Configuration']
        if path:
            config = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
            if not config:
                cls = unreal.load_class(None, row['ConfigurationClass'])
                assert cls, row['Name']+': configuration class missing'
                factory = unreal.DataAssetFactory()
                factory.set_editor_property('data_asset_class', cls)
                folder, name = path.rsplit('/', 1)
                config = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, cls, factory)
            assert config
            if isinstance(config, unreal.GuLiPointSkillConfiguration):
                for field, key in [('projectile','Projectile'),('ground_warning_style','GroundWarningStyle')]:
                    asset = unreal.load_asset(row[key]) if row[key] else None
                    assert not row[key] or asset, row[key]
                    config.set_editor_property(field, asset)
                for field, key in [('field_config_id','FieldConfigId'),('source_weapon_slot','SourceWeaponSlot'),('use_authored_trajectory','UseAuthoredTrajectory'),('target_area_diameter_centimeters','TargetAreaDiameterCentimeters')]:
                    config.set_editor_property(field, row[key])
            changed_configs.append(config)
        definition = unreal.GuLiActiveSkillDefinition()
        for key, value in dict(skill_id=row['Name'], scope=unreal.GuLiActiveSkillScope.UNIT,
            target_mode=target_modes[row['TargetMode']], cooldown_seconds=row['CooldownSeconds'],
            range_centimeters=row['RangeCentimeters'], range_source_slot=row['RangeSourceSlot'], range_multiplier=row['RangeMultiplier'],
            maximum_level=row['MaximumLevel'], executor_class=executor, configuration=config).items():
            definition.set_editor_property(key, value)
        definitions.append(definition)
    ids = {row['Name'] for row in rows}
    preserved = [s for s in catalog.skills if str(s.skill_id) not in ids]
    mapping = dict(catalog.unit_skills)
    seen_units = set()
    for row in units:
        unit = row['UnitTypeId']
        assert unit not in seen_units and 0 < unit <= 65535, row
        seen_units.add(unit)
        if row['SkillId']:
            assert row['SkillId'] in ids, row
            mapping[unit] = row['SkillId']
        else:
            mapping.pop(unit, None)
    catalog.set_editor_property('skills', preserved+definitions)
    catalog.set_editor_property('unit_skills', mapping)
    issues = list(unreal.GuLiSkillAuthoringLibrary.validate_commander_catalog(catalog))
    assert not issues, str(issues)
    for asset in changed_configs+[catalog]:
        assert unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
    namespace = {'__name__':'secondary_unit_skill_import'}
    source = (ROOT/'Scripts/import_data_to_engine.py').read_text(encoding='utf8')
    source = source.split('report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}',1)[0]
    exec(compile(source,'import_data_to_engine.py','exec'), namespace)
    namespace['PROGRESS'] = str(OUT/'table-import.log')
    manifest = json.loads((ROOT/'data/Json/manifest.json').read_text(encoding='utf8'))
    imported = [namespace['import_table'](t,manifest['tables'][t]) for t in TABLES]
    assert all(t.get('imported') for t in imported), imported
    report = {'source':'data/Excel/GuLiStrikeSecondaryUnitSkills.xlsx', 'catalog':CATALOG,
        'skills':[str(s.skill_id) for s in catalog.skills], 'unit_skills':{str(k):str(v) for k,v in catalog.unit_skills.items()},
        'source_rows':rows, 'tables':imported, 'issues':list(map(str,issues))}
    OUT.mkdir(parents=True, exist_ok=True)
    (OUT/'secondary-unit-skill-authoring.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps(report,ensure_ascii=True))

author()
