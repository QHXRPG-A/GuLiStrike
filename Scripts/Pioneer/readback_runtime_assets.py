"""Reload saved delivery packages and inspect asset data; never starts gameplay."""
import json,math
from pathlib import Path
import unreal
import sys

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts'))
from Models import model_catalog
OUT=ROOT/'ArtSource/Mechs/RSGMechStyle_20261003/Delivery_UE_v1/Reports'
BASE='/Game/GuLiStrike/Robots/RSGMech'
EDITOR=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
assert not EDITOR.get_game_world()
assert json.loads((OUT/'ue_runtime_deployment.json').read_text(encoding='utf8'))['success']
names=['DT_GuLiStrikeCommander_Soldiers','DT_GuLiStrikeCommander_UnitSkills',
       'DT_GuLiStrikeCommander_WeaponMounts','DT_GuLiStrikeGameTexts_Texts',
       'DT_GuLiStrikeSecondaryUnitSkills_Skills','DT_GuLiStrikeSecondaryUnitSkills_UnitSkills']
paths=[BASE+'/VAT/DA_Pioneer_VAT',BASE+'/UI/T_UI_Pioneer',
       '/Game/GuLiStrike/Commander/Skills/DA_Pioneer_Summon',
       '/Game/GuLiStrike/Commander/Skills/DA_CommanderSkills_V1',
       '/Game/Commander/UI/DA_CommanderUITheme','/Game/GuLiStrike/FX/CommanderWeapons/DA_CommanderCombatEffects']
paths+=['/Game/GuLiStrike/Data/'+name for name in names]
dirty={p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
assert not (dirty&set(paths)),sorted(dirty&set(paths))
packages=[]
for path in paths:
    asset=unreal.load_asset(path);assert asset,path
    packages.append(asset.get_outer())
reloaded,error=unreal.EditorLoadingAndSavingUtils.reload_packages(packages,unreal.ReloadPackagesInteractionMode.ASSUME_NEGATIVE)
assert reloaded and not str(error),(reloaded,str(error))

def prop(value,key):return value.get_editor_property(key)
def table(name):
    asset=unreal.load_asset('/Game/GuLiStrike/Data/'+name)
    return {r['Name']:r for r in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(asset))}
def close(actual,expected):assert math.isclose(float(actual),float(expected),abs_tol=.001),(actual,expected)
def soft_path(value):
    value=str(value)
    if "'" in value:value=value.split("'",1)[1].rstrip("'")
    return value

vat=unreal.load_asset(BASE+'/VAT/DA_Pioneer_VAT')
assert vat.is_valid_definition()
errors=list(vat.call_method('ValidateImportedTextures'));assert not errors,errors
bones=prop(vat,'bones');clips=prop(vat,'clips');muzzles=prop(vat,'muzzles');bounds=prop(vat,'gameplay_bounds')
assert len(bones)==44 and len(clips)==7 and len(prop(vat,'bone_deltas'))==44*311
assert [str(prop(c,'name')) for c in clips]==['Idle','Forward','Backward','Left','Right','Death','Landing']
presentation=json.loads((OUT.parent/'runtime_presentation_settings.json').read_text(encoding='utf8'))
source_clips={c['name']:c for c in json.loads((OUT.parent/'vat_metadata.json').read_text(encoding='utf8'))['clips']}
locomotion_rate=float(presentation['locomotion_rate_multiplier'])
clip_strides={str(prop(c,'name')):float(prop(c,'stride_centimeters')) for c in clips}
for name in presentation['locomotion_clips']:
    close(clip_strides[name],source_clips[name]['stride_cm']/locomotion_rate)
close(bounds.max.y-bounds.min.y,625)
assert [prop(m,'bone_index') for m in muzzles]==[24,22]
soldiers=table(names[0]);weapons=table(names[1]);mount_rows=table(names[2]);texts=table(names[3])
pioneer=soldiers['DefaultSoldier'];sweeper=soldiers['SweeperSummon'];gun=weapons['SoldierA_Strafe']
assert pioneer['Id']==1 and pioneer['DisplayName']=='先驱号' and pioneer['bSummonOnly'] is False
for key,value in {'MovementSpeedCmPerSecond':1440,'ModelWidthMeters':6.25,'PresentationScale':1,'MaxHealth':100,'Defense':0}.items():close(pioneer[key],value)
assert model_catalog.resource(pioneer['ModelId'])==BASE+'/Meshes/SM_Pioneer_VAT.SM_Pioneer_VAT'
assert model_catalog.definition(pioneer['ModelId'])['VATDefinition']==vat.get_path_name()
assert sweeper['Id']==5 and sweeper['bSummonOnly'] is True and sweeper['DisplayName']=='扫荡者'
for key,value in {'MovementSpeedCmPerSecond':720,'PresentationScale':.2,'MaxHealth':100,'Defense':0}.items():close(sweeper[key],value)
assert 'SM_Sweeper_Rigid' in sweeper['ModelAsset']
for key,value in {'ProjectileCount':2,'RangeCentimeters':6000,'AttackRatePerSecond':1,'Damage':10,'ProjectileSpreadAngleDegrees':2}.items():close(gun[key],value)
for key,value in {'ProjectileCount':1,'RangeCentimeters':2000,'AttackRatePerSecond':1,'Damage':10,'ProjectileSpreadAngleDegrees':2}.items():close(weapons['SweeperSummon_Strafe'][key],value)
assert all(row['ProjectileSpreadAngleDegrees']!=10 for row in weapons.values())
assert len([r for r in mount_rows.values() if r['UnitTypeId']==1 and r['PointRole']=='Muzzle'])==2
assert '召唤5个扫荡者' in texts['UI.CommanderSkills.PioneerSummon']['Content']

config=unreal.load_asset('/Game/GuLiStrike/Commander/Skills/DA_Pioneer_Summon')
for key,value in {'unit_type_id':5,'count':5,'clearance_centimeters':50,'outer_rings':3,'candidates_per_ring':36}.items():close(prop(config,key),value)
catalog=unreal.load_asset('/Game/GuLiStrike/Commander/Skills/DA_CommanderSkills_V1')
issues=list(unreal.GuLiSkillAuthoringLibrary.validate_commander_catalog(catalog));assert not issues,issues
mapping=dict(prop(catalog,'unit_skills'));assert str(mapping[1])=='Pioneer_Summon' and 5 not in mapping
q=next(s for s in prop(catalog,'skills') if str(prop(s,'skill_id'))=='Pioneer_Summon')
assert prop(q,'scope')==unreal.GuLiActiveSkillScope.UNIT and prop(q,'target_mode')==unreal.GuLiActiveSkillTargetMode.SELF
close(prop(q,'cooldown_seconds'),30)
assert prop(q,'executor_class').get_name()=='GuLiSummonSkillExecutor' and prop(q,'configuration')==config
theme=unreal.load_asset('/Game/Commander/UI/DA_CommanderUITheme')
portraits=dict(prop(theme,'portraits'))
assert portraits[1].get_path_name()==BASE+'/UI/T_UI_Pioneer.T_UI_Pioneer'
assert 'T_UI_Sweeper' in portraits[5].get_path_name()
effects=unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_CommanderCombatEffects')
effect_mount=next(m for m in prop(effects,'mounts') if prop(m,'unit_type_id')==1 and str(prop(m,'slot_id'))=='BasicAttack')
assert len(prop(effect_mount,'muzzles'))==2
for actual,expected in zip(prop(effect_mount,'muzzles'),muzzles):
    reference=prop(expected,'reference_position')
    assert all(abs(getattr(actual,axis)-getattr(reference,axis))<.001 for axis in ['x','y','z'])
assert len([m for m in prop(effects,'mounts') if prop(m,'unit_type_id')==5 and str(prop(m,'slot_id'))=='BasicAttack'])==1
assert not ({p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}&set(paths))
result={'success':True,'packages_reloaded':paths,'native_vat_valid':True,'texture_source_errors':errors,
    'bones':len(bones),'frames':311,'clips':[str(prop(c,'name')) for c in clips],
    'locomotion_rate_multiplier':locomotion_rate,'clip_stride_centimeters':clip_strides,
    'gameplay_width_cm':bounds.max.y-bounds.min.y,'pioneer':pioneer,'sweeper':sweeper,
    'pioneer_gun':gun,'unit_skill_mapping':{str(k):str(v) for k,v in mapping.items()},
    'summon_config':{k:prop(config,k) for k in ['unit_type_id','count','clearance_centimeters','outer_rings','candidates_per_ring']},
    'portraits':{str(k):v.get_path_name() for k,v in portraits.items()},'play_started':False}
(OUT/'ue_runtime_readback.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'packages_reloaded':len(paths),'bones':44,'frames':311,
    'texture_source_errors':errors,'pioneer_id':1,'summon_id':5,'q_count':5,'q_cooldown_seconds':30,'play_started':False}))
