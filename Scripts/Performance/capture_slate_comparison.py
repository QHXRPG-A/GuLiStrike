"""Compare window invalidation in identical fresh PIE rooms; analyze only afterward."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from capture_live_pie import capture_hotspot, editor, status
from analyze_hotspot_capture import analyze, response_comparison
from hybrid_workload import comparable_census, slate_presentation_comparison


def console_value(name, value=None):
    code = "w=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()\n"
    if value is not None:
        code += f"unreal.SystemLibrary.execute_console_command(w,{(name+' '+str(int(value)))!r})\n"
    code += f"r={{'value':unreal.SystemLibrary.get_console_variable_int_value({name!r})}}"
    result = editor(code)['value']
    if value is not None and result != value:
        raise RuntimeError(name+' did not apply')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--scenario', choices=('static', 'interaction', 'combat'), default='static')
    parser.add_argument('--reverse', action='store_true')
    parser.add_argument('--mask', type=int, default=223)
    parser.add_argument('--variant', choices=('global','indexed','combined'), default='global')
    args = parser.parse_args()
    if status().get('active'):
        raise RuntimeError('A fresh comparison requires the previous PIE to finish')
    args.output.mkdir(parents=True, exist_ok=False)
    names=('Slate.EnableGlobalInvalidation','gs.SceneUI.IndexedGeometry')
    previous = {name:console_value(name) for name in names}
    (args.output/'settings-before.json').write_text(json.dumps(previous))
    settings={'A':(0,0),'B':(
        int(args.variant in ('global','combined')),
        int(args.variant in ('indexed','combined')))}
    sequence = ['A','B']
    if args.reverse:
        sequence.reverse()
    recipe = None
    try:
        for variant in sequence:
            global_value,indexed_value=settings[variant]
            for name,value in zip(names,(global_value,indexed_value)):
                console_value(name,value)
            capture_hotspot(args.output/variant, args.scenario, args.mask, recipe, 15,
                            workers=2, high_load=True, slate_windows=True)
            recipe = recipe or args.output/variant/'recipe.json'
    finally:
        for name,value in previous.items():console_value(name,value)
    insights = Path('D:/UnrealEngine-5.7/Engine/Binaries/Win64/UnrealInsights.exe')
    summaries = {v: analyze(args.output/v, insights, discard=True) for v in ('A', 'B')}
    load = lambda p: json.loads(p.read_text(encoding='utf-8'))
    a, b = summaries['A'], summaries['B']
    assert a['capture']['identity'] == b['capture']['identity'], 'Module/map identity changed'
    contexts = {v: load(args.output/v/'context-before.json') for v in ('A', 'B')}
    ca, cb = (dict(contexts[v]['cvars']) for v in ('A', 'B'))
    for i,name in enumerate(names):
        assert ca.pop(name)==settings['A'][i]
        assert cb.pop(name)==settings['B'][i]
    assert ca == cb, 'A quality or runtime setting changed between variants'
    works = {v: load(args.output/v/'profile-after.json')['hotspot_workload'] for v in ('A', 'B')}
    assert comparable_census(works['A']['simulation_census_start'], works['B']['simulation_census_start'])
    responses = response_comparison(works['A'], works['B'], a['game_thread']['mean_ms'])
    assert all(s['slate_target']['coverage_valid'] for s in summaries.values()), 'Complete game UI attribution missing'
    report = {
        'scenario': args.scenario, 'experiment': args.variant,'settings':settings,'order': sequence, 'variants': summaries,
        'same_identity': True, 'same_starting_authority': True,
        'response': responses,
        'layout_draw_mean_change_ms': b['slate_target']['actual']['mean_ms']-a['slate_target']['actual']['mean_ms'],
        'layout_draw_p95_not_worse': b['slate_target']['actual']['p95_ms'] <= a['slate_target']['actual']['p95_ms'],
        'timing_below_2_5_ms': b['slate_target']['timing_met'],
        'attributed_game_window_p95_not_worse': b['slate_target']['attributed_game_window']['p95_ms'] <= a['slate_target']['attributed_game_window']['p95_ms'],
        'full_pie_mean_change_ms': b['slate_full_pie']['actual']['mean_ms']-a['slate_full_pie']['actual']['mean_ms'],
        'scope': 'Single pair screening of the conservative complete game-client UI bound. Full PIE cost is separate. Dynamic UI correctness and repeated/combined acceptance remain required.'}
    report['presentation_comparison']=slate_presentation_comparison(args.output)
    report['valid_pair']=report['presentation_comparison']['comparable']
    report['eligible_for_acceptance']=report['valid_pair'] and report['timing_below_2_5_ms'] and report['layout_draw_p95_not_worse'] and report['attributed_game_window_p95_not_worse'] and all(
        row['within_response_budget'] for row in responses.values() if row['acceptance'])
    report['acceptance_stage']='single_pair_screening'
    report['final_acceptance']=False
    (args.output/'comparison.json').write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding='utf-8')
    print(json.dumps({k:v for k,v in report.items() if k not in ('variants','response') }), flush=True)


if __name__ == '__main__':
    main()
