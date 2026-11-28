"""Exercise the live Xuntian HUD trial through the same gunner and F4 selection used by the pilot.

Start in a freshly opened editor on LVL_CommanderMassPrototype. Results contain structured game data only.
"""
import json
import re
import time
import traceback
from pathlib import Path
import unreal


def read_ring_geometry():
    path = '/Game/GuLiStrike/FX/GroundWarning/M_GroundWarning_Circle'
    material = unreal.load_asset(path)
    for expression in unreal.ObjectIterator(unreal.MaterialExpressionCustom):
        if not expression.get_path_name().startswith(path + '.'):
            continue
        match = re.search(r'float r = length\(UV \* 2 - 1\) / ([.\d]+);', expression.get_editor_property('code'))
        if match:
            return material, float(match.group(1))
    raise ValueError('Missing radial mask on the warning material')


def sample_blast_components(world, tier_record, material, rim_fraction, resource, has_warnings):
    if has_warnings and 'warning_radii_cm' not in tier_record:
        radii = []
        for decal in unreal.ObjectIterator(unreal.DecalComponent):
            if decal.get_world() != world or not decal.is_visible():
                continue
            instance = decal.get_decal_material()
            if instance and instance.get_base_material() == material:
                size, scale = decal.get_editor_property('decal_size'), decal.get_world_scale()
                radii.append([round(abs(size.y * scale.y) * rim_fraction, 2),
                              round(abs(size.z * scale.z) * rim_fraction, 2)])
        if radii:
            tier_record['warning_radii_cm'] = radii
    if 'blast_component' not in tier_record:
        for comp in unreal.ObjectIterator(unreal.NiagaraComponent):
            if comp.get_world() != world or not comp.is_active():
                continue
            system = comp.get_asset()
            if system and system.get_path_name() == resource:
                scale, has_scale = comp.get_variable_float('User.Area_Scale')
                radius, has_radius = comp.get_variable_float('User.Radius')
                assert has_scale and has_radius, resource
                tier_record['blast_component'] = dict(asset=resource, area_scale=scale, radius_cm=radius)
                break


def start(short_salvos=False, check_blast_radius=False, upgrades_only=False, concurrent_hangar=False):
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    world = editor.get_editor_world()
    assert world.get_outermost().get_name() == '/Game/Maps/LVL_CommanderMassPrototype'
    assert not unreal.WidgetService.is_pie_running()
    folder = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))/'TestResults/XuntianBelly'
    folder.mkdir(parents=True,exist_ok=True)
    output = folder / ('air-damage.json' if short_salvos else 'cold-start.json')
    if concurrent_hangar:
        upgrades_only = True
        output = folder / 'f4-hangar-regression.json'
    elif upgrades_only:
        output = folder / 'f4-input.json'
    elif check_blast_radius:
        output = folder / 'blast-range-pie.json'
        material, rim_fraction = read_ring_geometry()
        registry = json.loads((folder.parents[1]/'Data/Json/DT_GuLiStrikeVfx_Effects.json').read_text(encoding='utf8'))
        explosions = {row['Name']: row for row in registry if row['Name'].startswith('XuntianBellyExplosion')}
    session = unreal.get_editor_subsystem(unreal.GuLiLocalPIESubsystem)
    session.queue_xuntian_hud_range()
    result={'map':world.get_outermost().get_name(),'stage':'starting','tiers':[]}
    def save():
        output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    save()
    unreal.SystemLibrary.execute_console_command(world,'gs.Commander.QA.StartPIE standalone 1600 900')
    started=time.monotonic()
    last_poll=0
    handle=None
    stage='starting'
    since=started
    shots={}
    fields={}
    sample={}
    current=None

    def inspect(action='none'):
        return json.loads(unreal.GuLiHybridAuthoringLibrary.inspect_xuntian_battery(action))

    def send_f4(pressed):
        player = unreal.GameplayStatics.get_player_controller(editor.get_game_world(), 0)
        command = 'F4Down' if pressed else 'F4Up'
        assert unreal.GuLiRogueCardQALibrary.action(player, command, 0), command

    def begin_stage(name):
        nonlocal stage,since
        stage=name
        since=time.monotonic()
        result['stage']=name
        save()

    def finished(ok,reason=''):
        if unreal.WidgetService.is_pie_running():
            inspect('release')
        result['passed']=ok
        result['error']=reason
        result['stage']='complete' if ok else 'failed'
        result['last_state']=sample
        result['session']=json.loads(session.get_session_status())
        save()
        unreal.unregister_slate_post_tick_callback(handle)
        if unreal.WidgetService.is_pie_running():
            unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_request_end_play()

    def tick(delta):
        nonlocal last_poll,current,sample
        now=time.monotonic()
        if now-last_poll < (.08 if check_blast_radius else .1 if short_salvos else .24):
            return
        last_poll=now
        try:
            sample=inspect()
            if not sample.get('ready'):
                if now-started>95: finished(False,'Pilot did not become ready')
                return
            elapsed=now-since
            tier=sample['tier']
            if stage=='starting':
                result['before_input']=sample
                assert tier==1 and len(sample['sockets'])==3,sample
                assert sample['tiers_resident']==[True,True,True] and sample['pending_compiles']==0,sample
                if upgrades_only:
                    begin_stage('ready_for_f4')
                    return
                inspect('ground' if check_blast_radius else 'low')
                begin_stage('aim')
            elif stage=='ready_for_f4':
                game_world = editor.get_game_world()
                ship = unreal.GameplayStatics.get_player_pawn(game_world, 0)
                authority = ship.get_component_by_class(unreal.GuLiHybridActorComponent)
                if not authority or not authority.can_control() or not json.loads(session.get_session_status()).get('world_ready'):
                    assert elapsed < 30, 'Pilot has not acquired local flight controls'
                    return
                if concurrent_hangar and tier == 1:
                    marker = json.loads(unreal.GuLiHybridAuthoringLibrary.inspect_xuntian_marker_material())
                    assert marker.get('guide') and marker['ready'] and marker['own_render_shader'], marker
                    result['bound_marker_material'] = marker
                context = unreal.load_asset('/Game/GuLiStrike/Ship/Build/Input/IMC_XuntianBelly')
                mapping = context.default_key_mappings.mappings[0]
                local = next(s for s in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem)
                             if not s.get_name().startswith('Default__') and s.has_mapping_context(context) is not None)
                keys = [str(key.get_editor_property('key_name')) for key in local.query_keys_mapped_to_action(mapping.action)]
                assert keys == ['F4'] and not sample['modal'], (keys, sample)
                current = dict(tier=tier, sockets=sample['sockets'], mapped_keys=keys, action=mapping.action.get_path_name(),
                               can_control=authority.can_control())
                result['tiers'].append(current)
                send_f4(True)
                begin_stage('f4_down')
            elif stage=='aim':
                if elapsed<1.8: return
                if not sample['ground_bank'] or sample['coverage']==0 or (check_blast_radius and not sample['terrain_aim']):
                    inspect('ground')
                    if elapsed<4: return
                assert sample['ground_bank'] and sample['coverage']>0,sample
                if check_blast_radius:
                    assert sample['terrain_aim'], sample
                current={'tier':tier,'sockets':sample['sockets'],'before_health':sample['other_ship_health'],
                         'before_bursts':sample['bursts'],'before_commits':sample['commits'],'max_warnings':0,
                         'air_hits':[],'after_health':sample['other_ship_health']}
                if check_blast_radius:
                    current.update(terrain_aim=sample['terrain_aim'], aim=sample['aim'])
                result['tiers'].append(current)
                shots.clear();fields.clear()
                inspect('press')
                begin_stage('firing')
            elif stage in ('firing','draining'):
                for effect in sample['effects']:
                    if effect['field']!='XuntianBellyL'+str(current['tier']): continue
                    records=shots if effect['projectile'] else fields
                    records[effect['id']]=effect
                current['max_warnings']=max(current['max_warnings'],sample['warnings'])
                if check_blast_radius:
                    explosion = explosions['XuntianBellyExplosion'+str(tier)]
                    sample_blast_components(editor.get_game_world(), current, material, rim_fraction, explosion['ResourcePath'],
                                            sample['warnings'] > 0 and bool(shots))
                for target,hp in sample['other_ship_health'].items():
                    previous=current['after_health'].get(target,hp)
                    if 0 < hp < previous: current['air_hits'].append(previous-hp)
                current.update(projectiles=len(shots),impacts=len(fields),after_health=sample['other_ship_health'],
                               after_commits=sample['commits'],after_bursts=sample['bursts'])
                if stage=='firing' and (elapsed>=4.8 or (short_salvos and shots)):
                    inspect('release')
                    begin_stage('draining')
                    return
                if stage=='draining':
                    if elapsed<(2.5 if short_salvos else 1) or (sample['warnings']>0 and elapsed<17): return
                    current.update(warnings_after=sample['warnings'],shots=list(shots.values()),fields=list(fields.values()))
                    assert shots and fields and current['max_warnings']>0 and sample['warnings']==0,current
                    radius=(800,1200,1600)[tier-1]
                    assert all(e['speed']==14000 and e['radius']==radius and e['ground_warning'] for e in shots.values()),current
                    assert all(e['radius']==radius for e in fields.values()),current
                    if check_blast_radius:
                        assert current.get('warning_radii_cm') and all(abs(axis-radius)<.1 for axes in current['warning_radii_cm'] for axis in axes), current
                        blast = current.get('blast_component', {})
                        assert blast.get('radius_cm') == radius, current
                        assert abs(blast['area_scale'] - radius / explosion['RadiusAtUnitScaleCm']) < .001, current
                    if short_salvos:
                        expected=(56,70,84)[tier-1]
                        assert current['air_hits'] and all(abs(d/expected-round(d/expected))<.002 for d in current['air_hits']),current
                    assert not sample['modal']
                    send_f4(True)
                    begin_stage('f4_down')
            elif stage=='f4_down':
                # Let Enhanced Input process a pressed frame before releasing the key.
                send_f4(False)
                begin_stage('waiting_for_menu')
            elif stage=='waiting_for_menu':
                if not sample['modal']:
                    assert elapsed < 3, 'F4 did not open the belly upgrade panel while piloting Xuntian'
                    return
                current['opened_by_f4'] = True
                if tier==3:
                    result['max_level_menu']=sample
                    finished(True)
                    return
                if concurrent_hangar and tier==1:
                    player = unreal.GameplayStatics.get_player_controller(editor.get_game_world(), 0)
                    build = player.get_editor_property('player_state').get_component_by_class(unreal.GuLiShipBuildComponent)
                    state = build.get_build_state()
                    assert list(map(str,state.chosen_node_ids)) == ['01']
                    request = build.request_confirmed_choice(unreal.GuidLibrary.new_guid(), '08', state.match_epoch, state.build_revision)
                    assert unreal.GuidLibrary.is_valid_guid(request)
                click = inspect('click_upgrade')
                assert click.get('clicked'), 'The displayed upgrade button did not receive a pointer click'
                current['slate_button_clicked'] = True
                begin_stage('upgrading')
            elif stage=='upgrading':
                if tier==current['tier']+1 and not sample['modal']:
                    assert len(sample['sockets'])==(2 if tier==2 else 5),sample
                    if concurrent_hangar:
                        state = inspect('build')
                        expected = ['01', '08', '02'] if tier==2 else ['01', '08', '02', '03']
                        assert state['hangar_enabled'] and state['ledger_nodes']==state['installed_nodes']==state['player_nodes']==expected, state
                        current['confirmed_nodes'] = state['ledger_nodes']
                    if upgrades_only:
                        begin_stage('ready_for_f4')
                        return
                    inspect('ground' if check_blast_radius else 'low')
                    begin_stage('aim')
                elif elapsed>18:
                    finished(False,'Confirmed ship build did not advance')
        except Exception:
            finished(False,traceback.format_exc())

    handle=unreal.register_slate_post_tick_callback(tick)
    return str(output)
