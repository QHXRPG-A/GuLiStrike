"""Scoped, asynchronous PIE regression requested by the approved rogue upgrade plan.

Exercises real F4 Enhanced Input and Blueprint phase transitions via the editor-only QA adapter.
Does not inspect screenshots or change the saved map. Writes all observations before stopping PIE.
"""
import json
import time
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / 'Artifacts/RogueCards/Upgrade/runtime-regression.json'
QA = unreal.GuLiRogueCardQALibrary
STATE = {'phase': 'ready', 'since': time.monotonic(), 'start': time.monotonic(), 'events': [], 'samples': [],
         'card': 0, 'success': False, 'interaction_injection': 'F4 keys; Blueprint ActivateHit for card clicks',
         'visual_review': 'user_pending'}
HANDLE = None


def log(name, snap=None):
    STATE['events'].append({'name': name, 'time': round(time.monotonic()-STATE['start'], 3), 'snapshot': snap})


def step(phase):
    STATE['phase'] = phase; STATE['since'] = time.monotonic()


def finish(error=None):
    global HANDLE
    if error:
        STATE['error'] = error
    STATE['success'] = error is None
    if HANDLE is not None:
        unreal.unregister_slate_post_tick_callback(HANDLE); HANDLE = None
    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(STATE, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()


def clean(snapshot):
    return not any(snapshot[k] for k in ['open', 'closing', 'submitting', 'overlay', 'capture', 'director', 'target', 'ticking']) and snapshot['blur'] == 0 and snapshot['fade'] == 0


def tick(dt):
    try:
        now = time.monotonic()
        if now-STATE['start'] > 100:
            raise RuntimeError('PIE regression timed out in '+STATE['phase'])
        world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()
        if not world:
            return
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        if not pc:
            return
        snap = json.loads(QA.snapshot(pc))
        elapsed = now-STATE['since']; phase = STATE['phase']
        if phase == 'ready':
            if not snap.get('ready'):
                return
            assert snap['f4_debug_count'] == 0
            STATE['initial_viewmode'] = snap['viewmode']; log('ready', snap)
            QA.action(pc, 'F4Down'); step('f4')
        elif phase == 'f4' and elapsed > .15:
            QA.action(pc, 'F4Up'); log('F4_opened', snap)
            assert snap['open'] and snap['viewmode'] == STATE['initial_viewmode'] and not snap['detail_lighting']
            step('choose')
        elif phase == 'choose' and snap.get('phase') == 1:
            STATE['session'] = snap['session']; QA.action(pc, 'Open')
            assert json.loads(QA.snapshot(pc))['session'] == STATE['session']
            QA.action(pc, 'Hit', STATE['card']); QA.action(pc, 'Hit', (STATE['card']+1)%3)
            check = json.loads(QA.snapshot(pc)); log('first_click_and_fast_second', check)
            assert check['phase'] == 2 and check['selected'] == STATE['card']
            step('flip')
        elif phase == 'flip' and snap.get('phase') == 3:
            QA.action(pc, 'Hit', (STATE['card']+1)%3)
            assert json.loads(QA.snapshot(pc))['phase'] == 3
            QA.action(pc, 'Hit', STATE['card']); log('confirmed', snap); step('exit')
        elif phase == 'exit':
            STATE['samples'].append({'time': round(now-STATE['start'], 3), **snap})
            if not snap['open']:
                assert clean(snap) and snap['viewmode'] == STATE['initial_viewmode']
                log('cleaned_before_upgrade', snap); step('vfx')
        elif phase == 'vfx':
            STATE['samples'].append({'time': round(now-STATE['start'], 3), **snap})
            if elapsed > 1.6:
                assert snap['upgrade_components'] == 0 and snap['upgrade_active'] == 0
                session, valid = unreal.GuidLibrary.parse_string_to_guid(STATE['session'])
                assert valid
                QA.replay(pc, session, ['01.01', '03.01', '02.01'][STATE['card']], False)
                QA.replay(pc, session, '', True); QA.action(pc, 'CleanupTwice')
                assert clean(json.loads(QA.snapshot(pc)))
                step('replay')
        elif phase == 'replay' and elapsed > .25:
            assert clean(snap) and snap['upgrade_active'] == 0
            log('duplicate_confirmation_and_cleanup', snap)
            STATE['card'] += 1
            if STATE['card'] < 3:
                QA.action(pc, 'Open'); step('choose')
            else:
                QA.action(pc, 'Open'); step('cancel')
        elif phase == 'cancel' and elapsed > .25:
            QA.action(pc, 'Cancel'); step('cancelled')
        elif phase == 'cancelled' and elapsed > .4:
            assert clean(snap); log('cancelled', snap)
            QA.action(pc, 'Open'); QA.action(pc, 'CleanupTwice'); step('interrupted')
        elif phase == 'interrupted' and elapsed > .4:
            assert clean(snap) and snap['upgrade_active'] == 0; log('interrupted_initialization', snap)
            finish()
    except Exception:
        finish(traceback.format_exc())


assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
assert unreal.GuLiComponentSkillQALibrary.start_pie(0, 1)
HANDLE = unreal.register_slate_post_tick_callback(tick)
print(json.dumps({'started': True, 'report': str(OUT)}))
