"""Captured tuning consumed by the local Mass editor readback."""
BASE = {'gs.Avoidance.LookaheadSeconds':2.5, 'gs.Avoidance.DistanceFirst':0,
        'gs.Avoidance.MaxCandidates':6, 'gs.Avoidance.SharedIndex':0,
        'gs.Avoidance.ReuseZero':0, 'gs.Avoidance.CacheMaxSeconds':.3,
        'gs.Units.MassTurnScale':1}
AVOID = dict(BASE, **{'gs.Avoidance.LookaheadSeconds':.5, 'gs.Avoidance.DistanceFirst':1,
                     'gs.Avoidance.SharedIndex':1, 'gs.Avoidance.ReuseZero':1})
TURN = dict(BASE, **{'gs.Units.MassTurnScale':3})
FINAL = dict(AVOID, **{'gs.Units.MassTurnScale':3})

if __name__ == '__main__':
    raise SystemExit('Use run_snapshot_parallel_review.py for the current implementation.')
