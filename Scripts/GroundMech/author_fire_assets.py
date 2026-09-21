"""Author the isolated, playable ground-mech fire review candidate in the source editor.

Preserves production weapon settings and game mode. The animation authoring pass
binds the production weapon as well as this review copy.
Run after the Excel export and source Editor build, via Scripts/ue_exec.py.
"""
import unreal, json, traceback
from pathlib import Path

ROOT = Path(unreal.Paths.project_dir())
OUT = ROOT / 'TestResults/GroundMech/Fire'
OUT.mkdir(parents=True, exist_ok=True)
BASE = '/Game/GuLiStrike/GroundMech'
FX = '/Game/GuLiStrike/FX/GroundMech'
LIB = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
REPORT = {'success': False, 'assets': [], 'tables': []}

def save(asset):
    assert asset.get_path_name().startswith(('/Game/GuLiStrike/', '/Game/Maps/LVL_GroundMech_FireReview'))
    assert LIB.save_loaded_asset(asset, False)
    REPORT['assets'].append(asset.get_path_name())

def create(path, cls, factory):
    if LIB.does_asset_exist(path):
        return unreal.load_asset(path)
    folder, name = path.rsplit('/', 1)
    return TOOLS.create_asset(name, folder, cls, factory)

def duplicate(source, dest):
    return unreal.load_asset(dest) if LIB.does_asset_exist(dest) else LIB.duplicate_asset(source, dest)

def run():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert not level.is_in_play_in_editor()
    for source, name in [('VFX_Smg_Loop','Bullet'), ('VFX_FireGun_Loop','Muzzle')]:
        asset = duplicate('/Game/Assets/VFX/WeaponBulletVFX/NS/' + source, FX + '/NS_GroundMech_' + name)
        assert asset
        # Niagara refinement is performed by author_fire_niagara.py before review.
        save(asset)
    unreal.SystemLibrary.execute_console_command(None, 'gs.MechFire.QA.AuthorCurve')
    curve = unreal.load_asset(BASE + '/Animations/CF_Machinegun_Recoil')
    assert curve and abs(curve.get_float_value(.025)-1) < .00001
    REPORT['curve'] = [{'time': t, 'value': curve.get_float_value(t)} for t in [0,.025,.055,.1,.15]]
    gun = unreal.load_asset(BASE + '/Style_v9/Meshes/SK_Machinegun')
    sockets = unreal.SkeletonService.list_sockets(gun.get_path_name())
    if not any(str(s.socket_name) == 'Muzzle' for s in sockets):
        assert unreal.SkeletonService.add_socket(gun.get_path_name(), 'Muzzle', 'Barrel_end', unreal.Vector(), unreal.Rotator(pitch=90), unreal.Vector(1,1,1), False)
    save(gun)
    REPORT['socket'] = str(unreal.SkeletonService.get_socket_info(gun.get_path_name(), 'Muzzle'))
    factory = unreal.AnimBlueprintFactory()
    factory.set_editor_property('target_skeleton', gun.skeleton)
    factory.set_editor_property('parent_class', unreal.GuLiGroundMechWeaponAnimInstance)
    abp = create(BASE + '/Animations/ABP_GroundMech_Machinegun', unreal.AnimBlueprint, factory)
    unreal.BlueprintEditorLibrary.compile_blueprint(abp)
    defaults = unreal.get_default_object(abp.generated_class())
    defaults.set_editor_property('recoil_curve', curve)
    defaults.set_editor_property('recoil_bone', 'Barrel_big')
    defaults.set_editor_property('recoil_target_z', 152.)
    defaults.set_editor_property('recoil_duration', .15)
    save(abp)
    namespace = {'__name__': 'mech_fire_import'}
    source = (ROOT / 'Scripts/import_data_to_engine.py').read_text(encoding='utf-8')
    exec(compile(source.split('report = {"tables": [], "config_wired": [], "unwired": [], "errors": []}',1)[0], 'import_data_to_engine.py', 'exec'), namespace)
    namespace['PROGRESS'] = str(OUT / 'data-import.log')
    original_sidecar = namespace['write_csv_sidecar']
    def utf8_sidecar(rows, path):
        original_sidecar(rows, path)
        file = Path(path)
        text = file.read_text(encoding='utf-8')
        file.write_text(text, encoding='utf-8-sig')
    namespace['write_csv_sidecar'] = utf8_sidecar
    manifest = json.loads((ROOT / 'Data/Json/manifest.json').read_text(encoding='utf-8'))
    for suffix in ('Upgrades', 'Skills'):
        name = 'DT_GuLiStrikeMech_' + suffix
        result = namespace['import_table'](name, manifest['tables'][name])
        REPORT['tables'].append(result)
        assert result.get('imported'), result
    bp = duplicate(BASE + '/BP_GroundMech_Light', BASE + '/Review/BP_GroundMech_FireReview')
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    bp.modify()
    defaults = unreal.get_default_object(bp.generated_class())
    weapon = defaults.get_editor_property('weapon')
    weapon.modify()
    weapon.set_editor_property('weapon_enabled', True)
    weapon.set_editor_property('upgrade_table', unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Upgrades'))
    weapon.set_editor_property('skill_table', unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Skills'))
    defaults.get_editor_property('machinegun').set_anim_instance_class(abp.generated_class())
    save(bp)
    gm = duplicate(BASE + '/BP_GroundMech_DemoMode', BASE + '/Review/BP_GroundMech_FireReviewMode')
    unreal.BlueprintEditorLibrary.compile_blueprint(gm)
    gm.modify()
    mode = unreal.get_default_object(gm.generated_class())
    roles = mode.get_editor_property('role_pawn_classes')
    roles[unreal.GuLiCommanderRole.GROUND] = bp.generated_class()
    mode.set_editor_property('role_pawn_classes', roles)
    save(gm)
    target = '/Game/Maps/LVL_GroundMech_FireReview'
    if not LIB.does_asset_exist(target):
        assert level.load_level('/Game/Maps/LVL_GroundMech_Demo')
        assert unreal.EditorLoadingAndSavingUtils.save_map(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world(), target)
    assert level.load_level(target)
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode', gm.generated_class())
    # The demo's point traces can put the large player capsule beside a rock.
    # Use full capsule clearance for the two isolated review starts.
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    starts = sorted([a for a in actors.get_all_level_actors() if isinstance(a,unreal.PlayerStart)], key=lambda a:a.get_actor_label())
    assert len(starts)==2
    REPORT['review_starts']=[]
    for start,(x,y) in zip(starts,[(17000,-6000),(18000,-4300)]):
        hit=unreal.SystemLibrary.capsule_trace_single_for_objects(world,unreal.Vector(x,y,8000),unreal.Vector(x,y,-4000),235,380,[unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1,unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY2],False,starts,unreal.DrawDebugTrace.NONE)
        values=hit.to_tuple()
        assert values[0] and not values[1] and values[6].z>.9
        start.set_actor_location(values[4]+unreal.Vector(0,0,40),False,False)
        REPORT['review_starts'].append(list(start.get_actor_location().to_tuple()))
    assert level.save_current_level()
    REPORT['map'] = target
    REPORT['production_weapon_enabled'] = unreal.get_default_object(LIB.load_blueprint_class(BASE + '/BP_GroundMech_Light')).get_editor_property('weapon').get_editor_property('weapon_enabled')
    REPORT['success'] = True

try:
    run()
except Exception:
    REPORT['error'] = traceback.format_exc()
(OUT / 'asset-authoring.json').write_text(json.dumps(REPORT, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(REPORT, ensure_ascii=True))
