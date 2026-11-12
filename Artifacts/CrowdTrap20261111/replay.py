"""Focused replay of the captured crowd geometry using existing developer APIs."""
import json, sys, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(__file__).resolve().parent
OUT = BASE / sys.argv[2] if len(sys.argv)>2 else BASE
OUT.mkdir(exist_ok=True)
sys.path.insert(0, str(ROOT / 'Scripts/Performance'))
from capture_circular_avoidance import editor, game, status

def save(name, value):
    (OUT / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding='utf-8')

def wait_for(read, done, seconds=45):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        value = read()
        if done(value):
            return value
        time.sleep(.5)
    raise RuntimeError(str(value))

stage = sys.argv[1]
if stage == 'open':
    import subprocess
    for action in ('start','summon','seed','preview'):
        subprocess.run([sys.executable,'-X','utf8',__file__,action,'player-preview'],check=True,cwd=ROOT)
elif stage == 'start':
    assert not status().get('active')
    original = editor("s=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrikeEditor.GuLiLocalPIESettings'))\n"
        "r={'prompt':s.get_editor_property('bPromptForRole'),'role':int(s.get_editor_property('MainRole').value),"
        "'idle':unreal.SystemLibrary.get_console_variable_int_value('t.IdleWhenNotForeground'),"
        "'slate':unreal.SystemLibrary.get_console_variable_int_value('Slate.bAllowThrottling'),"
        "'isolate_combat':unreal.SystemLibrary.get_console_variable_int_value('gs.Review.GroundIsolateCombat'),"
        "'fixture_hold':unreal.SystemLibrary.get_console_variable_int_value('gs.Perf.MovementFixtureHold')}\n"
        "s.set_editor_property('bPromptForRole',False,unreal.PropertyAccessChangeNotifyMode.NEVER)\n"
        "s.set_editor_property('MainRole',unreal.GuLiCommanderRole.COMMANDER,unreal.PropertyAccessChangeNotifyMode.NEVER)")
    save('settings-original.json', original)
    editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
        "unreal.SystemLibrary.execute_console_command(w,'t.IdleWhenNotForeground 0')\n"
        "unreal.SystemLibrary.execute_console_command(w,'Slate.bAllowThrottling 0')\n"
        "unreal.SystemLibrary.execute_console_command(w,'gs.Review.GroundIsolateCombat 1')\n"
        "unreal.SystemLibrary.execute_console_command(w,'gs.Perf.MovementFixtureHold 3')\n"
        "unreal.SystemLibrary.execute_console_command(w,'gs.Commander.QA.StartPIE standalone 1600 900')\nr={'started':True}")
    ready = wait_for(status, lambda s:s.get('world_ready') and s.get('can_issue_orders') and len(s.get('members',[]))==2 and all(m.get('ready') for m in s['members']), 90)
    save('session.json', ready)
    print('PIE ready',flush=True)
elif stage == 'summon':
    d = game("r=json.loads(a.get_move_response_diagnostics())")
    ids = sorted(s['id'] for s in d['soldiers'] if s['alive'] and s['can_simulate'] and s['team']==1 and s['unit_type']==1 and (s['id']-1)%25!=12 and s['id'] not in (108,195))[:18]
    request = game(f"selection=json.loads(p.prepare_hotspot_selection({ids!r}))\nassert not selection.get('error'),selection\n"
        "skills=unreal.GameplayStatics.get_player_controller(w,0).player_state.get_component_by_class(unreal.GuLiCommanderSkillComponent)\n"
        "request=skills.request_hybrid_unit_skills(False,unreal.Vector(0,0,0))\n"
        "r={'request':unreal.GuidLibrary.conv_guid_to_string(request),'selection':selection}")
    save('summon-request.json', request)
    receipt = wait_for(lambda:game("skills=unreal.GameplayStatics.get_player_controller(w,0).player_state.get_component_by_class(unreal.GuLiCommanderSkillComponent)\n"
        "reply=skills.get_last_reply()\n"
        "r={'request':unreal.GuidLibrary.conv_guid_to_string(reply.request_id),'error':reply.error,'units':[{'succeeded':u.execution.succeeded,'pending':u.execution.pending_commit,'error':u.execution.error} for u in reply.units]}"),
        lambda r:r['request']==request['request'] and len(r['units'])==len(ids) and not any(u['pending'] for u in r['units']))
    save('summon-receipt.json', receipt)
    assert all(u['succeeded'] for u in receipt['units']),receipt
    print('Summons ready',flush=True)
elif stage == 'seed':
    d = game("r=json.loads(a.get_move_response_diagnostics())")
    small = sorted(s['id'] for s in d['soldiers'] if s['alive'] and s['can_simulate'] and s['team']==1 and s['unit_type']==5 and s['id']>600)
    assert len(small)>=85,len(small)
    original=json.loads((BASE/'initial.json').read_text())
    old=sorted((s for s in original['soldiers'] if s['alive'] and s['unit_type']==5 and s['command'] in (49,54)),key=lambda s:s['id'])
    assert len(old)==85
    mapping={s['id']:n for s,n in zip(old,small)}
    save('id-map.json',mapping)
    seed_position=next(s['position'] for s in d['soldiers'] if s['id']==small[0])
    game("unreal.SystemLibrary.execute_console_command(w,'guli.Commander.MoveLatencyDiagnostics 1')\n"
        "sync=unreal.GameplayStatics.get_player_controller(w,0).get_component_by_class(unreal.GuLiCommanderNetSyncComponent)\n"
        f"r={{'submitted':sync.submit_move_response_diagnostic('same-type',940011,{small[0]},unreal.Vector(*{seed_position!r}))}}")
    time.sleep(.5)
    save('selection.json',game("sync=unreal.GameplayStatics.get_player_controller(w,0).get_component_by_class(unreal.GuLiCommanderNetSyncComponent)\nr=json.loads(sync.get_move_response_diagnostics())"))
    print(game("sync=unreal.GameplayStatics.get_player_controller(w,0).get_component_by_class(unreal.GuLiCommanderNetSyncComponent)\n"
        "r={'submitted':sync.submit_move_response_diagnostic('move',940012,0,unreal.Vector(-35000,68000,1000))}"),flush=True)
    time.sleep(1)
    game("sync=unreal.GameplayStatics.get_player_controller(w,0).get_component_by_class(unreal.GuLiCommanderNetSyncComponent)\n"
        "r={'submitted':sync.submit_move_response_diagnostic('stop',940013,0,unreal.Vector(0,0,0))}")
    time.sleep(.3)
    seed = game("r=json.loads(a.get_move_response_diagnostics())")
    save('seeded.json',seed)
    rows={s['id']:s for s in seed['soldiers']}
    commands={rows[n]['command'] for n in mapping.values()}
    assert len(commands)==1 and 0 not in commands,commands
    print('Wall command retained',commands,flush=True)
elif stage in ('capture', 'preview'):
    original=json.loads((BASE/'initial.json').read_text())
    mapping={int(k):v for k,v in json.loads((OUT/'id-map.json').read_text()).items()}
    original_units={s['id']:s for s in original['soldiers']}
    ids=[108,195]+list(mapping)
    members=[{'id':mapping.get(i,i),'unit_type':original_units[i]['unit_type'],'position':original_units[i]['position']} for i in ids]
    group=[108]+[mapping[i] for i in mapping if original_units[i]['command']==54]
    recipe={'schema':'guli-movement-capture/v1','scenario':'live-crowd-trap','map':'/Game/Maps/LVL_CommanderMassPrototype',
        'authority_fixture':True,'automatic_fire':False,'members':members,
        'commands':[{'kind':'move','at':.5,'members':group,'target':original_units[108]['end']},
                    {'kind':'move','at':.8,'members':[195],'target':original_units[195]['end']}]}
    save('recipe.json',recipe)
    prepared=game(f"r=json.loads(p.prepare_movement_capture({json.dumps(recipe)!r}))")
    save('prepared.json',prepared)
    assert not prepared.get('error'),prepared
    if stage=='preview':
        settings=json.loads((OUT/'settings-original.json').read_text())
        ready=game("pc=unreal.GameplayStatics.get_player_controller(w,0)\n"
            "pawn=unreal.GameplayStatics.get_player_pawn(w,0)\npc.set_view_target_with_blend(pawn,0)\npawn.set_actor_tick_enabled(True)\n"
            "unreal.SystemLibrary.execute_console_command(w,'gs.GM.Commander.Camera.Height 85')\n"
            "pawn.jump_to_world_location(unreal.Vector(-11200,70500,1075))\n"
            "unreal.SystemLibrary.execute_console_command(w,'guli.Commander.MoveLatencyDiagnostics 0')\n"
            f"unreal.SystemLibrary.execute_console_command(w,'gs.Review.GroundIsolateCombat {settings['isolate_combat']}')\n"
            f"unreal.SystemLibrary.execute_console_command(w,'gs.Perf.MovementFixtureHold {settings['fixture_hold']}')\n"
            f"r=json.loads(p.prepare_hotspot_selection([108,195]))")
        save('player-selection.json',ready)
        editor("s=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrikeEditor.GuLiLocalPIESettings'))\n"
            f"s.set_editor_property('bPromptForRole',{settings['prompt']!r},unreal.PropertyAccessChangeNotifyMode.NEVER)\n"
            f"s.set_editor_property('MainRole',unreal.GuLiCommanderRole.{['UNASSIGNED','COMMANDER','GROUND','AIR','OBSERVER'][settings['role']]},unreal.PropertyAccessChangeNotifyMode.NEVER)\n"
            "w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
            f"unreal.SystemLibrary.execute_console_command(w,'t.IdleWhenNotForeground {settings['idle']}')\n"
            f"unreal.SystemLibrary.execute_console_command(w,'Slate.bAllowThrottling {settings['slate']}')\nr={{'restored':True}}")
        save('player-ready.json',status())
        print('Player preview ready: 108 and 195 selected; right-click east of the crowd.',flush=True)
        sys.exit(0)
    assert game("r={'started':p.start_movement_capture()}")['started']
    print('30-second focused replay started',flush=True)
    time.sleep(31)
    finished=game("r=json.loads(p.get_movement_capture_json())")
    save('capture-after.json',finished)
    save('after.json',game("r=json.loads(a.get_move_response_diagnostics())"))
    print({k:finished.get(k) for k in ('finished','error','seconds')},flush=True)
