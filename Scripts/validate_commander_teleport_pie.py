"""Exercise the commander global-skill RPC/field flow in PIE; retain structured evidence."""
import json
import traceback
from pathlib import Path
import unreal


class TeleportPIERun:
    def __init__(self):
        self.world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        assert self.world, 'Start commander PIE first'
        self.pc = unreal.GameplayStatics.get_player_controller(self.world, 0)
        self.input = self.pc.get_component_by_class(unreal.GuLiTeleportInputComponent)
        self.presentation = unreal.GameplayStatics.get_all_actors_of_class(self.world, unreal.GuLiCommanderPresentationActor)[0]
        self.suffix = globals().get('TELEPORT_QA_SUFFIX','')
        self.out = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / 'TestResults/CommanderTeleport' / globals().get('TELEPORT_QA_REPORT','pie-flow'+self.suffix+'.json')
        self.report = {'world': self.world.get_path_name(), 'cases': [], 'passed': False, 'errors': []}
        self.cases = globals().get('TELEPORT_QA_CASES',['success', 'crowded', 'cancel_after_capture', 'timeout', 'cancel_before_capture'])
        self.camera = unreal.GuLiTeleportQALibrary.create_capture(self.world)
        self.capture = self.camera.capture_component2d
        self.capture.set_editor_property('capture_every_frame',False)
        self.capture.set_editor_property('capture_on_movement',False)
        self.capture.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
        self.capture.set_editor_property('fov_angle',60)
        self.target = unreal.RenderingLibrary.create_render_target2d(self.world,1400,1000,unreal.TextureRenderTargetFormat.RTF_RGBA8,unreal.LinearColor(0,0,0,1),False,False)
        self.capture.set_editor_property('texture_target',self.target)
        self.shots = set()
        self.case_index = -1
        self.last_action = 0
        self.handle = unreal.register_slate_post_tick_callback(self.tick)
        self.start_case()

    def now(self):
        return unreal.GameplayStatics.get_time_seconds(self.world)

    def blue_count(self):
        return sum(c.get_instance_count() for c in self.presentation.get_components_by_class(unreal.InstancedStaticMeshComponent)
                   if 'M_TeleportBody' in str(c.get_material(0)))

    def start_case(self):
        self.case_index += 1
        if self.case_index == len(self.cases):
            self.report['passed'] = all(c.get('passed') for c in self.report['cases'])
            self.finish()
            return
        self.name = self.cases[self.case_index]
        normal = next(c for c in self.presentation.get_components_by_class(unreal.InstancedStaticMeshComponent) if c.get_name() == 'UnitInstances')
        self.source = normal.get_instance_transform(0, world_space=True).translation
        self.input.set_server_level(4)
        unreal.GuLiTeleportQALibrary.submit_intent(self.input, unreal.GuLiTeleportCommand.SOURCE, unreal.Guid(), self.source)
        state = self.input.query_state()
        assert state.phase == unreal.GuLiTeleportPhase.WINDUP, 'Source rejected: ' + state.message
        self.cast = state.cast_id
        self.started = self.now()
        self.stage = 0
        self.seen = set()
        self.case = {'name': self.name, 'events': [], 'checks': {}, 'source': [self.source.x,self.source.y,self.source.z]}
        self.pc.get_controlled_pawn().jump_to_world_location(self.source)
        self.case['before'] = json.loads(unreal.GuLiTeleportQALibrary.snapshot(self.world))
        self.blocker = None
        self.report['cases'].append(self.case)
        self.save()

    def tick(self, delta):
        try:
            state = self.input.query_state()
            now = self.now()
            elapsed = now - self.started
            phase = state.phase
            if self.name == 'success':
                for at,label in [(0.12,'start'),(1.5,'half'),(2.97,'full'),(3.3,'rise'),(3.8,'landing'),(4.15,'fade')]:
                    if label=='landing' and (phase!=unreal.GuLiTeleportPhase.RECOVERY or now-state.phase_start_time<.26):
                        continue
                    if label=='fade' and (phase!=unreal.GuLiTeleportPhase.FINISHED or now-state.phase_start_time<.25):
                        continue
                    if elapsed>=at and label not in self.shots:
                        self.shots.add(label)
                        center = state.destination if label in ['landing','fade'] else self.source
                        wide = label in ['rise','landing','fade']
                        position = center+unreal.Vector(0,-20000 if wide else -7500,12000 if wide else 9000)
                        look = center+unreal.Vector(0,0,5000 if wide else 0)
                        self.camera.set_actor_location_and_rotation(position,unreal.MathLibrary.find_look_at_rotation(position,look),False,True)
                        self.capture.capture_scene()
                        folder = self.out.parent / ('Visuals'+self.suffix)
                        folder.mkdir(exist_ok=True)
                        unreal.RenderingLibrary.export_render_target(self.world,self.target,str(folder),label+'.png')
                        self.case.setdefault('screenshots',[]).append({'stage':label,'time':elapsed,'path':str(folder/(label+'.png'))})
                        if label=='half':
                            unreal.SystemLibrary.execute_console_command(self.world,'Shot SHOWUI filename='+str(folder/'hud.png')+' -nosuffix',self.pc)
            if phase not in self.seen:
                self.seen.add(phase)
                self.case['events'].append({'phase': str(phase), 'elapsed': elapsed, 'participants': state.participant_count,
                                            'blue_instances': self.blue_count(), 'message': state.message})
                self.save()
            if self.name == 'cancel_before_capture':
                if self.stage == 0 and elapsed >= 1:
                    unreal.GuLiTeleportQALibrary.submit_intent(self.input, unreal.GuLiTeleportCommand.CANCEL,self.cast,unreal.Vector())
                    self.stage = 1
            elif phase == unreal.GuLiTeleportPhase.WINDUP and elapsed >= 1 and self.stage == 0:
                unreal.GuLiTeleportQALibrary.submit_intent(self.input, unreal.GuLiTeleportCommand.DESTINATION,self.cast,self.source+unreal.Vector(3000,0,0))
                self.case['checks']['early_click_ignored'] = self.input.query_state().phase == unreal.GuLiTeleportPhase.WINDUP
                self.stage = 1
            elif phase == unreal.GuLiTeleportPhase.AWAITING_DESTINATION:
                self.case['checks']['capture_at_three_seconds'] = elapsed >= 3
                self.case['checks']['captured_nonempty'] = state.participant_count > 0
                if self.blue_count() > 0:
                    self.case['checks']['blue_body_visible'] = True
                if self.stage == 1 and elapsed >= 3.2:
                    self.case['captured'] = json.loads(unreal.GuLiTeleportQALibrary.snapshot(self.world))
                    captured = [u for u in self.case['captured']['units'] if u['phased']]
                    if captured:
                        self.case['checks']['phased_unit_immune'] = not unreal.GuLiTeleportQALibrary.damage_mass(self.world,captured[0]['id'],1)
                    if self.name == 'crowded':
                        self.blocker = unreal.GuLiTeleportQALibrary.create_blocker(self.world,self.source+unreal.Vector(3000,0,5000),unreal.Vector(4100,4100,5000))
                    self.case['deadline'] = state.deadline
                    if self.name == 'cancel_after_capture':
                        unreal.GuLiTeleportQALibrary.submit_intent(self.input, unreal.GuLiTeleportCommand.CANCEL,self.cast,unreal.Vector())
                    else:
                        unreal.GuLiTeleportQALibrary.submit_intent(self.input, unreal.GuLiTeleportCommand.DESTINATION,self.cast,(self.source+unreal.Vector(3000,0,0)) if self.name == 'crowded' else unreal.Vector(99999999,0,0))
                        after = self.input.query_state()
                        self.case['checks']['invalid_target_keeps_deadline'] = after.deadline == state.deadline and after.phase == phase
                    self.stage = 2
                elif self.name in ['success','crowded'] and self.stage == 2 and elapsed >= 3.5:
                    if self.blocker:
                        self.blocker.destroy_actor()
                        self.blocker = None
                    unreal.GuLiTeleportQALibrary.submit_intent(self.input, unreal.GuLiTeleportCommand.DESTINATION,self.cast,self.source+unreal.Vector(3000,0,0))
                    after = self.input.query_state()
                    self.case['checks']['landing_accepted'] = after.phase == unreal.GuLiTeleportPhase.RECOVERY
                    self.stage = 3
            elif phase == unreal.GuLiTeleportPhase.RECOVERY:
                self.case['checks']['recovery_is_half_second'] = abs(state.deadline-state.phase_start_time-.5) < .001
                self.case['checks']['landed'] = state.landed
            if phase in [unreal.GuLiTeleportPhase.FINISHED,unreal.GuLiTeleportPhase.IDLE] and elapsed > 1.1:
                if self.name == 'timeout':
                    self.case['checks']['wait_is_ten_seconds'] = elapsed >= 13
                self.case['checks']['blue_bodies_restored'] = self.blue_count() == 0
                if 'captured' in self.case:
                    after = json.loads(unreal.GuLiTeleportQALibrary.snapshot(self.world))
                    self.case['after'] = after
                    by_id = {u['id']:u for u in after['units']}
                    captured = [u for u in self.case['captured']['units'] if u['phased']]
                    self.case['checks']['identity_type_health_preserved'] = all(u['id'] in by_id and u['type']==by_id[u['id']]['type'] and u['health']==by_id[u['id']]['health'] for u in captured)
                    self.case['checks']['action_locks_released'] = all(not by_id[u['id']]['locked'] for u in captured)
                self.case['passed'] = bool(self.case['checks']) and all(self.case['checks'].values())
                if phase == unreal.GuLiTeleportPhase.IDLE:
                    self.start_case()
            if elapsed > 25:
                raise RuntimeError('Field did not finish within bounded test duration')
        except Exception:
            self.report['errors'].append(traceback.format_exc())
            self.finish()

    def save(self):
        self.out.parent.mkdir(parents=True,exist_ok=True)
        self.out.write_text(json.dumps(self.report,ensure_ascii=False,indent=2),encoding='utf-8')

    def finish(self):
        unreal.unregister_slate_post_tick_callback(self.handle)
        if self.camera: self.camera.destroy_actor()
        if getattr(self,'blocker',None): self.blocker.destroy_actor()
        self.save()
        unreal.log('Teleport PIE validation: ' + str(self.report['passed']))


teleport_pie_run = TeleportPIERun()
print(json.dumps({'started': True, 'report': str(teleport_pie_run.out)}))
