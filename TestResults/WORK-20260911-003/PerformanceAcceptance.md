# Resource world performance acceptance

Date: 2026-09-11

## Structural budget

| Metric | Result | Evidence |
|---|---:|---|
| Logical ore nodes | 6240 | `ResourceBoardBake.json` and dual-client PIE |
| Unit ore Actors | 0 | ore presentation is owned by one field Actor |
| HISM components | 24 | dual-client PIE `hism_components` |
| Cluster obstacle Actors | 240 authority / 0 client | dual-client PIE |
| Max dirty Recast tiles for one cluster | 9 | 2600cm radius bounds intersect at most 3×3 tiles with `TileSizeUU=5000` |
| Public ore replication granularity | one FastArray item per changed NodeId | `GuLiStrike.Resources.Network.PublicDeltaState` |

The tile count is a geometric upper bound from the configured modifier bounds and tile size. Runtime validation also confirms that consuming the first 39 raw ore keeps the obstacle and consuming raw ore 40 disables only that cluster.

## Timings

| Run | Resource initialization marker | Navigation Ready marker | Observed navigation wait | Frames during wait | Mean tick interval |
|---|---|---|---:|---:|---:|
| two-client listen-server PIE, NullRHI | 07:07:29.427, frame 45 | 07:07:41.115, frame 894 | 11.688s | 849 | 13.77ms |
| Cooked standalone authority, NullRHI | 07:28:15.535, frame 0 | 07:28:21.625, frame 490 | 6.090s | 490 | 12.43ms |

Mass authority spawning followed Ready by 0.018s in PIE and 0.006s in the Cooked run. The controlled Cooked acceptance process reached initialization, navigation Ready, and 500 Mass members in 12.64s wall time.

These mean tick intervals are calculated from log timestamps/frame counters during the asynchronous navigation wait; they are not GPU frame-time measurements because both acceptance runs used NullRHI.

## Network delta result

- Public ore state uses FastArray and adds/changes one item for the changed NodeId; it does not resend the 6240-node initial layout.
- The two-client acceptance confirmed that the same ore delta and public Territory owners reached authority and remote client.
- Inventory, vehicle cargo/control timing, and factory queue remained OwnerOnly in the same run.
- Packet byte totals were not captured; the acceptance boundary is semantic delta granularity and visibility, not transport-byte profiling.

