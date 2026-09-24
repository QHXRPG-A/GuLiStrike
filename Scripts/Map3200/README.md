# Commander 3.2 km terrain / 2.7 km battlefield migration

The approved migration of `/Game/Maps/LVL_CommanderMassPrototype` to 81 outposts has been applied and saved. This folder records the implementation, not a command to repeat it on the delivered map. The [development record](../../Progress/DevelopmentDocumentation/20260922-导航内存优化与对局容量预算.md) preserves the previous 4.2 km phase and the current verification boundary.

Recovery/evidence root: `Artifacts/Map3200/20260923`. `backup/` contains the previous map and managed assets; `source-before/` preserves existing workspace text changes. `baseline.json`, `layout-before.json`, `density-before.json` and `terrain/source-*` describe the scene before this migration. The original 8.16 km height and three weight layers are in `Artifacts/Map4200/20260922/terrain/source-*`; terrain preparation intentionally reads those originals to avoid rescaling the 7×7 roads.

Recorded execution order:

1. Back up files and capture the live source editor with `capture_baseline.py`. Inspect PIE and dirty packages before any shutdown. Compile/load the native changes under the user's existing approval.
2. Export `unreal.GuLiResourceAuthoringLibrary.get_initial_army_spawn_layout_json()` as `initial-army-layout.json` in the evidence root. It reads the same initial 500-slot calculation used at runtime, plus the engineering/factory/outpost reservations. Local preparation uses its XY footprint, so it can run before terrain replacement.
3. Run `prepare_terrain.py`, `prepare_packed_weights.py` and `prepare_density.py` in local Python with NumPy/Pillow. Their output is numerical data, PNGs and an interleaved uint8 weight import; they do not open binary UE asset contents. The final terrain includes a 0.1% base grade because collision traces missed entirely flat base tiles in this build.
4. Run `replace_landscape.py` in the editor. It requires the backed-up 4.2 km extent, reuses the 256 components, registers persistent target layers through `GuLiLandscapeAuthoringLibrary`, imports height/weights and updates navigation bounds. `migrate_scene.py` requires exactly 49 captured markers, preserves their identities and migrates scene content. These stages deliberately refuse the already delivered 81-marker/3.2 km state.
5. `reimport_prepared_height.py` records the local terrain correction used before successful migration. It imports the final prepared height and notifies the navigation system. Do not rerun it unless intentionally revising terrain; then all dependent navigation/resources need validation again.
6. Run `bake_ground_and_flight.py`, then `bake_resources.py`. Resource preparation adds the 32 new outposts and canonical regions, applies version 4, and requires 500/500 initial slots to pass before writing the asset. Keep the full 200/40/6240 budget.
7. `capture_layout.py` captures eight temporary editor views and removes its capture actor. `inspect_baked_routes.py` reads 768 static source routes without gameplay. These do not instantiate runtime units, resource obstacles or buildings.
8. `save_delivery.py` validates resources, prepares final navigation and saves only explicitly managed packages that are dirty. `readback_scene.py` checks entities, identities, resource totals/pairs/grounding, actual GameMode/Controller, 500 slots, bounds and signatures. It writes the post-terrain spawn layout separately as `initial-army-layout-after.json`. The saved map was reloaded and read back again with no dirty packages.

The subsequent weight persistence repair is recorded by `restore_persistent_weights.py`. The older VibeUE `add_layer` implementation only populates `LandscapeInfo::Layers`; UE 5.7 requires persistent `ALandscape::TargetLayers` registration. Also, sequential paint imports rebalance previously imported layers. The native editor helper registers persistent layers and imports all channels together through the engine's packed-weight API, leaving the material appearance intact. After any such repair, refresh navigation, save, reload, run `export_persisted_weights.py`, and compare all three exported weight arrays against `terrain/weight-3200-*.png`. A successful import call or populated transient layer list alone is insufficient evidence of saved weights.

Editor calls run serially through:

```powershell
python -B Scripts/commander_editor_python.py --file Scripts/Map3200/readback_scene.py --timeout 120 --output Artifacts/Map3200/20260923/readback-response.json
```

A transport timeout does not cancel work on the editor game thread; inspect completion before sending another command. Never run editor calls concurrently. The migration scripts assert the target map and no PIE. Their presence does not authorize compiling, shutting down other editor sessions, PIE, automated gameplay tests or performance runs.

`compare_persisted_weights.py` performs the local pixel comparison after export. The final comparison retained every nonzero source pixel in all three layers; maximum difference was 2 uint8 levels from the engine's normalization of original sums 253..257 to 255. The export API can mark the map dirty even while reading; save only the known target map afterward and run the entity readback to confirm the final dirty-package state.

No GitHub commit/push was performed. The map remains ignored and local. The 500-slot authoring check is evidence of spawn clearance; actual Mass creation, synchronization, selection, commands, dynamic obstacles and gameplay remain pending PIE authorization and acceptance. Memory/capacity gains require dedicated measurements.
