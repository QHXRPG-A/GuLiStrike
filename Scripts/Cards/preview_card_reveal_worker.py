"""Isolated visual/runtime verification for the explicitly requested demo flow.

Requires -CardRevealPreview, runs only its own PIE session, saves no assets.
Uses the real Blueprint state functions; OS pointer input remains untouched.
"""
import json
import time
import traceback
from pathlib import Path
import unreal

LIVE=globals().get('CARD_REVEAL_LIVE',False)
assert LIVE or '-CardRevealPreview' in unreal.SystemLibrary.get_command_line(), 'Explicit demo verification required'
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
ROOT='/Game/GuLiStrike/Cards/RevealDemo'
OUT=Path('D:/UE5.7/test1/ArtSource/UI/CardRevealDemo')
REPORT={'checks':[], 'screenshots':[], 'errors':[], 'input_boundary':'State helper plus Enhanced Input injection and real viewport mouse coordinates in live mode; OS focus switching is not automated.'}
STATE={'start':time.monotonic(), 'ready':False, 'job':0, 'wait':0., 'capture':None, 'world':None}
settings=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.LevelEditorPlaySettings'))
settings.set_editor_property('NewWindowWidth',1280)
settings.set_editor_property('NewWindowHeight',720)
settings.set_editor_property('CenterNewWindow',True)
settings.set_editor_property('PlayNumberOfClients',1)
settings.set_editor_property('RunUnderOneProcess',True)
if not LIVE:
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).load_level(ROOT+'/Maps/LVL_CardRevealDemo')
    assert unreal.WidgetService.start_pie()
else:
    assert unreal.WidgetService.is_pie_running(), 'Open the dedicated map in PIE first'


def check(name, condition, detail=None):
    REPORT['checks'].append({'name':name,'passed':bool(condition),'detail':detail})
    if not condition:raise RuntimeError(name+': '+str(detail))


def prop(name):return DIRECTOR.get_editor_property(name)


def call(name,*args):return DIRECTOR.call_method(name,args)


def cards():
    return list(unreal.GameplayStatics.get_all_actors_of_class(STATE['world'], CARD_CLASS))


def component(actor,name):
    return next(c for c in actor.get_components_by_class(unreal.SceneComponent) if c.get_name()==name)


def step(dt):
    call('TickPresentation',dt)


def snapshot(name):
    values={'phase':prop('Phase'),'phase_time':prop('PhaseTime'),'selected':prop('SelectedIndex'),
            'cards':[{'location':list(c.get_actor_location().to_tuple()),'scale':list(c.get_actor_scale3d().to_tuple()),
                      'flip':str(component(c,'FlipPivot').get_editor_property('relative_rotation')),
                      'hover':str(component(c,'HoverPivot').get_editor_property('relative_rotation'))} for c in cards()]}
    REPORT.setdefault('frames',{})[name]=values


def shot(name):
    snapshot(name)
    path=OUT/(name+'.png')
    width,height=REPORT['viewport_size']
    command=('Shot showui filename="'+str(path)+'" -nosuffix') if LIVE else (f'HighResShot {width}x{height} filename="'+str(path)+'"')
    unreal.SystemLibrary.execute_console_command(STATE['world'],command,prop('Controller'))
    STATE['capture']=str(path)
    STATE['wait']=1.2


def reset():
    # Controlled fixture reset for recording the first entry. Later resets use the actual completed state.
    call('SetPhase',6)
    call('StartPresentation')
    step(0)


def enter(dt,count):
    step(dt)
    snapshot('entry_'+str(count))
    cs=[prop('Card'+str(i)) for i in range(3)]
    landed=sum(abs(c.get_actor_location().y)<.001 for c in cs)
    check('sequential entry '+str(count),landed==count,landed)
    if count==3:STATE['wait']=2.


def hover():
    c=prop('Card0');before=c.get_actor_location()
    c.call_method('ApplyFrame',(before.x,prop('CardScale'),1.,prop('FarDistance'),0.,.75,.8,
                  prop('MaximumTilt'),prop('HoverInterpSpeed'),.2,0.,prop('FlashIntensity'),True))
    r=component(c,'HoverPivot').get_editor_property('relative_rotation')
    check('hover keeps center fixed',(before-c.get_actor_location()).length()<.001)
    check('bottom-right pushes inward',r.yaw<0 and r.roll>0,str(r))
    shot('hover-bottom-right')


def select(index):
    call('ActivateHit',index)
    check('select '+str(index),prop('Phase')==2 and prop('SelectedIndex')==index)
    call('ActivateHit',index)
    call('ActivateHit',(index+1)%3)
    check('ignore clicks during flip',prop('Phase')==2 and prop('SelectedIndex')==index)


def back():
    step(.225)
    check('flip completed',prop('Phase')==3,prop('Phase'))
    call('ActivateHit',1)
    call('ActivateHit',-1)
    check('other cards and blank cannot confirm',prop('Phase')==3 and prop('SelectedIndex')==0)
    shot('selected-card-back')


def finished():
    step(.25)
    check('complete after exit',prop('Phase')==6,prop('Phase'))
    check('no remaining card actors',len(cards())==0,len(cards()))
    step(1)
    check('completed state remains stable',prop('Phase')==6 and len(cards())==0)
    shot('presentation-complete')


def replay_cycle(index):
    call('StartPresentation');step(1.5)
    check('replay restores 3 front cards',len(cards())==3 and prop('SelectedIndex')==-1 and prop('Phase')==1)
    select(index);step(.45)
    check('await fresh confirmation '+str(index),prop('Phase')==3)
    call('ActivateHit',(index+1)%3)
    check('selection locked '+str(index),prop('Phase')==3 and prop('SelectedIndex')==index)
    call('ActivateHit',index)
    check('second press starts flash '+str(index),prop('Phase')==4)
    step(.25);step(.5)
    check('cleanup cycle '+str(index),prop('Phase')==6 and len(cards())==0)


def pointer(x,y):
    pc=prop('Controller');w,h=pc.get_viewport_size()
    halfw=prop('CardScale')*16.0875/prop('ViewWidth')
    halfh=prop('CardScale')*24.3085/prop('ViewHeight')
    pc.set_mouse_location(round(w*(.5+x*halfw)),round(h*(.5+y*halfh)))
    STATE['wait']=.15


def verify_pointer(x,y):
    step(.2)
    c=prop('Card1');r=component(c,'HoverPivot').get_editor_property('relative_rotation')
    check('stable center at '+str((x,y)),c.get_actor_location().length()<.001)
    check('viewport hit at '+str((x,y)),prop('HoveredIndex')==1,prop('HoveredIndex'))
    check('press direction at '+str((x,y)),abs(r.yaw+x*prop('MaximumTilt'))<.12 and abs(r.roll-y*prop('MaximumTilt'))<.12,str(r))


def replay_button():
    prop('PresentationHUD').call_method('RequestReplay')
    step(1.5)
    check('HUD replay dispatcher resets presentation',prop('Phase')==1 and prop('SelectedIndex')==-1 and len(cards())==3)


def set_hold(value):
    STATE['hold']=value
    STATE['wait']=.2


def check_recenter():
    step(.2)
    r=component(prop('Card1'),'HoverPivot').get_editor_property('relative_rotation')
    check('outside stable hit region recenters',prop('HoveredIndex')==-1 and abs(r.yaw)<.01 and abs(r.roll)<.01,str(r))


JOBS=[reset,lambda:enter(.5,1),lambda:enter(.5,2),lambda:enter(.5,3),
      lambda:shot('layout-viewport'),hover,lambda:select(0),lambda:(step(.225),shot('flip-midpoint')),
      back,lambda:(call('ActivateHit',0),step(0),shot('flash-start')),
      lambda:(step(.055),shot('flash-peak')),lambda:(step(.165),shot('flash-fade')),
      lambda:(step(.031),check('flash then exit',prop('Phase')==5)),
      lambda:(step(.25),shot('exit-midpoint')),finished,lambda:replay_cycle(1),lambda:replay_cycle(2)]

if LIVE:
    JOBS.append(replay_button)
    for x,y in [(0,0),(-.9,0),(.9,0),(0,-.9),(0,.9),(-.9,-.9),(.9,-.9),(-.9,.9),(.9,.9)]:
        JOBS.extend([lambda x=x,y=y:pointer(x,y),lambda x=x,y=y:verify_pointer(x,y)])
    JOBS.extend([lambda:pointer(0,1.2),check_recenter,lambda:pointer(0,0),lambda:step(.1),
        lambda:set_hold(True),lambda:check('Enhanced Input Started selects center',prop('Phase')==2 and prop('SelectedIndex')==1),
        lambda:(step(.45),STATE.update(wait=.5)),
        lambda:check('held press does not confirm after flip',prop('Phase')==3),
        lambda:set_hold(False),lambda:set_hold(True),
        lambda:check('fresh Enhanced Input press confirms',prop('Phase')==4),lambda:set_hold(False),
        lambda:(step(.25),step(.5)),lambda:check('input-driven cycle cleaned up',prop('Phase')==6 and len(cards())==0)])


def finish():
    REPORT['success']=not REPORT['errors'] and all(c['passed'] for c in REPORT['checks'])
    (OUT/('runtime-live.json' if LIVE else 'runtime-preview.json')).write_text(json.dumps(REPORT,ensure_ascii=False,indent=2),encoding='utf-8')
    unreal.unregister_slate_post_tick_callback(HANDLE)
    if LIVE:
        STATE['hold']=False
        call('SetPhase',6)
        call('StartPresentation')
        DIRECTOR.set_actor_tick_enabled(True)
    else:
        unreal.WidgetService.stop_pie()
        unreal.SystemLibrary.quit_editor()


def tick(delta):
    global DIRECTOR,CARD_CLASS
    try:
        if time.monotonic()-STATE['start']>180:raise RuntimeError('Preview exceeded 180 seconds')
        if not STATE['ready']:
            w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
            if not w:return
            ds=unreal.GameplayStatics.get_all_actors_of_class(w,unreal.load_class(None,ROOT+'/Blueprints/BP_CardRevealDirector.BP_CardRevealDirector_C'))
            if not ds:return
            DIRECTOR=ds[0]
            if DIRECTOR.get_editor_property('Phase')==6:return
            DIRECTOR.set_actor_tick_enabled(False)
            CARD_CLASS=unreal.load_class(None,ROOT+'/Blueprints/BP_ParallaxRevealCard.BP_ParallaxRevealCard_C')
            STATE.update(ready=True,world=w,wait=8.)
            REPORT['viewport_size']=list(DIRECTOR.get_editor_property('Controller').get_viewport_size())
            cam=DIRECTOR.get_component_by_class(unreal.CameraComponent)
            pp=cam.post_process_settings
            REPORT['camera']={'location':list(cam.get_world_location().to_tuple()),'exposure':str(pp.auto_exposure_method),'override':pp.override_auto_exposure_method,'blend':cam.post_process_blend_weight}
            return
        if LIVE and STATE.get('hold'):
            subs=[s for s in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem) if not s.get_name().startswith('Default__')]
            assert len(subs)==1, 'Only the dedicated single-player PIE session may be tested'
            sub=subs[0]
            sub.inject_input_vector_for_action(unreal.load_asset(ROOT+'/Input/IA_CardRevealClick'),unreal.Vector(1,0,0),[],[])
        STATE['wait']-=delta
        if STATE['wait']>0:return
        if STATE['capture']:
            check('screenshot saved',Path(STATE['capture']).is_file(),STATE['capture'])
            REPORT['screenshots'].append(STATE['capture']);STATE['capture']=None
        if STATE['job']>=len(JOBS):finish();return
        JOBS[STATE['job']]()
        STATE['job']+=1
        if STATE['wait']==0:STATE['wait']=.05
    except Exception:
        REPORT['errors'].append(traceback.format_exc())
        finish()


HANDLE=unreal.register_slate_post_tick_callback(tick)
print(json.dumps({'preview_started':True}))
