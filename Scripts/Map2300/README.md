# Commander 2.3 km terrain / 1.8 km battlefield migration

Implementation scripts for `/Game/Maps/LVL_CommanderMassPrototype`, 81 outposts and 256 Landscape components. These record the approved migration; do not rerun the destructive import stages on the delivered map. The current contract and acceptance boundary live in the [development record](../../Progress/DevelopmentDocumentation/20260922-导航内存优化与对局容量预算.md).

Evidence and recovery root: `Artifacts/Map2300/20260923`. The original map, associated assets and affected source were copied before changes; `baseline.json`, `layout-before.json`, `density-before.json` and `terrain/source-*` capture the former 3.2 km scene. The terrain generator deliberately reads the original 8.16 km height/weights in `Artifacts/Map4200/20260922/terrain/source-*`, avoiding compounded old roads.

Execution order:

1. Inspect the live source editor, PIE and dirty packages; back up, then use `capture_baseline.py`. Preserve existing workspace changes. Compile/restart only under the existing user approval.
2. Export `GuLiResourceAuthoringLibrary.get_initial_army_spawn_layout_json()` into `initial-army-layout.json`: 500 Mass slots and 615 reservations. The shared layout uses the 72500 cm assembly anchors and 4000 cm inward offset.
3. Run `prepare_terrain.py`, `prepare_packed_weights.py`, `prepare_density.py` locally with NumPy/Pillow. Terrain uses 4081² samples and XY Scale 230000/4080. Density retains schema v1 and 25 m cells; native baking samples nine subcell positions. The small base grade maintains reliable Landscape collision traces.
4. Run `replace_landscape.py` then `migrate_scene.py` serially through the editor bridge. Import preserves 256 components and three persistent weight layers; migration preserves all 81 marker/region identities, object scale and prop spacing. These stages guard against accidental repeat execution.
5. Rebuild the builder tree with `gs.Commander.BuildBuilderStateTree`; this touches only `ST_CommanderBuilder`, retaining other trees and Soldiers bindings.
6. Run `bake_ground_and_flight.py`, then `bake_resources.py`. Version 5 requires the exact 200 blue / 40 red / 6240 node budget, central mirrored pairs, 55 m global spacing, 50 m clear roads, full spawn clearance and 500/500 editor slot validation. The sampler prunes conflicting candidate sets without relaxing geometry or density weights; runtime and editor share the 12.5 m boundary constant.
7. `capture_layout.py` uses a temporary editor capture and removes it. Eight views cover overhead, ramps, Commander camera pose and four corners. `inspect_baked_routes.py` checks 768 static source routes using both existing agents. Neither starts gameplay nor instantiates runtime units/obstacles.
8. `save_delivery.py` validates source hashes, prepares final navigation and saves only explicitly managed dirty packages. Reload the saved level and run `readback_scene.py` to check entities, identities, dimensions, budgets, placements, anchors, source hashes, GameMode/Controller and slots.
9. After reload, `export_persisted_weights.py` and `compare_persisted_weights.py` compare all three saved layers against prepared weights. UE normalizes the original summed weights to 255; the comparison allows at most two uint8 levels. Export may dirty the map; save only that known package and confirm clean readback.

Example editor call:

```powershell
python -B Scripts/commander_editor_python.py --file Scripts/Map2300/readback_scene.py --timeout 180 --output Artifacts/Map2300/20260923/readback-response.json
```

Calls must remain serial. A transport timeout does not cancel work on the game thread. PIE, gameplay automation and performance runs require separate authorization. Editor preflight and static navigation do not prove actual spawning, selection, construction, dynamic blocking or memory gains.

No GitHub commit or push is part of this work. The map remains ignored and local. Historical scripts under Map3200/Map4200 and their evidence are retained.
