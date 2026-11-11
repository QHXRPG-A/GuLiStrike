"""Explicit, disposable PIE movement baseline preparation and 30-second capture.

Eight workloads share a complete frozen population and semantic scene snapshot.
Each invocation records one window; the report qualifies the full repeated suite.
Only native instrumentation runs inside the window; never reads binary UE assets.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "Scripts"))
from commander_editor_python import call_editor
from circular_avoidance_scenarios import make_extended_recipe


def editor(code: str) -> dict:
    result = call_editor("import unreal,json\n" + code +
                         "\nunreal.MCPythonHelper.submit_result(json.dumps(r))", 30)
    value = result.get("result")
    if not result.get("success") or not isinstance(value, dict) or value.get("success") is False:
        raise RuntimeError(json.dumps(result, ensure_ascii=False))
    return value


def status() -> dict:
    return editor("r=json.loads(unreal.get_editor_subsystem(unreal.GuLiLocalPIESubsystem).get_session_status())")


def game(code: str) -> dict:
    return editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world()\n"
                  "assert w, 'PIE is not running'\n"
                  "p=next(s for s in unreal.ObjectIterator(unreal.GuLiPerformanceSubsystem) if s.get_outer()==w)\n"
                  "a=next(s for s in unreal.ObjectIterator(unreal.GuLiBattleAuthoritySubsystem) if s.get_outer()==w)\n" + code)


def make_recipe(diagnostics: dict, scenario: str) -> dict:
    local = sorted((s for s in diagnostics["soldiers"] if s["alive"] and
                    s["can_simulate"] and s["team"] == 1 and not s["engineering"]), key=lambda s: s["id"])
    ground = [s for s in local if s["unit_type"] == 1]
    air = [s for s in local if s["unit_type"] == 2]
    if len(ground) < 50 or (scenario == "gather" and len(air) < 25):
        raise RuntimeError("Full starting local formations are not available")
    groups = [ground[:25], air[:25] if scenario == "gather" else ground[25:50]]
    members, commands = [], []
    for group_index, group in enumerate(groups):
        side = -1 if group_index == 0 else 1
        for i, unit in enumerate(group):
            pitch = 1300 if unit["unit_type"] == 2 else 650
            members.append({"id": unit["id"], "unit_type": unit["unit_type"],
                            "position": [-35000 + side * (5000 + (i // 5) * pitch),
                                         62000 + (i % 5 - 2) * pitch, 900]})
        if not (scenario == "hold" and group_index == 1):
            target = [-35000, 62000, 900] if scenario == "gather" else [-35000 - side * 7000, 62000, 900]
            commands.append({"at": .5 + group_index * .3, "members": [s["id"] for s in group], "target": target})
    return {"schema": "guli-movement-capture/v1", "scenario": scenario,
            "map": "/Game/Maps/LVL_CommanderMassPrototype", "automatic_fire": False,
            "members": members, "commands": commands}


def population(census: dict) -> dict:
    return {key: {field: row[field] for field in
                  ("alive", "local_alive_owned", "peer_alive_owned", "local_simulation", "dormant")}
            for key, row in census["by_definition"].items()
            if key.startswith(("unit/", "building/", "territory/", "player/"))}


def frozen_units(diagnostics: dict) -> list:
    fields = ("id", "unit_type", "team", "alive", "local_simulation", "movement_layer", "position", "health")
    return [{k: unit[k] for k in fields} for unit in sorted(diagnostics["soldiers"], key=lambda s: s["id"])]


def verify_units(expected: list, actual: list) -> None:
    if len(expected) != len(actual):
        raise RuntimeError("Frozen full population changed")
    for a, b in zip(expected, actual):
        if any(a[k] != b[k] for k in a if k != "position"):
            raise RuntimeError(f"Frozen unit identity, ownership or health changed: {a['id']}")
        if any(abs(x - y) > 1.0 for x, y in zip(a["position"], b["position"])):
            raise RuntimeError(f"Frozen background position drifted: {a['id']}")


def frozen_scene(scene: dict) -> dict:
    if scene.get('error'):
        raise RuntimeError(scene['error'])
    result = dict(scene)
    result['ore'] = sorted(scene['ore'], key=lambda row: row['id'])
    # Gift creation has an asynchronous allocation order. Compare the complete
    # physical/semantic building, not that session's transient instance number.
    # Keep the original IDs in scene.json for target resolution and auditing.
    buildings = [{key: value for key, value in row.items() if key != 'id'} for row in scene['buildings']]
    result['buildings'] = sorted(buildings, key=lambda row: (row['definition'], row['team'], *row['position']))
    return result


def verify_scene(expected, actual, path='scene') -> None:
    if isinstance(expected, dict):
        if expected.keys() != actual.keys():
            raise RuntimeError(f'Frozen fields changed: {path}')
        for key in expected:
            verify_scene(expected[key], actual[key], path + '.' + key)
    elif isinstance(expected, list):
        if len(expected) != len(actual):
            raise RuntimeError(f'Frozen array changed: {path}')
        for i, (a, b) in enumerate(zip(expected, actual)):
            verify_scene(a, b, f'{path}[{i}]')
    elif isinstance(expected, (int, float)) and not isinstance(expected, bool):
        if abs(expected - actual) > (1.0 if 'camera_position' in path else .0001):
            raise RuntimeError(f'Frozen numeric value changed: {path}: {expected} -> {actual}')
    elif expected != actual:
        raise RuntimeError(f'Frozen value changed: {path}')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scenario", choices=["crossing", "hold", "gather", "mixed", "attack", "buildings", "ore", "work"], required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--recipe", type=Path)
    parser.add_argument('--preview', action='store_true', help='After replay, keep this PIE open for player control until the player stops it')
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)

    def save(name, value):
        (output / name).write_text(json.dumps(value, ensure_ascii=False, indent=2), encoding="utf-8")

    if status().get("active"):
        raise RuntimeError("An existing PIE is in use; the capture only starts its own disposable session")
    original = editor("s=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrikeEditor.GuLiLocalPIESettings'))\n"
                      "r={'prompt':s.get_editor_property('bPromptForRole'),'role':int(s.get_editor_property('MainRole').value),"
                      "'idle':unreal.SystemLibrary.get_console_variable_int_value('t.IdleWhenNotForeground'),"
                      "'slate':unreal.SystemLibrary.get_console_variable_int_value('Slate.bAllowThrottling'),"
                      "'isolate_combat':unreal.SystemLibrary.get_console_variable_int_value('gs.Review.GroundIsolateCombat'),"
                      "'fixture_hold':unreal.SystemLibrary.get_console_variable_int_value('gs.Perf.MovementFixtureHold')}\n"
                      "s.set_editor_property('bPromptForRole',False,unreal.PropertyAccessChangeNotifyMode.NEVER)\n"
                      "s.set_editor_property('MainRole',unreal.GuLiCommanderRole.COMMANDER,unreal.PropertyAccessChangeNotifyMode.NEVER)")
    save("settings-original.json", original)
    owned = False
    try:
        editor("w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
               "assert w.get_path_name().startswith('/Game/Maps/LVL_CommanderMassPrototype.'), 'Wrong map'\n"
               "unreal.SystemLibrary.execute_console_command(w,'t.IdleWhenNotForeground 0')\n"
               "unreal.SystemLibrary.execute_console_command(w,'Slate.bAllowThrottling 0')\n"
               "unreal.SystemLibrary.execute_console_command(w,'gs.Review.GroundIsolateCombat 1')\n"
               "unreal.SystemLibrary.execute_console_command(w,'gs.Perf.MovementFixtureHold 3')\n"
               "unreal.SystemLibrary.execute_console_command(w,'gs.Commander.QA.StartPIE standalone 1600 900')\n"
               "r={'started':True}")
        owned = True
        deadline = time.monotonic() + 90
        while time.monotonic() < deadline:
            s = status()
            if s.get("error") or s.get("network_error"):
                raise RuntimeError(json.dumps(s))
            if (s.get("world_ready") and s.get("can_issue_orders") and len(s.get("members", [])) == 2
                    and all(m.get("ready") for m in s["members"])):
                break
            time.sleep(1)
        else:
            save("readiness-failure.json", s)
            raise RuntimeError("Both clients did not become ready within 90 seconds")
        save("session.json", s)
        print(json.dumps({"phase": "warmup", "scenario": args.scenario}), flush=True)
        game("pc=unreal.GameplayStatics.get_player_controller(w,0)\n"
             "unreal.SystemLibrary.execute_console_command(w,'gs.GM.Commander.Camera.Height 133.4')\n"
             "pc.get_view_target().set_actor_rotation(unreal.Rotator(0,0,0),False)\n"
             "pc.get_view_target().jump_to_world_location(unreal.Vector(-35000,62000,900))\n"
             "pc.get_view_target().set_actor_tick_enabled(False)\n"
             "r={'camera_prepared':True}")
        time.sleep(10)
        construction = None
        if args.scenario == 'work':
            previous = json.loads(args.recipe.read_text(encoding='utf-8')) if args.recipe else {}
            preferred = previous.get('construction_preparation', {}).get('ground', [-1800, 72000, 900])
            placement = game(f"r=json.loads(p.prepare_movement_building(2,unreal.Vector(*{preferred!r})))")
            save('construction-submission.json', placement)
            if not placement.get('submitted'):
                raise RuntimeError('Construction preparation was not admitted')
            deadline = time.monotonic()+20
            while time.monotonic()<deadline:
                scene = game("r=json.loads(p.get_movement_scene_json())")
                construction = next((b for b in scene['buildings'] if b['definition']==2 and b['phase']==0 and b['team']==1), None)
                if construction:
                    time.sleep(1)  # Slot generation and navigation build, outside timing.
                    break
                time.sleep(.5)
            if not construction:
                raise RuntimeError('Construction placement produced no unfinished building within 20 seconds')
        context = None
        if args.scenario in ('mixed','attack','buildings','ore','work'):
            context = game("resources=next(s for s in unreal.ObjectIterator(unreal.GuLiResourceWorldSubsystem) if s.get_outer()==w)\n"
                           "m=resources.get_map_definition()\n"
                           "scene=json.loads(p.get_movement_scene_json())\n"
                           "factory=min((b for b in scene['buildings'] if b['definition']==6 and b['team']==1),key=lambda b:abs(b['ground'][0]))\n"
                           "f=next(f for f in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiResourceFactoryActor) if abs(f.get_actor_location().x-factory['ground'][0])<1 and abs(f.get_actor_location().y-factory['ground'][1])<1)\n"
                           "factory['unload_points']=[[p.x,p.y,p.z] for p in f.get_unload_points()]\n"
                           "clusters=[]\n"
                           "for c in m.get_editor_property('clusters'):\n"
                           " v=c.get_editor_property('center'); i=c.get_editor_property('cluster_id')\n"
                           " clusters.append({'id':i,'center':[v.x,v.y,v.z],'allowed':resources.can_team_mine_at(unreal.GuLiTeam.RED,i)})\n"
                           "def dist(c,p):return sum((a-b)**2 for a,b in zip(c['center'][:2],p[:2]))\n"
                           "ore=min(clusters,key=lambda c:dist(c,[-35000,62000]))\n"
                           "mining=min((c for c in clusters if c['allowed']),key=lambda c:dist(c,factory['ground']))\n"
                           "ore['nodes']=[{'id':n.get_editor_property('node_id'),'initial':n.get_editor_property('initial_amount')} for n in m.get_editor_property('nodes') if n.get_editor_property('cluster_id')==ore['id']]\n"
                           "r={'factory':factory,'ore_cluster':ore,'mining_cluster':mining}")
            context['construction'] = construction
            save('scenario-context.json', context)
        before = game("r=json.loads(a.get_move_response_diagnostics())")
        save("world-before-preparation.json", before)
        recipe = json.loads(args.recipe.read_text(encoding="utf-8")) if args.recipe else (
            make_extended_recipe(before,args.scenario,context) if context else make_recipe(before,args.scenario))
        # Full-population evidence stays in the driver. Sending it through the
        # editor console envelope exceeds the bridge's bounded request size.
        native_recipe = {key: value for key, value in recipe.items() if not key.startswith("_")}
        prepared = game(f"r=json.loads(p.prepare_movement_capture({json.dumps(native_recipe)!r}))")
        save("preparation.json", prepared)
        if prepared.get("error") or not prepared.get("prepared"):
            raise RuntimeError(prepared.get("error", "Preparation did not complete"))
        save("recipe.json", prepared["recipe"])
        if args.scenario == 'attack':
            game("unreal.SystemLibrary.execute_console_command(w,'gs.Review.GroundIsolateCombat 0')\nr={'attack_enabled':True}")
        applied = game("r={'diagnostics':json.loads(a.get_move_response_diagnostics()),'profile':json.loads(p.get_capture_json())}")
        save("world-prepared.json", applied)
        scene_raw = game("r=json.loads(p.get_movement_scene_json())")
        save("scene.json", scene_raw)
        scene = frozen_scene(scene_raw)
        source_map = ROOT / 'Content/Maps/LVL_CommanderMassPrototype.umap'
        stat = source_map.stat()  # Metadata only; never read binary asset contents.
        scene['source_map_stat'] = {'size': stat.st_size, 'mtime_ns': stat.st_mtime_ns}
        if recipe.get('_scene'):
            expected_scene = frozen_scene(recipe['_scene'])
            # A v3 development recipe from before map metadata was recorded is
            # a preparation input, not a qualified frozen-scene comparison.
            if 'source_map_stat' not in expected_scene:
                raise RuntimeError('The frozen recipe predates complete scene metadata; create a fresh recipe')
            # Navigation preparation and editor-only review notes can resave
            # the package without changing the measured world. File size and
            # timestamp are evidence, not gameplay layout identities. Require
            # baked source/layout hashes, every building/ore entry, graphics,
            # camera and the complete unit snapshot to match below.
            expected_stamp = expected_scene['source_map_stat']
            actual_stamp = scene['source_map_stat']
            if actual_stamp != expected_stamp:
                save('map-reserialization.json', {'old_stat': expected_stamp, 'new_stat': actual_stamp,
                     'reason': 'Package resaved during navigation preparation or editor-only review authoring',
                     'semantic_comparison': 'All remaining frozen scene fields and full units must match'})
            verify_scene({k:v for k,v in expected_scene.items() if k != 'source_map_stat'},
                         {k:v for k,v in scene.items() if k != 'source_map_stat'})
        census = prepared["census_before"]
        if census["live_soldiers"] != 600 or any(census['by_definition'][f'unit/{i}']['alive'] != 16 for i in (3, 4)):
            raise RuntimeError("The complete opening workload must contain 600 combat units and all 32 engineering vehicles")
        shape, units = population(census), frozen_units(applied["diagnostics"])
        if recipe.get("_population") and recipe["_population"] != shape:
            raise RuntimeError("Frozen definition/simulation population differs")
        if recipe.get("_units"):
            verify_units(recipe["_units"], units)
        prepared["recipe"]["_population"] = shape
        prepared["recipe"]["_units"] = units
        prepared["recipe"]["_scene"] = scene
        if recipe.get('_scene'):
            prepared['recipe']['_scene']['source_map_stat'] = recipe['_scene']['source_map_stat']
        save("recipe.json", prepared["recipe"])
        started = game("r={'started':p.start_movement_capture(),'state':json.loads(p.get_movement_capture_json())}")
        save("start.json", started)
        if not started["started"]:
            raise RuntimeError(started["state"].get("error", "Capture did not start"))
        print(json.dumps({"phase": "measuring", "seconds": 30, "scenario": args.scenario}), flush=True)
        time.sleep(32)
        captured = game("r={'movement':json.loads(p.get_movement_capture_json()),'performance':json.loads(p.get_capture_json()),"
                        "'diagnostics':json.loads(a.get_move_response_diagnostics())}")
        save("capture.json", captured)
        save('scene-after.json',game("r=json.loads(p.get_movement_scene_json())"))
        if args.scenario == 'work':
            save('work-after.json',game("r={'builders':[{'debug':b.get_component_by_class(unreal.GuLiConstructionWorkComponent).get_construction_debug(),'actor':b.get_name()} for b in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiConstructionVehiclePawn)],"
                                       "'miners':[{'state':int(c.get_task_state().value),'cargo_blue':c.get_cargo().blue,'cargo_red':c.get_cargo().red} for c in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.GuLiMiningVehiclePawn)]}"))
        m, p = captured["movement"], captured["performance"]
        valid = (m.get("finished") and not m.get("error") and m["seconds"] >= 30
                 and p.get("capture_valid") and not p.get("capturing")
                 and m["commands_sent"] == len(recipe["commands"])
                 and (args.scenario == 'attack' or population(m["census_after"]) == shape))
        valid = valid and m.get('observation_schema') == 3 and m.get('applications_observed', 0) >= m.get('observed_logical_steps', 0)
        save("result.json", {"valid_window": bool(valid), "complete_acceptance_suite": False,
                             "scenario": args.scenario, "error": m.get("error", "")})
        if not valid:
            raise RuntimeError(m.get("error") or "Native window incomplete")
        print(json.dumps({"phase": "captured", "valid_window": True,
                          "arrived": sum(x["arrival_seconds"] >= 0 for x in m["members"])}), flush=True)
        if args.preview:
            print('Player preview ready. Continue issuing normal commander orders; stop PIE to restore the previous editor settings.', flush=True)
            while status().get('active'):
                time.sleep(5)
    except Exception as error:
        save("failure.json", {"error": str(error), "baseline_gate_passed": False})
        raise
    finally:
        if owned:
            editor("unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()\nr={'requested_end':True}")
            deadline = time.monotonic() + 20
            while time.monotonic() < deadline:
                ended = status()
                if not ended.get("active") and not ended.get("logic_running") and not ended.get("coordinator_running"):
                    save("shutdown.json", ended)
                    break
                time.sleep(.5)
        editor("s=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrikeEditor.GuLiLocalPIESettings'))\n"
               f"s.set_editor_property('bPromptForRole',{original['prompt']!r},unreal.PropertyAccessChangeNotifyMode.NEVER)\n"
               f"s.set_editor_property('MainRole',unreal.GuLiCommanderRole.{['UNASSIGNED','COMMANDER','GROUND','AIR','OBSERVER'][original['role']]},unreal.PropertyAccessChangeNotifyMode.NEVER)\n"
               "w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
               f"unreal.SystemLibrary.execute_console_command(w,'t.IdleWhenNotForeground {original['idle']}')\n"
               f"unreal.SystemLibrary.execute_console_command(w,'Slate.bAllowThrottling {original['slate']}')\n"
               f"unreal.SystemLibrary.execute_console_command(w,'gs.Review.GroundIsolateCombat {original['isolate_combat']}')\n"
               f"unreal.SystemLibrary.execute_console_command(w,'gs.Perf.MovementFixtureHold {original['fixture_hold']}')\n"
               "r={'settings_restored':True}")


if __name__ == "__main__":
    main()
