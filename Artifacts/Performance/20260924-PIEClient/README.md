# PIE client frame capture — 2026-09-24

## Scenario and capture

- UE 5.7 editor, `LVL_CommanderMassPrototype`, embedded PIE with one client and an in-process server.
- Captured from UnrealEditor PID 71460 through its local Trace control port. Channels: CPU, frame, GPU, bookmark, log, counter. No graphics quality or gameplay settings were changed.
- Trace transport was active for 9 seconds. The `.utrace` file also contains earlier buffered events; all numbers below use the final 9 seconds only.
- This interval contains 384 game-frame starts and 383 complete world-tick groups. For game/render frame interval percentiles, the first 10 and final 5 intervals were excluded, leaving 368 samples. GPU frame percentiles use 383 samples.
- Durations use the trace's 10 MHz timestamp clock. Pass and nested scope times overlap; do not add inclusive times together.

## Frame timing

| Item | Median | P95 |
| --- | ---: | ---: |
| PIE process game-frame interval | 21.84 ms | 33.13 ms |
| GPU graphics-queue frame | 14.02 ms | 15.60 ms |
| Client `UWorld_Tick` | 6.36 ms | 7.71 ms |
| Server `UWorld_Tick` | 7.19 ms | 17.93 ms |
| Editor-world `UWorld_Tick` | 0.18 ms | 0.24 ms |

The three world ticks were separated by order within each game frame. The client world is identified by one `GuLiCommanderPresentation_Interpolation` call per tick; the server world contains `GuLiCommanderMassStateTreeProcessor_0` and predictive-avoidance work.

## Client GameThread costs

Average per client world tick over the 383 complete ticks:

| Scope | Inclusive | Exclusive | Calls |
| --- | ---: | ---: | ---: |
| `ProcessUntilTasksComplete` | 4.90 ms | 1.45 ms | 2,681 |
| `NS_MiningLaser_Green` | 0.89 ms | 0.89 ms | 770 |
| `UCharacterMovementComponent_TickComponent` | 0.73 ms | 0.46 ms | 12,256 |
| `UChannel_ReceivedSequencedBunch` | 0.57 ms | 0.18 ms | 7,729 |
| `GuLiCommanderPresentation_Interpolation` | 0.48 ms | 0.48 ms | 383 |
| `GuLiCommanderPresentation_RebuildLocalInstances` | 0.62 ms | 0.04 ms | 383 |
| `NS_Flash` | 0.20 ms | 0.20 ms | 1,076 |
| `GuLiCombatEffects_Presentation` | 0.15 ms | 0.14 ms | 383 |
| `GameNetDriver` | 0.14 ms | 0.14 ms | 383 |

`ProcessUntilTasksComplete` includes nested task work and waiting; its 1.45 ms exclusive portion is not the full 4.90 ms. `GuLiCommanderPresentation_RebuildLocalInstances` includes the interpolation scope, so those inclusive values overlap.

## GPU costs

Graphics-queue pass averages per PIE game frame: `SceneRender` 7.88 ms inclusive, `RenderVelocities` 1.19 ms, `BasePass` 1.01 ms, `ShadowDepths` 0.55 ms, `Nanite::DrawGeometry` 0.48 ms, `LumenSceneUpdate` 0.38 ms, and `PostProcessing` 0.37 ms. These are nested pass timings and should not be summed.

## Interpretation and limits

The client world tick is relatively stable while the server world tick has a much higher P95. `GuLiCommanderMassStateTreeProcessor_0` averages 1.80 ms per server tick over this window. Editor Slate work also appears in the overall process frame (`Slate::Prepass` 1.83 ms and `Slate_PaintSlowPath` 1.70 ms average per game frame). This capture does not establish packaged-client performance.

Evidence: `client-9s.utrace`, `summary-tail9.json`, and `world-attribution-tail9.json`. The two Python scripts in this directory document the aggregation method. The engine source was restored after a temporary local change to the TraceAnalyzer text converter, with SHA-256 `4821BB31164A9C5B152F51B4AC0A6CCD62C00F98330906A8300B6C6EDAF86487`.
