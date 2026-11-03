"""Extend the existing Ground Buff area with saved fixed-5m boundary cases."""
import json
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/GroundRogueFix_20261102'
MAP = '/Game/Maps/LVL_CommanderMassPrototype'
PREFIX = 'GroundBuff_FiveMeter_'
# Existing 300 cm authoring grid, odd footprints use cell centers.
ORIGIN = (-13950, 51150, 1075)
CASES = [('Host', (0, 0), 1, unreal.GuLiTeam.BLUE, 3000, 0),
         ('Inside', (0, 600), 1, unreal.GuLiTeam.BLUE, 1000, 0),
         ('BodyIntersection', (600, 0), 1, unreal.GuLiTeam.BLUE, 1000, 0),
         ('Outside', (1200, 600), 1, unreal.GuLiTeam.BLUE, 1000, 0),
         ('Friendly', (0, -600), 1, unreal.GuLiTeam.RED, 1000, 0),
         ('Shield', (-600, 0), 1, unreal.GuLiTeam.BLUE, 1000, 250)]


def require(value, message):
    if not value:
        raise RuntimeError(message)
    return value


def main():
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    require(not level.is_in_play_in_editor(), 'Preserve active PIE')
    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    require(not dirty or (globals().get('GULI_BURST_SCENE_RETRY_OWNED', False) and len(dirty) == 1 and dirty[0].get_name() == MAP), 'Preserve unsaved maps')
    current_map = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_outermost().get_name()
    if current_map != MAP:
        require(level.load_level(MAP), 'Load formal map')
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = {a.get_actor_label(): a for a in actor_subsystem.get_all_level_actors()}
    before = {k: a.get_actor_transform().to_tuple() for k, a in actors.items() if not k.startswith(PREFIX)}
    require('GroundRogue_Entry' in actors and 'GroundBuff_LargeBurnHost' in actors, 'Reuse the established Ground area')
    def get(suffix, cls, position):
        label = PREFIX + suffix
        a = actors.get(label)
        if a is None:
            a = require(actor_subsystem.spawn_actor_from_class(cls, unreal.Vector(*position)), label)
            a.set_actor_label(label)
            actors[label] = a
        require(isinstance(a, cls), 'Unexpected actor class ' + label)
        a.modify()
        a.set_actor_location(unreal.Vector(*position), False, False)
        tags = list(a.get_editor_property('tags'))
        if unreal.Name('GuLi.Review.GroundBuff') not in tags:
            tags.append(unreal.Name('GuLi.Review.GroundBuff'))
        a.set_editor_property('tags', tags)
        return a
    for name, offset, unit, team, hp, shield in CASES:
        a = get(name, unreal.GuLiCommanderDeploymentPoint, (ORIGIN[0]+offset[0], ORIGIN[1]+offset[1], ORIGIN[2]))
        for key, value in [('team', team), ('unit_type_id', unit), ('rows', 1), ('columns', 1),
                           ('spacing_centimeters', 600.), ('allow_automatic_fire', False), ('start_idle', True),
                           ('use_in_population_manifest', True), ('initial_max_health_override', float(hp)), ('initial_shield', float(shield))]:
            a.set_editor_property(key, value)
    # Reuse the already baked GroundBuff_BurstWall; no new navigation geometry.
    wall = actors.pop(PREFIX + 'Wall', None)
    if wall:
        require(actor_subsystem.destroy_actor(wall), 'Remove owned temporary wall')
    note = get('Instructions', unreal.Note, (ORIGIN[0]+200, ORIGIN[1]-900, ORIGIN[2]+100))
    note.set_editor_property('text', 'Red Ground：F4先取灼烧，再取灼烧爆裂；向Host射击。\n'
        '先驱号Host；Inside/BodyIntersection中心6米但体积进入5米球、Outside中心约13.4米、Friendly友军、Shield护盾。\n'
        '以运行时真实受击中心和碰撞半径核对距离；爆燃各目标共同伤害，独立2秒周期，刷新不推迟。\n'
        '原GroundRogue群验证折射；原LargeBurnHost验证大宿主仍固定5米。新版本仅新局验收。')
    require(unreal.EditorLoadingAndSavingUtils.save_map(world, MAP), 'Save map')
    # Read saved entities without replacing a World held by Python wrappers.
    # A separate fresh editor process provides disk reload verification.
    saved = {a.get_actor_label(): a for a in actor_subsystem.get_all_level_actors()}
    require(before == {k: saved[k].get_actor_transform().to_tuple() for k in before}, 'Preserve unrelated actor transforms')
    entities = []
    for label, a in saved.items():
        if not label.startswith(PREFIX):
            continue
        row = {'label': label, 'class': a.get_class().get_path_name(), 'location': list(a.get_actor_location().to_tuple()),
               'scale': list(a.get_actor_scale3d().to_tuple()), 'map': a.get_outermost().get_name()}
        if isinstance(a, unreal.GuLiCommanderDeploymentPoint):
            row.update({k: str(a.get_editor_property(k)) for k in ['team', 'unit_type_id', 'rows', 'columns',
                'start_idle', 'allow_automatic_fire', 'use_in_population_manifest', 'initial_max_health_override', 'initial_shield']})
        entities.append(row)
    require(len(entities) == len(CASES)+1, 'Saved boundary actors')
    additions = [a for k, a in saved.items() if k.startswith(PREFIX) and isinstance(a, unreal.GuLiCommanderDeploymentPoint)]
    certification = unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    if not certification.success:
        certification = unreal.GuLiResourceAuthoringLibrary.revalidate_added_deployments(additions, True)
    require(certification.success, 'Added-deployment certification: ' + str(certification))
    manifests = []
    for population in (200, 600):
        path = ROOT / f'Config/Hybrid/RuntimeManifest-{population}.json'
        exported = json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population, str(path)))
        require(exported.get('success'), 'Manifest export: ' + json.dumps(exported))
        readback = json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(path)))
        require(readback.get('success'), 'Manifest validation: ' + json.dumps(readback))
        manifests.append({'population': population, 'export': exported, 'validation': readback})
    resource = unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    navigation = unreal.GuLiNavigationBakeLibrary.validate_world_navigation(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world())
    require(resource.success and navigation.success, 'Resource/navigation certification: ' + str(resource) + str(navigation))
    return {'success': True, 'saved': True, 'map': MAP, 'entities': entities, 'manifests': manifests,
            'resource': str(resource), 'navigation': str(navigation), 'certification': str(certification), 'assistant_visual_test': False}


try:
    report = main()
except Exception:
    report = {'success': False, 'error': traceback.format_exc()}
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'scene_install.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps(report, ensure_ascii=False))
