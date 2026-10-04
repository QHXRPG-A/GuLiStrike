"""Run after the approved native Editor build/reload. Uses the existing table importer."""
import json, math
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'ArtSource/Mechs/RSGMechStyle_20261003/Delivery_UE_v1/Reports'
META=json.loads((OUT.parent/'vat_metadata.json').read_text(encoding='utf8'))
PRESENTATION=json.loads((OUT.parent/'runtime_presentation_settings.json').read_text(encoding='utf8'))
LOCOMOTION_CLIPS=set(PRESENTATION['locomotion_clips'])
LOCOMOTION_RATE=float(PRESENTATION['locomotion_rate_multiplier'])
assert LOCOMOTION_CLIPS=={'Forward','Backward','Left','Right'}
assert math.isfinite(LOCOMOTION_RATE) and 0<LOCOMOTION_RATE<=1
BASE='/Game/GuLiStrike/Robots/RSGMech'
LIB=unreal.EditorAssetLibrary
OWNER='GuLiStrike.Pioneer.B-v1.Delivery_UE_v1'

def save(asset):
    assert LIB.save_loaded_asset(asset,False),asset.get_path_name()

def box(value):return unreal.Box(unreal.Vector(*value['min']),unreal.Vector(*value['max']))

def install_vat():
    # Fail before writing anything when the live process still has the previous native module.
    cls=unreal.GuLiVATDefinition
    path=BASE+'/VAT/DA_Pioneer_VAT'
    asset=unreal.load_asset(path) if LIB.does_asset_exist(path) else None
    assert not asset or LIB.get_metadata_tag(asset,'GuLi.Owner')==OWNER,path
    if not asset:
        factory=unreal.DataAssetFactory();factory.set_editor_property('data_asset_class',cls)
        asset=unreal.AssetToolsHelpers.get_asset_tools().create_asset('DA_Pioneer_VAT',BASE+'/VAT',cls,factory)
        LIB.set_metadata_tag(asset,'GuLi.Owner',OWNER)
    assert isinstance(asset,cls)
    transforms=[]
    for frame in META['frames']:
        for bone in frame:
            t=unreal.Transform()
            t.set_editor_property('translation',unreal.Vector(*bone['translation']))
            t.set_editor_property('rotation',unreal.Quat(*bone['rotation']))
            transforms.append(t)
    bones=[];clips=[];muzzles=[]
    for b in META['bones']:
        s=unreal.GuLiVATBone()
        for key,value in dict(name=b['name'],parent_index=b['parent_index'],upper_aim=b['upper_aim'],gun_pitch=b['gun_pitch']).items():s.set_editor_property(key,value)
        bones.append(s)
    for c in META['clips']:
        s=unreal.GuLiVATClip()
        # Runtime stride is presentation tuning; preserve the measured source metadata.
        stride=c['stride_cm']/LOCOMOTION_RATE if c['name'] in LOCOMOTION_CLIPS else c['stride_cm']
        for key,value in dict(name=c['name'],first_frame=c['first_frame'],frame_count=c['frame_count'],duration_seconds=c['duration_seconds'],
            stride_centimeters=stride,loop=c['loop']).items():s.set_editor_property(key,value)
        clips.append(s)
    for m in META['muzzles']:
        s=unreal.GuLiVATMuzzle()
        for key,value in dict(bone_index=m['bone_index'],reference_position=unreal.Vector(*m['reference_position_cm']),
            reference_direction=unreal.Vector(*m['reference_direction']),pitch_pivot=unreal.Vector(*m['pitch_pivot_cm'])).items():s.set_editor_property(key,value)
        muzzles.append(s)
    props=dict(bone_position=unreal.load_asset(BASE+'/Textures/T_Pioneer_BonePosition'),
        bone_rotation=unreal.load_asset(BASE+'/Textures/T_Pioneer_BoneRotation'),frames_per_second=30,
        bones=bones,clips=clips,bone_deltas=transforms,muzzles=muzzles,upper_pivot=unreal.Vector(*META['upper_pivot_cm']),
        gameplay_bounds=box(META['gameplay_bounds_cm']),runtime_render_bounds=box(META['runtime_render_bounds_cm']),
        hit_material=unreal.load_asset(BASE+'/VAT/M_Pioneer_Hit'),wreck_material=unreal.load_asset(BASE+'/VAT/M_Pioneer_Wreck'),
        phase_material=unreal.load_asset(BASE+'/VAT/M_Pioneer_Phase'))
    for key,value in props.items():assert value is not None,key;asset.set_editor_property(key,value)
    assert asset.is_valid_definition()
    errors=list(asset.call_method('ValidateImportedTextures'))
    assert not errors,errors
    LIB.set_metadata_tag(asset,'GuLi.Owner',OWNER);LIB.set_metadata_tag(asset,'GuLi.ApprovedVersion','B-v1')
    LIB.set_metadata_tag(asset,'GuLi.LocomotionRateMultiplier',str(LOCOMOTION_RATE))
    LIB.set_metadata_tag(asset,'GuLi.PresentationVersion',PRESENTATION['version'])
    LIB.set_metadata_tag(asset,'GuLi.SourceSHA256',META['source_sha256']);save(asset)
    return {'asset':path,'bones':len(asset.get_editor_property('bones')),
        'frames':len(asset.get_editor_property('bone_deltas'))//len(asset.get_editor_property('bones')),
        'clips':[str(c.get_editor_property('name')) for c in asset.get_editor_property('clips')],
        'texture_validation_errors':errors}

def install_tables():
    namespace={'__name__':'pioneer_table_import'}
    source=(ROOT/'Scripts/import_data_to_engine.py').read_text(encoding='utf8')
    exec(compile(source.split('report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}',1)[0],str(ROOT/'Scripts/import_data_to_engine.py'),'exec'),namespace)
    namespace['PROGRESS']=str(OUT/'runtime-table-import.log')
    manifest=json.loads((ROOT/'Data/Json/manifest.json').read_text(encoding='utf8'))
    names=['DT_GuLiStrikeCommander_Soldiers','DT_GuLiStrikeCommander_UnitSkills','DT_GuLiStrikeCommander_WeaponMounts','DT_GuLiStrikeGameTexts_Texts']
    result=[]
    for name in names:
        row=namespace['import_table'](name,manifest['tables'][name]);assert row.get('imported'),row;result.append(row)
    skill_namespace={'__name__':'pioneer_secondary_skill_import'}
    source=(ROOT/'Scripts/author_secondary_unit_skill_assets.py').read_text(encoding='utf8')
    exec(compile(source.rsplit('\nauthor()',1)[0],str(ROOT/'Scripts/author_secondary_unit_skill_assets.py'),'exec'),skill_namespace)
    skill_namespace['OUT']=OUT;skill_namespace['author']()
    return result

def install_portraits_and_effects():
    path=BASE+'/UI/T_UI_Pioneer'
    portrait=unreal.load_asset(path) if LIB.does_asset_exist(path) else None
    if not portrait:
        task=unreal.AssetImportTask()
        for key,value in dict(filename=str(ROOT/'ArtSource/Mechs/RSGMechStyle_20261003/Production_B_v1/ReviewImages/RSG_B_v1_Hero.png'),
            destination_path=BASE+'/UI',destination_name='T_UI_Pioneer',automated=True,save=False).items():task.set_editor_property(key,value)
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]);portrait=unreal.load_asset(path)
    assert portrait
    portrait.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_EDITOR_ICON)
    portrait.set_editor_property('lod_group',unreal.TextureGroup.TEXTUREGROUP_UI)
    portrait.set_editor_property('srgb',True)
    LIB.set_metadata_tag(portrait,'GuLi.Owner',OWNER);save(portrait)
    theme=unreal.load_asset('/Game/Commander/UI/DA_CommanderUITheme')
    portraits=dict(theme.get_editor_property('portraits'))
    if 5 not in portraits:portraits[5]=portraits[1]
    portraits[1]=portrait;theme.set_editor_property('portraits',portraits);save(theme)
    catalog=unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_CommanderCombatEffects')
    mounts=list(catalog.get_editor_property('mounts'))
    original=next(m for m in mounts if m.unit_type_id==1 and str(m.slot_id)=='BasicAttack')
    if not any(m.unit_type_id==5 and str(m.slot_id)=='BasicAttack' for m in mounts):
        copy=unreal.GuLiWeaponEffectMount()
        for key in ['slot_id','skill_id','muzzles','aim_offset','projectile','calibrated']:copy.set_editor_property(key,original.get_editor_property(key))
        copy.set_editor_property('unit_type_id',5);mounts.append(copy)
    original.set_editor_property('muzzles',[unreal.Vector(*m['reference_position_cm']) for m in META['muzzles']])
    aim=next(row for row in json.loads((ROOT/'Data/Json/DT_GuLiStrikeCommander_WeaponMounts.json').read_text(encoding='utf8')) if row['UnitTypeId']==1 and row['PointRole']=='AimTarget')
    original.set_editor_property('aim_offset',unreal.Vector(**{k.lower():v for k,v in aim['Offset'].items()}))
    original.set_editor_property('calibrated',True)
    catalog.set_editor_property('mounts',mounts);save(catalog)
    return {'portrait_asset':path,'portraits':{str(k):v.get_path_name() for k,v in portraits.items()},
        'mounts':[{'unit_type_id':m.unit_type_id,'slot':str(m.slot_id),'muzzles':[list(p.to_tuple()) for p in m.muzzles]} for m in mounts]}

result={'success':False}
try:
    assert not unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
    result['vat']=install_vat()
    result['tables']=install_tables()
    result['presentation']=install_portraits_and_effects()
    result['success']=True
finally:
    (OUT/'ue_runtime_deployment.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(result))
