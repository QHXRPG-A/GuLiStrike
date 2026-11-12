"""Prepare the recorded crowd through existing APIs; leave it playable or capture 30s."""
import json,math,sys,time,subprocess
from pathlib import Path
BASE=Path(__file__).resolve().parent
ROOT=BASE.parents[1]
OUT=BASE/('player-preview' if '--preview' in sys.argv else 'after')
OUT.mkdir(exist_ok=True)
sys.path.insert(0,str(ROOT/'Scripts/Performance'))
from capture_circular_avoidance import editor,game,status
def save(name,value):
    (OUT/name).write_text(json.dumps(value,ensure_ascii=False,indent=2),encoding='utf-8')

def wait_for(read,done,seconds=45):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        r=read()
        if done(r):return r
        time.sleep(.5)
    raise RuntimeError(str(r))

def restore_settings(settings):
    code="s=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrikeEditor.GuLiLocalPIESettings'))\n"
    code+=f"s.set_editor_property('bPromptForRole',{settings['prompt']!r},unreal.PropertyAccessChangeNotifyMode.NEVER)\n"
    code+=f"s.set_editor_property('MainRole',unreal.GuLiCommanderRole.{['UNASSIGNED','COMMANDER','GROUND','AIR','OBSERVER'][settings['role']]},unreal.PropertyAccessChangeNotifyMode.NEVER)\n"
    code+="w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
    for c,k in [('t.IdleWhenNotForeground','idle'),('Slate.bAllowThrottling','slate'),('gs.Review.GroundIsolateCombat','isolate'),('gs.Perf.MovementFixtureHold','hold')]:
        code+=f"unreal.SystemLibrary.execute_console_command(w,{(c+' '+str(settings[k]))!r})\n"
    editor(code+"r={'restored':True}")

if sys.argv[1]=='open':
    for action in ('start','summon','run'):
        subprocess.run([sys.executable,'-X','utf8',__file__,action,'--preview'],check=True,cwd=ROOT)
elif sys.argv[1]=='start':
    assert not status().get('active')
    settings=editor("s=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrikeEditor.GuLiLocalPIESettings'))\n"
        "r={'prompt':s.get_editor_property('bPromptForRole'),'role':int(s.get_editor_property('MainRole').value),"
        "'idle':unreal.SystemLibrary.get_console_variable_int_value('t.IdleWhenNotForeground'),"
        "'slate':unreal.SystemLibrary.get_console_variable_int_value('Slate.bAllowThrottling'),"
        "'isolate':unreal.SystemLibrary.get_console_variable_int_value('gs.Review.GroundIsolateCombat'),"
        "'hold':unreal.SystemLibrary.get_console_variable_int_value('gs.Perf.MovementFixtureHold')}\n"
        "s.set_editor_property('bPromptForRole',False,unreal.PropertyAccessChangeNotifyMode.NEVER)\n"
        "s.set_editor_property('MainRole',unreal.GuLiCommanderRole.COMMANDER,unreal.PropertyAccessChangeNotifyMode.NEVER)")
    save('settings-original.json',settings)
    editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
        "unreal.SystemLibrary.execute_console_command(w,'t.IdleWhenNotForeground 0')\n"
        "unreal.SystemLibrary.execute_console_command(w,'Slate.bAllowThrottling 0')\n"
        "unreal.SystemLibrary.execute_console_command(w,'gs.Review.GroundIsolateCombat 1')\n"
        "unreal.SystemLibrary.execute_console_command(w,'gs.Perf.MovementFixtureHold 3')\n"
        "unreal.SystemLibrary.execute_console_command(w,'gs.Commander.QA.StartPIE standalone 1600 900')\nr={'started':True}")
    save('session.json',wait_for(status,lambda s:s.get('world_ready') and s.get('can_issue_orders') and len(s.get('members',[]))==2 and all(m.get('ready') for m in s['members']),90))
    print('PIE ready',flush=True)
elif sys.argv[1]=='summon':
    initial=json.loads((BASE/'scene-recipe.json').read_text())
    old=sorted(m['id'] for m in initial['members'] if m['unit_type']==5)
    d=game("r=json.loads(a.get_move_response_diagnostics())")
    sources=sorted(s['id'] for s in d['soldiers'] if s['alive'] and s['can_simulate'] and s['team']==1 and s['unit_type']==1 and (s['id']-1)%25!=12)[:math.ceil(len(old)/5)]
    request=game(f"selection=json.loads(p.prepare_hotspot_selection({sources!r}))\nassert not selection.get('error'),selection\n"
        "skills=unreal.GameplayStatics.get_player_controller(w,0).player_state.get_component_by_class(unreal.GuLiCommanderSkillComponent)\n"
        "request=skills.request_hybrid_unit_skills(False,unreal.Vector(0,0,0))\nr={'request':unreal.GuidLibrary.conv_guid_to_string(request)}")
    save('summon-request.json',request)
    receipt=wait_for(lambda:game("skills=unreal.GameplayStatics.get_player_controller(w,0).player_state.get_component_by_class(unreal.GuLiCommanderSkillComponent)\n"
        "reply=skills.get_last_reply()\nr={'request':unreal.GuidLibrary.conv_guid_to_string(reply.request_id),'units':[{'succeeded':u.execution.succeeded,'pending':u.execution.pending_commit,'error':u.execution.error} for u in reply.units]}"),
        lambda r:r['request']==request['request'] and len(r['units'])==len(sources) and not any(u['pending'] for u in r['units']))
    save('summon-receipt.json',receipt)
    assert all(u['succeeded'] for u in receipt['units']),receipt
    d=game("r=json.loads(a.get_move_response_diagnostics())")
    small=sorted(s['id'] for s in d['soldiers'] if s['alive'] and s['can_simulate'] and s['unit_type']==5 and s['id']>600)
    assert len(small)>=len(old)
    save('id-map.json',dict(zip(old,small)))
    print('Summons ready',len(old),flush=True)
elif sys.argv[1]=='run':
    initial=json.loads((BASE/'scene-recipe.json').read_text())
    mapping={int(k):v for k,v in json.loads((OUT/'id-map.json').read_text()).items()}
    recipe={'schema':'guli-movement-capture/v1','scenario':'yield-slide-relay','authority_fixture':True,'automatic_fire':False,
        'members':[{'id':mapping.get(m['id'],m['id']),'unit_type':m['unit_type'],'position':m['position']} for m in initial['members']],
        'commands':[{'kind':'move','at':.5,'members':initial['command_members'],'target':initial['target']}]}
    if '--preview' not in sys.argv:
        d=game("r=json.loads(a.get_move_response_diagnostics())")
        used={m['id'] for m in recipe['members']}
        large=next(s['id'] for s in d['soldiers'] if s['alive'] and s['can_simulate'] and s['team']==1 and s['unit_type']==1 and s['id'] not in used)
        small=next(s['id'] for s in d['soldiers'] if s['alive'] and s['can_simulate'] and s['team']==1 and s['unit_type']==5 and s['id'] not in used)
        recipe['members'] += [{'id':small,'unit_type':5,'position':[-35000,62000,900]},
                              {'id':large,'unit_type':1,'position':[-34300,62000,900]}]
        recipe['commands'].append({'kind':'move','at':1.,'members':[small],'target':[-31500,62000,900]})
        save('small-requests-large.json',{'small':small,'large':large,'large_has_move_command':False})
    save('recipe.json',recipe)
    prepared=game(f"r=json.loads(p.prepare_movement_capture({json.dumps(recipe)!r}))")
    save('prepared.json',prepared)
    assert not prepared.get('error'),prepared
    settings=json.loads((OUT/'settings-original.json').read_text())
    if '--preview' in sys.argv:
        save('selection.json',game("pc=unreal.GameplayStatics.get_player_controller(w,0)\npawn=unreal.GameplayStatics.get_player_pawn(w,0)\n"
            "pc.set_view_target_with_blend(pawn,0)\npawn.set_actor_tick_enabled(True)\n"
            "unreal.SystemLibrary.execute_console_command(w,'gs.GM.Commander.Camera.Height 85')\n"
            "pawn.jump_to_world_location(unreal.Vector(11000,45400,902))\n"
            "r=json.loads(p.prepare_hotspot_selection([245]))"))
        restore_settings(settings)
        save('player-ready.json',status())
        print('Player preview ready: pioneer 245 selected.',flush=True)
    else:
        assert game("r={'started':p.start_movement_capture()}")['started']
        print('30-second focused capture started',flush=True)
        time.sleep(31)
        capture=game("r=json.loads(p.get_movement_capture_json())")
        save('capture.json',capture)
        save('diagnostics.json',game("r=json.loads(a.get_move_response_diagnostics())"))
        print([m for m in capture['members'] if m['id']==245],flush=True)
        restore_settings(settings)
