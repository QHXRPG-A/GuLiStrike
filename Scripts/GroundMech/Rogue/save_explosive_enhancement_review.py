"""Extend the existing Ground area; save/reload and certify the formal manifests."""
import ast
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT/'Artifacts/GroundExplosiveEnhancement_20261109'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
PREFIX = 'GroundAmmo_'


def require(value, message):
    if not value: raise RuntimeError(message)
    return value


def main():
    require(json.loads((OUT/'table_import.json').read_text(encoding='utf8'))['success'], 'Import final calibrated data first')
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    require(not level.is_in_play_in_editor(), 'End PIE')
    require(not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages() or globals().get('GULI_EXPLOSIVE_MAP_RETRY', False), 'Preserve unsaved map')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    require(world.get_path_name().split('.')[0] == MAP, 'Use original map')
    sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = {a.get_actor_label(): a for a in sub.get_all_level_actors()}
    before = {n: a.get_actor_transform().to_tuple() for n, a in actors.items() if not n.startswith(PREFIX+'Enhancement')}
    radius = json.loads((OUT/'radius-calibration.json').read_text(encoding='utf8'))['radius_cm']
    values = [radius, radius*1.02, radius*1.0404, radius*1.082016]
    added = []

    def actor(name, cls, position):
        label = PREFIX+'Enhancement'+name
        a = actors.get(label)
        if not a:
            a = require(sub.spawn_actor_from_class(cls, unreal.Vector(*position)), label)
            a.set_actor_label(label)
            actors[label] = a
        require(isinstance(a, cls), 'Preserve class '+label)
        a.modify()
        a.set_actor_location(unreal.Vector(*position), False, False)
        tags = list(a.get_editor_property('tags'))
        if unreal.Name('GuLi.Review.GroundAmmo') not in tags: tags.append(unreal.Name('GuLi.Review.GroundAmmo'))
        a.set_editor_property('tags', tags)
        added.append(label)
        return a

    for name, pos, team, shield in [
        ('Inside', (150,52650,905), unreal.GuLiTeam.BLUE, 0),
        ('Edge', (150,51450,905), unreal.GuLiTeam.BLUE, 0),
        ('Outside', (-1350,52950,905), unreal.GuLiTeam.BLUE, 0),
        ('Shield', (750,52050,905), unreal.GuLiTeam.BLUE, 10000),
        ('Friendly', (-450,52050,905), unreal.GuLiTeam.RED, 0)]:
        a = actor(name, unreal.GuLiCommanderDeploymentPoint, pos)
        for key, value in [('team',team),('unit_type_id',1),('rows',1),('columns',1),('spacing_centimeters',600.),
                           ('start_idle',True),('allow_automatic_fire',False),('use_in_population_manifest',True),
                           ('initial_max_health_override',100000.),('initial_shield',float(shield))]:
            a.set_editor_property(key,value)
    # Markers share one reference origin. Actual collision centers vary with gun side;
    # numerical boundary checks always use each recorded explosion center and hit body.
    origin = (-127.,52060.,1117.677)
    actor('RadiusOrigin', unreal.TargetPoint, origin)
    for n, r in enumerate(values):
        actor('Radius'+str(n), unreal.TargetPoint, (origin[0],origin[1]+r,origin[2]))
        note = actor('RadiusNote'+str(n), unreal.Note, (-2600+n*200,52400,1200))
        note.set_editor_property('text', f'强化爆炸弹{n}级：半径{r:.6f}cm；倍率{[1,1.02,1.0404,1.082016][n]}。Radius{n}相对RadiusOrigin标定；实际命中中心按每发爆炸记录，边界为与目标受击球相交。')
    note = actor('Instructions', unreal.Note, (-2800,52300,1200))
    note.set_editor_property('text', f'新局进入Ground，南侧PrimaryDamage靶开火。基础爆炸半径{radius}cm，保留原爆炸大小；11.01只决定5/4/2发阈值。无11.01不提供12.01；任意爆破后解锁，三级依次半径/伤害乘1.02、1.02、1.04，满级退出候选。未取10.01也可取得。升级12.01保留计数，旧弹保留旧伤害/半径/构筑版本。F4显示取得后等级及本次2/2/4%；HUD空图标槽保留等级。Inside/Edge/Outside/Shield/Friendly为真实Mass靶；沿用公共范围规则：爆炸点到逻辑Target.Location的距离不大于爆炸半径+CollisionRadius，勿用直接碰撞的GetHitCenter代替。原12m/15m/24m靶现在全部为远距离负对照。视觉贴合由玩家验收。')
    replacements = {
        'GroundAmmo_Instructions_TrailAndDamage': f'爆炸弹仍用Missile_Cluster载体、VFX_Missile飞行层和NS_Explosion_19命中副本；飞行参数、尺寸及尾迹不变。基础伤害20/30/40、半径{radius}cm；12.01累计半径/伤害倍率1.02/1.0404/1.082016，命中表现同比例放大。无衰减，仅一次范围伤害，按现有受击体/护盾/墙体规则。原Inside12m、Boundary15m、Outside24m现均在基础范围外。',
        'GroundAmmo_Instructions_Recovery': '升级12.01不清空爆破计数；升级11.01仍归零，保留已有12.01倍率。飞行中升级只影响之后发射的弹丸。重复确认和重复回执不能额外升级或扣血。沿用已有Loop、独立附弹与公共发射时序。新协议41/Ground7/Manifest27从新局生效。',
        'GroundAmmo_Instructions_Prerequisites': '新局候选09.01/10.01/11.01；12.01依赖任意11.01，不依赖10.01。06.01/07.01仍依赖10.01；08.01仍依赖07.01。新卡沿用空白牌面、HUD空图标槽及等级。'}
    for label, text in replacements.items():
        a = require(actors.get(label), label)
        a.modify(); a.set_editor_property('text', text)
    require(unreal.EditorLoadingAndSavingUtils.save_map(world, MAP), 'Save map')
    resource = unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    if not resource.success:
        new_deployments = [actors[n] for n in added if isinstance(actors[n], unreal.GuLiCommanderDeploymentPoint)]
        result = unreal.GuLiResourceAuthoringLibrary.revalidate_added_deployments(new_deployments, True)
        previous_file = OUT/'certified-deployment-positions.json'
        if not result.success and previous_file.exists():
            previous = json.loads(previous_file.read_text(encoding='utf8'))
            result = unreal.GuLiResourceAuthoringLibrary.revalidate_moved_deployments(
                new_deployments, [unreal.Vector(*previous[a.get_actor_label()]) for a in new_deployments], True)
        require(result.success, 'Certify only this task\'s added targets: '+str(result.message))
    tree = ast.parse((ROOT/'Scripts/GroundMech/Rogue/prepare_initial_bullet_scene.py').read_text(encoding='utf8'))
    fn = next(n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'prepare_scene_dependencies')
    exec(compile(ast.Module(body=[fn], type_ignores=[]), 'scene_dependencies', 'exec'), globals())
    dependencies = prepare_scene_dependencies(world, actors)
    require(level.load_level(MAP), 'Reload map')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    saved = {a.get_actor_label():a for a in sub.get_all_level_actors()}
    require(before == {n:saved[n].get_actor_transform().to_tuple() for n in before}, 'Preserve existing transforms')
    entities = []
    for label in added:
        a = require(saved.get(label), 'Saved '+label)
        row = {'label':label,'class':a.get_class().get_path_name(),'position':a.get_actor_location().to_tuple()}
        if isinstance(a,unreal.Note): row['text'] = str(a.get_editor_property('text'))
        if isinstance(a,unreal.GuLiCommanderDeploymentPoint):
            require(a.get_editor_property('use_in_population_manifest') and a.get_editor_property('start_idle') and not a.get_editor_property('allow_automatic_fire'), 'Passive real target')
            row['shield'] = a.get_editor_property('initial_shield')
        entities.append(row)
    manifests = []
    for population in (200,600):
        path = ROOT/f'Config/Hybrid/RuntimeManifest-{population}.json'
        exported = json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population,str(path)))
        require(exported.get('success'), 'Export '+str(exported))
        checked = json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(path)))
        require(checked.get('success'), 'Validate '+str(checked))
        manifests.append({'population':population,'export':exported,'readback':checked})
    resource = unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    navigation = unreal.GuLiNavigationBakeLibrary.validate_world_navigation(world)
    require(resource.success and navigation.success, 'Resource/navigation readiness')
    return {'success':True,'map':MAP,'radii_cm':values,'entities':entities,'manifests':manifests,
            'dependencies':dependencies,'resource':str(resource.message),'navigation':str(navigation.message),
            'preserved_existing_transforms':len(before),'visual_acceptance':'pending_user'}


try: report = main()
except Exception: report = {'success':False,'error':traceback.format_exc()}
(OUT/'scene_delivery.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'error':report.get('error')},ensure_ascii=False))
