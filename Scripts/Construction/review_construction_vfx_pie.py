"""Capture real placement and construction in PIE without retaining world references."""
import gc
import json
import time
import traceback
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/outputs/construction-vfx')
OUT.mkdir(parents=True, exist_ok=True)


def local_controller():
    return next(pc for pc in unreal.ObjectIterator(unreal.GuLiCommanderPlayerController)
                if 'Default__' not in pc.get_name() and pc.get_viewport_size()[0]>0)


class ConstructionVfxReview:
    def __init__(self):
        self.stage = 'funding'
        self.started = self.changed = time.monotonic()
        self.next_tick = 0.0
        self.factory_name = ''
        self.report = {'events': [], 'images': [], 'finished': False}
        self.handle = unreal.register_slate_post_tick_callback(self.tick)

    def save(self):
        (OUT/'runtime-vfx-review.json').write_text(json.dumps(self.report,ensure_ascii=False,indent=2),encoding='utf-8')

    def event(self, name, value):
        self.report['events'].append({'event':name,'value':value})
        self.save()

    def advance(self, stage):
        self.stage, self.changed = stage, time.monotonic()
        self.event('stage', stage)

    def inject(self, action):
        for subsystem in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem):
            if not subsystem.get_name().startswith('Default__'):
                subsystem.inject_input_vector_for_action(action,unreal.Vector(1,0,0),[],[])

    def input(self, name):
        self.inject(unreal.load_asset('/Game/GuLiStrike/GroundMech/Input/IA_Battle_'+name))

    def cursor(self, pc, point):
        screen = unreal.GameplayStatics.project_world_to_screen(pc,point)
        assert screen
        pc.set_mouse_location(round(screen.x),round(screen.y))

    def snapshot(self, world, factory):
        life = factory.get_component_by_class(unreal.GuLiBuildingLifecycleComponent)
        visual = factory.get_component_by_class(unreal.GuLiBuildingConstructionVisualComponent)
        beams=[]
        for builder in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.GuLiConstructionVehiclePawn):
            active=[]
            for child in builder.get_all_child_actors(True):
                for niagara in child.get_components_by_class(unreal.NiagaraComponent):
                    if 'MiningLaser' in niagara.get_name():
                        active.append({'name':niagara.get_name(),'active':niagara.is_active(),'visible':niagara.is_visible(),
                                       'system':niagara.get_asset().get_name() if niagara.get_asset() else None})
            if any(b['active'] for b in active):
                beams.append({'builder':builder.get_name(),'position':list(builder.get_actor_location().to_tuple()),'beams':active})
        top=[{'name':n.get_name(),'active':n.is_active(),'system':n.get_asset().get_name()}
             for n in unreal.ObjectIterator(unreal.NiagaraComponent)
             if n.get_world()==world and n.get_asset()
             and n.get_asset().get_name() in ('NS_ConstructionTopLoop','NS_ConstructionComplete')
             and (n.get_world_location()-factory.get_actor_location()).length()<10]
        return {'phase':str(life.get_state().phase),'work':life.get_state().work_done,
                'progress':life.get_construction_progress(),'display':visual.get_displayed_progress(),
                'active_builders':life.get_state().has_active_builders,
                'roof':[list(p.to_tuple()) for p in visual.get_roof_contour()],
                'beams':beams,'effects':top,'position':list(factory.get_actor_location().to_tuple())}

    def capture(self, world, factory, label):
        center=factory.get_actor_location()
        camera=unreal.GuLiTeleportQALibrary.create_capture(world)
        try:
            capture=camera.capture_component2d
            capture.set_editor_property('capture_every_frame',False)
            capture.set_editor_property('capture_on_movement',False)
            capture.set_editor_property('capture_source',unreal.SceneCaptureSource.SCS_FINAL_COLOR_LDR)
            capture.set_editor_property('fov_angle',50)
            target=unreal.RenderingLibrary.create_render_target2d(world,1400,1000,
                unreal.TextureRenderTargetFormat.RTF_RGBA8,unreal.LinearColor(0,0,0,1),False,False)
            capture.set_editor_property('texture_target',target)
            pos=center+unreal.Vector(1800,-2000,2000)
            camera.set_actor_location_and_rotation(pos,unreal.MathLibrary.find_look_at_rotation(pos,center+unreal.Vector(0,0,180)),False,True)
            capture.capture_scene()
            unreal.RenderingLibrary.export_render_target(world,target,str(OUT),label+'.png')
            self.report['images'].append(str(OUT/(label+'.png')))
            self.event(label,self.snapshot(world,factory))
        finally:
            camera.destroy_actor()

    def tick(self, dt):
        now=time.monotonic()
        if now < self.next_tick: return
        self.next_tick=now+.06
        try:
            pc=local_controller()
            world=pc.get_world()
            assert now-self.started<240,'Review timed out'
            age=now-self.changed
            point=unreal.Vector(*globals().get('CONSTRUCTION_REVIEW_POINT',(7000,76500,-1760)))
            factory=next((a for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.GuLiResourceFactoryActor) if a.get_name()==self.factory_name),None)
            if self.stage in ['select','aim','place']:
                pc.get_controlled_pawn().jump_to_world_location(point)
                self.cursor(pc,point)
            if self.stage=='funding':
                unreal.GuLiConstructionReviewLibrary.fund_review(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world())
                funds=pc.player_state.get_resource_inventory()
                if funds.blue>=60 and funds.red>=20:
                    authority=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
                    unreal.GameplayStatics.set_global_time_dilation(authority,.2)
                    if not pc.get_building_placement_component().is_build_mode_active(): self.input('Build')
                    self.advance('select')
            elif self.stage=='select' and age>.3:
                assert pc.get_building_placement_component().is_build_mode_active()
                self.input('Slot6')
                self.advance('aim')
            elif self.stage=='aim' and age>.4:
                previews=unreal.GameplayStatics.get_all_actors_of_class(world,unreal.GuLiBuildingPlacementPreview)
                if not previews:
                    assert age<25,'Preview never appeared'
                    return
                preview=previews[0]
                tint=preview.get_components_by_class(unreal.MeshComponent)[0].get_material(0).get_vector_parameter_value('TintColor')
                self.report['preview']={'position':list(preview.get_actor_location().to_tuple()),'green':tint.g>.9}
                if tint.g>.9:
                    self.input('Primary')
                    self.advance('place')
                else:
                    assert age<25,'Preview did not become valid'
            elif self.stage=='place':
                candidates=[a for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.GuLiResourceFactoryActor)
                    if (a.get_actor_location()-point).length()<1500 and a.get_component_by_class(unreal.GuLiBuildingLifecycleComponent).get_state().phase==unreal.GuLiBuildingPhase.UNDER_CONSTRUCTION]
                if candidates:
                    factory=candidates[0]
                    self.factory_name=factory.get_name()
                    if pc.get_building_placement_component().is_build_mode_active(): self.input('Cancel')
                    self.capture(world,factory,'pie-01-unbuilt')
                    self.advance('building')
                else:
                    assert age<5,'Placement did not spawn factory'
            elif self.stage=='building':
                life=factory.get_component_by_class(unreal.GuLiBuildingLifecycleComponent)
                if life.get_construction_progress()>.35:
                    self.capture(world,factory,'pie-02-building')
                    self.event('select_to_pause',unreal.GuLiComponentSkillQALibrary.select_radius(pc,factory.get_actor_location(),401,False))
                    self.advance('stop')
            elif self.stage=='stop' and age>.2:
                action=next(a for a in unreal.ObjectIterator(unreal.InputAction) if a.get_outer()==pc and a.get_name()=='InputAction_1')
                self.inject(action)
                self.advance('settle')
            elif self.stage=='settle' and age>.5:
                self.paused_work=factory.get_component_by_class(unreal.GuLiBuildingLifecycleComponent).get_state().work_done
                self.advance('paused')
            elif self.stage=='paused' and age>2:
                state=self.snapshot(world,factory)
                self.report['pause_holds_work']=state['work']==self.paused_work
                assert self.report['pause_holds_work'],'Stopped workers continued construction'
                self.capture(world,factory,'pie-03-paused')
                if globals().get('CONSTRUCTION_HOLD_PAUSED',False):
                    self.advance('held')
                    return
                self.cursor(pc,factory.get_actor_location()+unreal.Vector(0,0,100))
                self.input('Secondary')
                self.advance('resume')
            elif self.stage=='resume':
                state=factory.get_component_by_class(unreal.GuLiBuildingLifecycleComponent).get_state()
                if state.phase==unreal.GuLiBuildingPhase.COMPLETED:
                    self.capture(world,factory,'pie-04-completion-start')
                    self.completion_time=unreal.GameplayStatics.get_time_seconds(world)
                    self.completion_frame=0
                    self.advance('burst')
                elif age>1 and not self.report.get('resume_shot') and state.has_active_builders:
                    self.capture(world,factory,'pie-03b-resumed')
                    self.report['resume_shot']=True
            elif self.stage=='burst':
                effect_age=unreal.GameplayStatics.get_time_seconds(world)-self.completion_time
                if effect_age>0.1+self.completion_frame*.15:
                    self.capture(world,factory,'pie-complete-'+str(self.completion_frame).zfill(2))
                    self.completion_frame+=1
                if effect_age>2.8: self.advance('complete')
            elif self.stage=='complete' and age>2:
                self.capture(world,factory,'pie-06-completed')
                self.finish()
        except Exception:
            self.report['error']=traceback.format_exc()
            self.finish()

    def finish(self):
        unreal.unregister_slate_post_tick_callback(self.handle)
        self.report['finished']=True
        self.report['factory']=self.factory_name
        self.report['success']=not self.report.get('error')
        self.save()


construction_vfx_review=ConstructionVfxReview()
unreal.MCPythonHelper.submit_result(json.dumps({'started':True,'output':str(OUT)}))
gc.collect()
