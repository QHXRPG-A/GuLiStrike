"""Whole loaded build comparison, without reverting source or compiled code.

Capture 'before' while the old editor process is still loaded, then 'after'
after the authorized source build/restart. The user's earlier 15-20 FPS is an
approximate historical observation, distinct from this fixed 600/500 fixture.
"""
from __future__ import annotations
import argparse, hashlib, json, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Scripts'))
from Performance import run_four_stage_review as review

OUT = ROOT / 'outputs/performance/20261009-all-optimizations'
BASE_LAUNCH = review.launch

FIXTURE = r"""
ws = unreal.EditorLevelLibrary.get_pie_worlds(True)
server = ws[0]
marker = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(server, unreal.Actor)
              if unreal.Name('FlightEventsQAOrigin') in a.tags)
origin = marker.get_actor_location()
rows = []
for w in ws[1:]:
 for role, label, effect_id, offset in [
  ('Mining', 'PerfReview_Mining_Opaque', 36, -2000),
  ('Construction', 'PerfReview_Construction_Opaque', 45, 2000),
  ('Muzzle', 'PerfReview_Flash_Source', 52, 0),
  ('Impact', 'PerfReview_Flash_Impact', 5, 4000)]:
  actor = next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w, unreal.GuLiPerformanceReviewActor)
               if a.get_actor_label() == label)
  definition = unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(effect_id)
  asset = definition.resource_path
  assert asset, (effect_id, str(definition.resource_path))
  actor.set_editor_property('System', asset)
  actor.set_editor_property('EffectCount', 16 if role in ('Mining','Construction') else 16)
  scale = definition.scale
  actor.set_editor_property('BaseScale', scale)
  actor.set_actor_location(unreal.Vector(origin.x + 15000 + offset, origin.y,
                                         origin.z + 3000), False, True)
  actor.start_comparison()
  rows.append({'world':w.get_path_name(), 'role':role, 'id':effect_id,
               'path':asset.get_path_name(), 'count':16, 'scale':str(scale)})
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'fixtures':rows}))
"""

def launch(case, version):
    setup = BASE_LAUNCH('runtime', 'optimized_controls')
    setup['whole_effects'] = review.run(FIXTURE)
    setup['comparison'] = 'Whole loaded build; all previously enabled optimizations remain on.'
    return setup

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--version', choices=['before','after','final'], required=True)
    ap.add_argument('--samples', type=int, default=3)
    ap.add_argument('--first-sample', type=int, default=1)
    args = ap.parse_args()
    directory = OUT / args.version
    directory.mkdir(parents=True, exist_ok=True)
    review.OUT = directory
    review.launch = launch
    receipt = {'version':args.version, 'historical_user_fps':[15,20],
               'historical_scope':'User observation; no recovered pre-plan build.',
               'module_metadata':json.loads((ROOT/'Binaries/Win64/UnrealEditor.modules').read_text()),
               'modules':{}, 'effects_source':json.loads((ROOT/'Data/Json/DT_GuLiStrikeVfx_Effects.json').read_text())}
    for name in ['UnrealEditor-GuLiStrike.dll','UnrealEditor-GuLiStrikeEditor.dll']:
        receipt['modules'][name] = hashlib.sha256((ROOT/'Binaries/Win64'/name).read_bytes()).hexdigest()
    (directory/'loaded-build.json').write_text(json.dumps(receipt,ensure_ascii=False,indent=2),encoding='utf-8')
    results = []
    try:
        for sample in range(args.first_sample,args.first_sample+args.samples):
            prior = set((directory/'paired').iterdir()) if (directory/'paired').exists() else set()
            record = review.capture('runtime', sample, args.version)
            record['scope'] = 'Whole loaded build, same process dedicated server + two 1280x720 clients, 600 moving units, four sources x125 flights, 32 continuous tool beams and 32 muzzle/impact effects per client. No rollback controls or restored code.'
            created = set((directory/'paired').iterdir()) - prior
            assert len(created)==1, created
            path = created.pop()/'result.json'
            record['source_directory'] = path.parent.name
            path.write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
            results.append(record)
    finally:
        name = 'whole-results.json' if args.first_sample==1 else f'whole-results-additional-{args.first_sample}.json'
        (directory/name).write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
        review.stop()

if __name__ == '__main__':
    main()
