"""Targeted PIE review using real placement input and the construction vehicle."""
import gc
import json
import time
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/outputs/construction-20260922')


class ConstructionSampleReview:
    # Only primitive identifiers survive between ticks, so ending PIE cannot pin its world.
    def __init__(self):
        self.stage = 'aim'
        self.stage_start = time.monotonic()
        self.started = self.stage_start
        self.next_tick = 0.0
        self.factory_name = ''
        self.builder_name = ''
        self.blocker_name = ''
        self.rotation_count = 0
        self.report = {'checks': {}, 'events': [], 'images': [], 'finished': False}
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def save(self):
        (OUT / 'runtime-review.json').write_text(json.dumps(self.report, ensure_ascii=False, indent=2), encoding='utf-8')

    def event(self, name, value):
        self.report['events'].append({'event': name, 'value': value})
        self.save()

    def check(self, name, value):
        self.report['checks'][name] = bool(value)
        self.save()
        assert value, name

    def advance(self, stage):
        self.stage = stage
        self.stage_start = time.monotonic()
        self.event('stage', stage)

    def inject(self, action):
        for subsystem in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem):
            if not subsystem.get_name().startswith('Default__'):
                subsystem.inject_input_vector_for_action(action, unreal.Vector(1, 0, 0), [], [])

    def battle_input(self, name):
        self.inject(unreal.load_asset('/Game/GuLiStrike/GroundMech/Input/IA_Battle_' + name))

    def rotate_input(self, placement):
        self.inject(next(a for a in unreal.ObjectIterator(unreal.InputAction) if a.get_outer() == placement))

    def cursor(self, pc, point):
        screen = unreal.GameplayStatics.project_world_to_screen(pc, point)
        assert screen
        pc.set_mouse_location(round(screen.x), round(screen.y))

    def capture(self, label):
        scope = {'CONSTRUCTION_SHOT': label}
        source = (Path('D:/UE5.7/test1/Scripts/Construction') / 'capture_construction_runtime.py').read_text(encoding='utf-8')
        exec(compile(source.rsplit('\nmain()', 1)[0], 'capture_construction_runtime.py', 'exec'), scope)
        result = scope['run']()
        self.report['images'].append(result['image'])
        self.save()

    def tint(self, preview):
        color = preview.get_components_by_class(unreal.MeshComponent)[0].get_material(0).get_vector_parameter_value('TintColor')
        return (color.r, color.g, color.b)

    def factory(self, world):
        return next((a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GuLiResourceFactoryActor)
            if a.get_name() == self.factory_name), None)

    def builder(self, world):
        return next((a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GuLiConstructionVehiclePawn)
            if a.get_name() == self.builder_name), None)

    def snapshot(self, factory):
        def pose(component):
            transform = component.get_world_transform()
            rotation = transform.rotation
            return {'position': transform.translation.to_tuple(), 'scale': transform.scale3d.to_tuple(),
                'rotation': (rotation.x, rotation.y, rotation.z, rotation.w)}
        life = factory.get_component_by_class(unreal.GuLiBuildingLifecycleComponent)
        visual = factory.get_component_by_class(unreal.GuLiBuildingConstructionVisualComponent)
        children = factory.get_all_child_actors(True)
        meshes = [m for a in [factory] + list(children) for m in a.get_components_by_class(unreal.MeshComponent)]
        source = [m for m in meshes if 'GuLiConstructionProxy' not in [str(t) for t in m.component_tags]]
        return {'phase': str(life.get_state().phase), 'work': life.get_state().work_done,
            'progress': life.get_construction_progress(), 'display': visual.get_displayed_progress(),
            'construction_visible': visual.is_construction_visible(),
            'collision_transform': pose(factory.root_component),
            'dock': factory.get_dock_point().to_tuple(),
            'originals': [{'name': m.get_name(), 'transform': pose(m), 'visible': m.is_visible()}
                for m in source],
            'proxy_count': len(meshes) - len(source),
            'lights': [l.is_visible() for a in children for l in a.get_components_by_class(unreal.LightComponent)]}

    def tick(self, dt):
        now = time.monotonic()
        if now < self.next_tick:
            return
        self.next_tick = now + 0.08
        try:
            world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            assert world, 'PIE ended'
            assert now - self.started < 240, 'Review timed out'
            pc = unreal.GameplayStatics.get_player_controller(world, 0)
            placement = pc.get_building_placement_component()
            previews = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GuLiBuildingPlacementPreview)
            preview = previews[0] if previews else None
            age = now - self.stage_start
            point = unreal.Vector(4000, 224000, -6418)
            if self.stage in ('red', 'funding', 'rotate', 'blocker_setup', 'blocked', 'unblocked', 'place'):
                self.cursor(pc, point)

            if self.stage == 'aim':
                self.cursor(pc, point)
                self.advance('red')
            elif self.stage == 'red' and age > .3:
                self.check('insufficient_resources_red', preview and self.tint(preview)[0] > .9)
                self.capture('placement-red')
                factory = min((a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GuLiResourceFactoryActor)
                    if a.get_team() == unreal.GuLiTeam.RED), key=lambda a: (a.get_actor_location().x ** 2 + (a.get_actor_location().y - 224000) ** 2))
                self.check('queue_test_ore', factory.enqueue_raw(unreal.GuLiResourceType.RED, 100))
                factory.enqueue_raw(unreal.GuLiResourceType.BLUE, 100)
                self.advance('funding')
            elif self.stage == 'funding' and age > .5 and pc.player_state.get_resource_inventory().red >= 10 and pc.player_state.get_resource_inventory().blue >= 40 and preview and not preview.get_editor_property('hidden') and self.tint(preview)[1] > .9:
                self.check('funded_preview_green', preview and self.tint(preview)[1] > .9)
                self.capture('placement-green')
                self.initial_yaw = preview.get_actor_rotation().yaw
                self.report['rotation_yaws'] = [self.initial_yaw]
                self.rotate_input(placement)
                self.advance('rotate')
            elif self.stage == 'rotate' and age > .25:
                self.rotation_count += 1
                yaw = preview.get_actor_rotation().yaw
                self.report['rotation_yaws'].append(yaw)
                self.check('rotation_' + str(self.rotation_count), abs((yaw - self.initial_yaw) % 360 - (90 * self.rotation_count) % 360) < .01)
                if self.rotation_count < 4:
                    self.rotate_input(placement)
                    self.stage_start = now
                else:
                    pc.get_controlled_pawn().jump_to_world_location(unreal.Vector(-4551, 225349, -6418))
                    self.advance('negative_aim')
            elif self.stage == 'negative_aim' and age > .3:
                self.cursor(pc, unreal.Vector(-4551, 225349, -6418))
                self.advance('negative')
            elif self.stage == 'negative' and age > .3:
                p = preview.get_actor_location()
                self.check('negative_world_grid_snap', p.x < 0 and abs(p.x / 100 - round(p.x / 100)) < .001
                    and abs(p.y / 100 - round(p.y / 100)) < .001)
                self.event('negative_candidate', p.to_tuple())
                pc.get_controlled_pawn().jump_to_world_location(point)
                self.advance('blocker_setup')
            elif self.stage == 'blocker_setup' and age > .3:
                blocker = unreal.GuLiTeleportQALibrary.create_blocker(world, point + unreal.Vector(0, 0, 300), unreal.Vector(100, 100, 250))
                self.blocker_name = blocker.get_name()
                self.advance('blocked')
            elif self.stage == 'blocked' and age > .4:
                self.check('stationary_dynamic_obstacle_red', self.tint(preview)[0] > .9)
                next(a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor) if a.get_name() == self.blocker_name).destroy_actor()
                self.advance('unblocked')
            elif self.stage == 'unblocked' and age > .4:
                self.check('obstacle_removed_green', self.tint(preview)[1] > .9)
                width, height = pc.get_viewport_size()
                pc.set_mouse_location(round(width * .75), round(height * .88))
                self.advance('ui')
            elif self.stage == 'ui' and age > .3:
                self.check('ui_hides_preview_and_grid', preview.get_editor_property('hidden'))
                self.cursor(pc, point)
                self.advance('place')
            elif self.stage == 'place' and age > .3:
                self.check('ready_to_place', self.tint(preview)[1] > .9 and not preview.get_editor_property('hidden'))
                self.battle_input('Primary')
                self.advance('waiting_placement')
            elif self.stage == 'waiting_placement':
                factories = [a for a in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GuLiResourceFactoryActor)
                    if a.get_component_by_class(unreal.GuLiBuildingLifecycleComponent).get_state().phase == unreal.GuLiBuildingPhase.UNDER_CONSTRUCTION]
                if factories:
                    factory = min(factories, key=lambda a: abs(a.get_actor_location().x - 4000) + abs(a.get_actor_location().y - 224000))
                    self.factory_name = factory.get_name()
                    self.battle_input('Cancel')
                    self.advance('blueprint')
                else:
                    assert age < 5, 'Normal primary placement did not spawn a factory'
            elif self.stage == 'blueprint' and age > .3:
                factory = self.factory(world)
                self.initial = self.snapshot(factory)
                self.check('unbuilt_blueprint', self.initial['work'] == 0 and self.initial['construction_visible'] and self.initial['proxy_count'] >= 6)
                self.check('unbuilt_lights_off', not any(self.initial['lights']))
                self.event('unbuilt', self.initial)
                self.capture('construction-unbuilt')
                builder = min(unreal.GameplayStatics.get_all_actors_of_class(world, unreal.GuLiConstructionVehiclePawn),
                    key=lambda a: (a.get_actor_location().x - 4000) ** 2 + (a.get_actor_location().y - 224000) ** 2)
                self.builder_name = builder.get_name()
                self.check('real_builder_order_accepted', builder.issue_construction(factory.get_component_by_class(unreal.GuLiBuildingLifecycleComponent)))
                self.advance('building')
            elif self.stage == 'building':
                factory = self.factory(world)
                current = self.snapshot(factory)
                if current['progress'] >= .4:
                    self.capture('construction-progress')
                    self.check('collision_stationary_during_rise', current['collision_transform'] == self.initial['collision_transform'] and current['dock'] == self.initial['dock'])
                    self.event('mid_build', current)
                    self.cursor(pc, self.builder(world).get_actor_location())
                    self.battle_input('Primary')
                    self.advance('select_builder')
            elif self.stage == 'select_builder' and age > .35:
                # SetupInputComponent creates M, S, Tab, F10 in that order on this controller.
                stop = next(a for a in unreal.ObjectIterator(unreal.InputAction)
                    if a.get_outer() == pc and a.get_name() == 'InputAction_1')
                self.inject(stop)
                self.advance('pause_settle')
            elif self.stage == 'pause_settle' and age > .4:
                self.paused = self.snapshot(self.factory(world))
                self.advance('paused')
            elif self.stage == 'paused' and age > 1.5:
                current = self.snapshot(self.factory(world))
                self.check('pause_holds_progress', current['work'] == self.paused['work'] and abs(current['display'] - self.paused['display']) < .0001)
                self.event('paused', current)
                self.capture('construction-paused')
                self.cursor(pc, point + unreal.Vector(0, 0, 100))
                self.battle_input('Secondary')
                self.advance('finishing')
            elif self.stage == 'finishing':
                current = self.snapshot(self.factory(world))
                if current['progress'] == 1:
                    self.capture('construction-finish-glow')
                    self.event('finish', current)
                    self.advance('complete')
            elif self.stage == 'complete' and age > 1:
                current = self.snapshot(self.factory(world))
                self.check('completed_proxy_cleanup', current['proxy_count'] == 0 and not current['construction_visible'])
                self.check('original_meshes_restored', all(m['visible'] for m in current['originals']))
                self.check('lights_restored', all(current['lights']))
                self.check('collision_stationary_after_completion', current['collision_transform'] == self.initial['collision_transform'] and current['dock'] == self.initial['dock'])
                self.event('completed', current)
                self.capture('construction-completed')
                self.finish()
        except Exception as error:
            self.report['error'] = str(error)
            self.finish()

    def finish(self):
        unreal.unregister_slate_post_tick_callback(self.handle)
        self.report['finished'] = True
        self.report['success'] = not self.report.get('error') and all(self.report['checks'].values())
        self.report['sample_factory'] = self.factory_name
        self.save()


construction_review = ConstructionSampleReview()
unreal.MCPythonHelper.submit_result(json.dumps({'started': True, 'report': str(OUT / 'runtime-review.json')}))
gc.collect()
