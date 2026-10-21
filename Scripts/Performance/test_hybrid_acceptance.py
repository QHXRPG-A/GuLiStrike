"""Regression tests for the acceptance denominator and inclusive-scope union."""
import contextlib
import copy
import csv
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from hybrid_workload import HIGH_LOAD_MINIMUM, comparable_census, validate_minimum, combat_inputs, sampled_window_load, camera_comparison, history_scale_comparison
from analyze_hotspot_capture import analyze, response_comparison, slate_client_scope
from analyze_hybrid_soak import occupancy


def census():
    return {'ready': True, 'local_player': 'A', 'by_definition': {
        name: {'total': count*2, 'alive': count*2, 'local_alive_owned': count,
               'peer_alive_owned': count, 'local_simulation': count, 'dormant': 0}
        for name, count in HIGH_LOAD_MINIMUM.items()}}


class HybridAcceptanceTests(unittest.TestCase):
    def test_slate_camera_gate_rejects_overview_or_changed_pose(self):
        a={'viewport':[2359,1751],'camera_location':[1,2,3],'camera_rotation':[0,-35,0],
           'camera_tier':'TACTICAL','camera_target_height_m':133.4,'camera_transitioning':False}
        self.assertTrue(camera_comparison(a,copy.deepcopy(a))['comparable'])
        for field,value in [('camera_tier','OVERVIEW'),('camera_location',[1,2,160000]),
                            ('camera_rotation',[0,-60,0]),('camera_transitioning',True)]:
            b=copy.deepcopy(a);b[field]=value
            self.assertFalse(camera_comparison(a,b)['comparable'])
        b=copy.deepcopy(a);del b['camera_tier']
        self.assertFalse(camera_comparison(a,b)['comparable'])

    def test_total_or_local_ownership_cannot_replace_active_simulation(self):
        value=census();validate_minimum(value)
        value['by_definition']['unit/5']['local_simulation']-=1
        with self.assertRaisesRegex(ValueError, 'unit/5'):validate_minimum(value)
        value['by_definition']['unit/5']['total']=10000
        with self.assertRaises(ValueError):validate_minimum(value)

    def test_authority_and_life_composition_must_match(self):
        a=census();b=copy.deepcopy(a);b['local_player']='another fresh room identity'
        self.assertTrue(comparable_census(a,b))
        b['by_definition']['unit/2']['peer_alive_owned']+=1
        self.assertFalse(comparable_census(a,b))
        self.assertFalse(comparable_census(a,{'ready':False}))
        b=copy.deepcopy(a);del b['by_definition']['unit/4']['dormant']
        self.assertFalse(comparable_census(a,b))

    def test_combat_casualties_cannot_qualify_reduced_window_load(self):
        initial=census();later=copy.deepcopy(initial);later['by_definition']['unit/1']['local_simulation']-=1
        profile={'network_samples':[{'simulation_census':initial},{'simulation_census':later}],
                 'hotspot_workload':{'simulation_census_start':initial,'simulation_census_end':later}}
        result=sampled_window_load(profile)
        self.assertFalse(result['maintained'])
        self.assertEqual(len(result['failures']),2)
        self.assertFalse(sampled_window_load({})['maintained'])

    def test_frozen_static_load_rejects_extra_units_and_history_mismatch(self):
        initial=census();later=copy.deepcopy(initial);later['by_definition']['unit/1']['local_simulation']+=1
        profile={'network_samples':[{'simulation_census':initial},{'simulation_census':later}]}
        self.assertTrue(sampled_window_load(profile)['maintained'])
        self.assertFalse(sampled_window_load(profile,exact_units=True)['maintained'])
        a={'directory_records':10000,'retired_target_records':15}
        self.assertTrue(history_scale_comparison(a,dict(a))['matched'])
        self.assertFalse(history_scale_comparison(a,dict(a,directory_records=10600))['matched'])
        self.assertFalse(history_scale_comparison(a,dict(a,retired_target_records=16))['matched'])
        self.assertFalse(history_scale_comparison(a,{})['matched'])

    def test_combat_inputs_include_actual_types_and_positions(self):
        a={'enemy_ordinal':2,'enemy_position':[1,2,3],'token':'first','positions':[
            {'ordinal':1,'definition':'unit/1','generation':1,'applied':[4,5,6],'before':[0,0,0]}]}
        b=copy.deepcopy(a);b['token']='second';b['positions'][0]['before']=[7,8,9]
        self.assertEqual(combat_inputs(a),combat_inputs(b))
        b['positions'][0]['definition']='unit/2'
        self.assertNotEqual(combat_inputs(a),combat_inputs(b))
        b=copy.deepcopy(a);b['positions'][0]['applied'][0]+=1
        self.assertNotEqual(combat_inputs(a),combat_inputs(b))

    def test_long_queue_trend_keeps_transient_peaks_separate_from_growth(self):
        samples=[{'wall_seconds':float(i),'async_transactions':{'input_bytes':(i//60)*100}}
                 for i in range(360)]
        growing=occupancy(samples,'input_bytes')
        self.assertTrue(growing['five_minute_continuous_growth_observed'])
        for i,row in enumerate(samples):row['async_transactions']['input_bytes']=10000 if 60<=i<120 else 0
        transient=occupancy(samples,'input_bytes')
        self.assertFalse(transient['five_minute_continuous_growth_observed'])
        self.assertEqual(transient['peak'],10000)
        for i,row in enumerate(samples):row['async_transactions']['core_waiting_result_authority']=i//60 if i<359 else 0
        late_clear=occupancy(samples,'core_waiting_result_authority')
        self.assertEqual(late_clear['last'],0)
        self.assertTrue(late_clear['five_minute_continuous_growth_observed'])

    def test_hybrid_union_counts_nested_work_once_and_moved_work(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);out=root/'analysis';out.mkdir()
            (root/'capture.json').write_text(json.dumps({'valid':True}))
            counters={k:0 for k in ('input_bytes','output_admitted_bytes','result_reserved_bytes','control_bytes',
                'worker_ms','main_apply_ms','simulation_ms','logical_steps','inputs','applications','hud_full_object_copies')}
            profile={'capture_valid':True,'qpc_frequency':1,'engine_frames':300,'counters':{},'sample_metrics':{},
                'hotspot_workload':{'start_qpc':0,'end_qpc':30},
                'network_samples':[{'wall_seconds':t,'async_transactions':counters} for t in (0,30)]}
            (root/'profile-after.json').write_text(json.dumps(profile))
            with (out/'game-thread-events.csv').open('w',newline='') as stream:
                writer=csv.DictWriter(stream,fieldnames=['TimerName','StartTime','EndTime','Duration','Depth']);writer.writeheader()
                for frame in range(300):
                    start=frame/10
                    for name,offset,length,depth in [
                        ('FEngineLoop::Tick',0,.1,0),('GuLiHybrid_WorldTick',.001,.006,1),
                        ('GuLiHybrid_MainLogicalBatch',.002,.004,2),('GuLiHybrid_MainReceipt',.003,.001,3),
                        ('GuLiHybrid_MainReceipt',.010,.001,1),('GuLiCommander_MovementApply',.012,.002,1),
                        ('GuLiHybrid_NetworkTick',.020,.004,1),
                        ('Slate::Tick (Time and Widgets)',.030,.005,1),
                        ('Slate::DrawWindows',.030,.005,2),('Slate::Prepass',.030,.001,3),
                        ('GuLiSlate_EditorOnly_Prepass',.030,.0004,4),
                        ('GuLiSlate_GameWindow_Prepass',.0304,.0006,4),
                        ('Slate::DrawWindow',.031,.001,3),('GuLiSlate_EditorOnly_Paint',.031,.001,4),
                        ('Slate::DrawWindow',.032,.003,3),('GuLiSlate_GameWindow_Paint',.032,.003,4),
                        ('GuLiSceneUI_Paint',.032,.001,5),('Paint: Game UI',.033,.002,5),
                        ('Slate::Tick (Platform and Input)',.040,.003,1),
                        ('ProcessLocalPlayerSlateOperations',.045,.001,1),
                        ('GuLiCommanderPredictiveAvoidanceProcessor_Execute',.050,.004,1),
                        ('GuLiCommander_PredictiveAvoidancePrepare',.051,.002,2)]:
                        writer.writerow(dict(TimerName=name,StartTime=start+offset,EndTime=start+offset+length,Duration=length,Depth=depth))
            (out/'all-thread-timers.csv').write_text('Name,Count,Incl,Excl\n')
            (out/'worker-calculations.csv').write_text('ThreadId,TimerName,Duration\n')
            with patch('analyze_hotspot_capture.trace_clock',return_value={'qpc_origin_seconds':0}),contextlib.redirect_stdout(io.StringIO()):
                result=analyze(root,Path('unused'))
            self.assertAlmostEqual(result['hybrid_target']['actual']['mean_ms'],17)
            self.assertEqual(result['hybrid_target']['scope_version'],5)
            self.assertFalse(result['simulation_clock']['available'])
            self.assertFalse(result['simulation_clock']['maintained'])
            self.assertAlmostEqual(result['world_update_only']['actual']['mean_ms'],9)
            self.assertAlmostEqual(result['game_thread']['mean_ms'],100)
            self.assertFalse(result['hybrid_target']['met'])
            self.assertAlmostEqual(result['slate_full_pie']['actual']['mean_ms'],5)
            self.assertIsNone(result['slate_target']['actual'])
            self.assertAlmostEqual(result['slate_target']['game_ui_paint_subset']['mean_ms'],3)
            self.assertAlmostEqual(result['slate_target']['platform_input_separate']['mean_ms'],3)
            self.assertAlmostEqual(result['slate_target']['local_player_operations_separate']['mean_ms'],1)
            self.assertFalse(result['slate_target']['timing_met'])
            (root/'slate-window-scope.json').write_text(json.dumps({'scope_version':2,'complete':True,
                'valid':True,'invalid_pairs':0,'binding_changes':0,'windows':[{'kind':'game'}]}))
            with patch('analyze_hotspot_capture.trace_clock',return_value={'qpc_origin_seconds':0}),contextlib.redirect_stdout(io.StringIO()):
                measured=analyze(root,Path('unused'))
            self.assertAlmostEqual(measured['slate_full_pie']['actual']['mean_ms'],5)
            self.assertAlmostEqual(measured['slate_target']['actual']['mean_ms'],3.6)
            self.assertAlmostEqual(measured['slate_target']['attributed_game_window']['mean_ms'],3.6)
            self.assertAlmostEqual(measured['slate_target']['excluded_editor_windows']['mean_ms'],1.4)
            self.assertTrue(measured['slate_target']['coverage_valid'])

    def test_slate_bound_keeps_shared_work_and_rejects_missing_coverage(self):
        metadata={'scope_version':2,'complete':True,'valid':True,'invalid_pairs':0,'binding_changes':0,
                  'windows':[{'kind':'game','excluded_editor_only':False}]}
        report=slate_client_scope([5,6],[3.5,4],[1,1.2],1,metadata,[1,1])
        self.assertAlmostEqual(report['actual']['mean_ms'],1.75)
        self.assertAlmostEqual(report['unassigned_and_shared_upper_bound']['mean_ms'],.65)
        self.assertTrue(report['timing_met'])
        self.assertIsNone(slate_client_scope([5],[4],[.5],1,None,[1])['actual'])
        self.assertIsNone(slate_client_scope([5,5],[4,4],[.5,.5],1,metadata,[2,0])['timing_met'])
        changed=copy.deepcopy(metadata);changed['binding_changes']=1
        self.assertIsNone(slate_client_scope([5],[4],[.5],1,changed,[1])['actual'])
        self.assertFalse(slate_client_scope([5],[2.5],[1],1,metadata,[1])['timing_met'])
        with self.assertRaises(AssertionError):slate_client_scope([5],[4],[2],1,metadata,[1])

    def test_one_frame_gate_rejects_a_single_extra_latency_peak(self):
        a={'command_displacement_latency':[{'command':900001+i//24,'soldier':i,'displacement_ms':10} for i in range(48)],
           'command_final_receipt_latency_ms':{'900001':10,'900002':10}}
        b=copy.deepcopy(a);b['command_displacement_latency'][-1]['displacement_ms']=40
        result=response_comparison(a,b,16)['command_displacement_latency']
        self.assertLess(result['added_ms']['mean_ms'],16)
        self.assertLess(result['added_ms']['p95_ms'],16)
        self.assertFalse(result['within_response_budget'])

    def test_static_response_requires_both_real_displacements(self):
        a={'response_probe_count':2,'command_displacement_latency':[
            {'command':900001+i,'soldier':i,'displacement_ms':10} for i in range(2)],
           'command_final_receipt_latency_ms':{'900001':10,'900002':10}}
        self.assertTrue(response_comparison(a,copy.deepcopy(a),16)['command_displacement_latency']['within_response_budget'])
        b=copy.deepcopy(a);b['command_displacement_latency'].pop()
        with self.assertRaises(AssertionError):response_comparison(a,b,16)
        a['command_displacement_latency']=[];b=copy.deepcopy(a)
        with self.assertRaises(AssertionError):response_comparison(a,b,16)

    def test_authoritative_timestamps_drive_the_response_gate(self):
        for timing in ('authoritative_mass_write_and_next_tick_observation_v2',
                       'authoritative_mass_write_and_ordered_next_tick_observation_v3'):
            with self.subTest(timing=timing):
                a={'displacement_timing':timing,'response_probe_count':2,
                   'command_displacement_latency':[
                       {'command':900001+i,'soldier':i,'applied_ms':10,
                        'displacement_ms':100,'observation_wait_ms':90} for i in range(2)],
                   'command_final_receipt_latency_ms':{'900001':10,'900002':10}}
                b=copy.deepcopy(a)
                b['command_displacement_latency'][1].update(applied_ms=40,observation_wait_ms=60)
                result=response_comparison(a,b,16)
                self.assertFalse(result['command_displacement_latency']['acceptance'])
                self.assertTrue(result['command_authoritative_application_latency']['acceptance'])
                self.assertFalse(result['command_authoritative_application_latency']['within_one_frame'])
                b['command_displacement_latency'][1]['displacement_ms']=5
                with self.assertRaises(AssertionError):response_comparison(a,b,16)


if __name__=='__main__':unittest.main()
