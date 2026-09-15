"""Disposable PIE network acceptance. No assets or settings are saved.

Source editor: -ExecutePythonScript=.../validate_component_skills_network.py
-ComponentSkillsMode=2 (dedicated, default) or 1 (listen).
"""
import json
import os
import re
import runpy
import time
import traceback
from pathlib import Path
import unreal

QA = unreal.GuLiComponentSkillQALibrary
ROOT = Path(unreal.Paths.project_dir()).resolve()
match = re.search(r'-ComponentSkillsMode=(\d)', unreal.SystemLibrary.get_command_line())
MODE = int(match.group(1)) if match else 2


def prop(obj, name):
    return obj.get_editor_property(name)


def point(unit):
    return unreal.Vector(unit['x'], unit['y'], unit['z'])


def world_snapshots():
    return [(w, json.loads(unreal.GuLiTeleportQALibrary.snapshot(w)))
            for w in unreal.ObjectIterator(unreal.World) if 'UEDPIE_' in w.get_path_name()]


def pcs(world):
    return unreal.GameplayStatics.get_all_actors_of_class(world, unreal.PlayerController)


def reply_data(component):
    reply = component.get_last_reply()
    return {'id': reply.request_id.to_string(), 'code': str(reply.code), 'error': reply.error,
            'units': [{'id': prop(x.soldier_id, 'value'), 'code': str(x.code),
                       'effect': x.execution.effect_id.to_string(), 'ready_at': x.ready_at_server_seconds}
                      for x in reply.units]}


class Run:
    def __init__(self):
        self.stage = 0
        self.started = self.stage_at = time.monotonic()
        self.catalog = unreal.load_asset('/Game/GuLiStrike/Commander/Skills/DA_CommanderSkills_V1')
        self.original_skills = list(prop(self.catalog, 'skills'))
        self.original_mapping = dict(prop(self.catalog, 'unit_skills'))
        self.out = ROOT / 'TestResults/ComponentSkills/Network' / ('dedicated.json' if MODE == 2 else 'listen.json')
        self.out.parent.mkdir(parents=True, exist_ok=True)
        self.report = {'pid': os.getpid(), 'mode': MODE, 'checks': {}, 'replies': [], 'passed': False, 'errors': []}
        unreal.EditorPythonScripting.set_keep_python_script_alive(True)
        assert QA.start_pie(MODE, 2), 'PIE start rejected'
        self.handle = unreal.register_slate_post_tick_callback(self.tick)
        self.save()

    def check(self, name, success):
        self.report['checks'][name] = bool(success)
        self.save()
        assert success, name

    def next(self, stage):
        self.stage = stage
        self.stage_at = time.monotonic()
        self.save()

    def save(self):
        self.report['stage'] = self.stage
        self.out.write_text(json.dumps(self.report, ensure_ascii=False, indent=2), encoding='utf-8')

    def send(self, point_valid=True, revision=None, request=None, point_value=None):
        self.request = request or unreal.GuidLibrary.new_guid()
        self.sent_revision = self.selection['revision'] if revision is None else revision
        self.check('native_owned_rpc_sent', QA.submit_skill(self.skills, self.request, 'None', self.sent_revision,
                                                           point_valid, point_value or self.ground))

    def has_reply(self):
        return self.skills.get_last_reply().request_id.to_string() == self.request.to_string()

    def capture_reply(self):
        data = reply_data(self.skills)
        self.report['replies'].append(data)
        return data

    def tick(self, delta):
        try:
            now = time.monotonic()
            assert now - self.started < 240, 'Network acceptance timed out'
            wait = now - self.stage_at
            if self.stage == 0:
                snapshots = world_snapshots()
                server = next(((w, s) for w, s in snapshots if s['net_mode'] in (1, 2)), None)
                if not server or not server[1]['units']:
                    return
                local = [p for w, _ in snapshots for p in pcs(w) if p.is_local_controller() and p.player_state
                         and p.player_state.is_commander() and p.player_state.get_team() == unreal.GuLiTeam.RED]
                if not local:
                    return
                self.pc = local[0]
                self.selection = json.loads(QA.selection_snapshot(self.pc))
                if not self.selection.get('ready'):
                    return
                self.world, self.initial = server
                self.skills = self.pc.player_state.get_component_by_class(unreal.GuLiCommanderSkillComponent)
                self.red = [u for u in self.initial['units'] if u['team'] == 1 and u['health'] > 0 and not u['locked']]
                if not self.red:
                    return
                self.first = self.red[0]
                self.ground = point(self.first) + unreal.Vector(0, 4000, 0)
                self.check('selection_rpc_sent', QA.select_radius(self.pc, point(self.first), 70001, False))
                self.next(1)
            elif self.stage == 1 and wait > 0.5:
                selection = json.loads(QA.selection_snapshot(self.pc))
                if selection['pending'] or not selection['members']:
                    return
                second = next((u for u in self.red if u['type'] != self.first['type']), None)
                if second:
                    self.check('mixed_selection_rpc_sent', QA.select_radius(self.pc, point(second), 70002, True))
                self.next(2)
            elif self.stage == 2 and wait > 0.5:
                self.selection = json.loads(QA.selection_snapshot(self.pc))
                if self.selection['pending'] or not self.selection['members']:
                    return
                self.before_empty_q = reply_data(self.skills)
                QA.press_q(self.skills, True, self.ground)
                self.next(3)
            elif self.stage == 3 and wait > 0.5:
                self.check('all_no_skill_q_sends_no_request', reply_data(self.skills) == self.before_empty_q)
                types = sorted({u['type'] for u in self.red if u['id'] in self.selection['members']})
                self.report['selected_types'] = types
                config = unreal.new_object(unreal.GuLiPointSkillConfiguration)
                config.set_editor_property('projectile', unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_Missile'))
                config.set_editor_property('field_config_id', 'WM01_MissileExplosion')
                definitions = []
                mapping = {}
                for index, unit_type in enumerate(types):
                    definition = unreal.GuLiActiveSkillDefinition()
                    for name, value in dict(skill_id='QA.Point.' + str(unit_type), scope=unreal.GuLiActiveSkillScope.UNIT,
                                            target_mode=unreal.GuLiActiveSkillTargetMode.GROUND_POINT, cooldown_seconds=60.0,
                                            range_centimeters=10000000.0 if index == 0 else 1.0,
                                            executor_class=unreal.GuLiPointSkillExecutor, configuration=config).items():
                        definition.set_editor_property(name, value)
                    definitions.append(definition)
                    mapping[unit_type] = prop(definition, 'skill_id')
                self.catalog.set_editor_property('skills', self.original_skills + definitions)
                self.catalog.set_editor_property('unit_skills', mapping)
                self.check('qa_configuration_valid', not unreal.GuLiSkillAuthoringLibrary.validate_commander_catalog(self.catalog))
                QA.press_q(self.skills, True, self.ground)
                self.next(4)
            elif self.stage == 4 and wait > 0.5:
                data = self.capture_reply()
                self.check('q_executes_real_point_effects', any('SUCCEEDED' in u['code'] for u in data['units']))
                if len(self.report['selected_types']) > 1:
                    self.check('mixed_units_skip_out_of_range', any('OUT_OF_RANGE' in u['code'] for u in data['units']))
                self.check('selected_cooldown_views_replicated', bool(self.skills.get_selected_unit_skills()))
                self.original_reply = data
                self.original_request = self.skills.get_last_reply().request_id
                self.send(request=self.original_request)
                self.next(5)
            elif self.stage == 5 and wait > 0.5 and self.has_reply():
                self.check('duplicate_returns_original_effect_ids_and_cooldowns', self.capture_reply() == self.original_reply)
                self.send()
                self.next(6)
            elif self.stage == 6 and self.has_reply():
                data = self.capture_reply()
                self.check('successful_units_have_independent_cooldowns', any('COOLDOWN' in u['code'] for u in data['units']))
                self.send(point_valid=False)
                self.next(7)
            elif self.stage == 7 and self.has_reply():
                data = self.capture_reply()
                if len(self.report['selected_types']) > 1:
                    self.check('no_ground_rejected_for_ready_point_units', any('INVALID_GROUND' in u['code'] for u in data['units']))
                self.send(revision=self.selection['revision'] + 1)
                self.next(8)
            elif self.stage == 8 and self.has_reply():
                self.check('stale_selection_rejected', 'STALE_SELECTION' in self.capture_reply()['code'])
                self.send(request=self.original_request, point_value=self.ground + unreal.Vector(100, 0, 0))
                self.next(9)
            elif self.stage == 9 and wait > 0.5 and self.has_reply():
                self.check('request_id_content_reuse_rejected', 'different' in self.capture_reply()['error'])
                self.catalog.set_editor_property('skills', self.original_skills)
                self.catalog.set_editor_property('unit_skills', self.original_mapping)
                self.check('air_join_prepared', unreal.GuLiTeleportQALibrary.prefer_air_for_next_join(self.world))
                unreal.GuLiTeleportQALibrary.request_late_join()
                self.next(10)
            elif self.stage == 10 and wait > 2:
                ships = [s for s in unreal.GameplayStatics.get_all_actors_of_class(self.world, unreal.GuLiStrikeShip)
                         if s.player_state and s.player_state.get_team() == unreal.GuLiTeam.RED]
                if not ships:
                    return
                self.ship = ships[0]
                self.check('ship_has_no_startup_hangar', self.ship.get_hangar_capability() is None)
                self.empty_teleport = runpy.run_path(str(ROOT / 'Scripts/validate_commander_teleport_network.py'),
                    init_globals={'TELEPORT_QA_OUTPUT_ROOT': 'TestResults/ComponentSkills/Network/EmptyShip',
                                  'TELEPORT_QA_LATE_JOIN': False})['teleport_network_run']
                self.next(14)
            elif self.stage == 14 and self.empty_teleport.finished:
                self.check('global_teleport_without_hangar', self.empty_teleport.report['passed'])
                build = self.ship.player_state.get_component_by_class(unreal.GuLiShipBuildComponent)
                # UE Python maps a bool + one output parameter to that output, or None on failure.
                reason = QA.commit_ship_choice(build, '08')
                self.report['choice_reason'] = reason
                self.check('server_choice_commits_hangar', reason is not None)
                self.next(11)
            elif self.stage == 11 and wait > 4:
                self.report['wingman_bootstrap'] = {w.get_path_name(): json.loads(QA.wingman_snapshot(w)) for w, _ in world_snapshots()}
                ships = [s for w, _ in world_snapshots() for s in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.GuLiStrikeShip)
                         if s.player_state and s.player_state.get_battle_slot_index() == self.ship.player_state.get_battle_slot_index()]
                self.check('all_peers_reconstruct_one_hangar', len(ships) >= 3 and all(len(s.get_components_by_class(unreal.GuLiShipHangarCapabilityComponent)) == 1 for s in ships))
                self.check('all_peers_reconstruct_two_mounts', all(s.get_part_at('wingman_bay_1') and s.get_part_at('wingman_bay_2') for s in ships))
                self.teleport = runpy.run_path(str(ROOT / 'Scripts/validate_commander_teleport_network.py'),
                    init_globals={'TELEPORT_QA_OUTPUT_ROOT': 'TestResults/ComponentSkills/Network'})['teleport_network_run']
                self.next(12)
            elif self.stage == 12 and self.teleport.finished:
                self.check('global_teleport_and_late_join', self.teleport.report['passed'])
                self.report['passed'] = True
                self.finish()
        except Exception:
            self.report['errors'].append(traceback.format_exc())
            self.finish()

    def finish(self):
        unreal.unregister_slate_post_tick_callback(self.handle)
        self.catalog.set_editor_property('skills', self.original_skills)
        self.catalog.set_editor_property('unit_skills', self.original_mapping)
        self.save()
        unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)


component_skill_network_run = Run()
