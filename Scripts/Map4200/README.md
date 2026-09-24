# Commander 4.2 km migration

This is the one-time migration of `/Game/Maps/LVL_CommanderMassPrototype` recorded in [the development handoff](../../Progress/DevelopmentDocumentation/20260922-导航内存优化与对局容量预算.md). It has already been applied and saved. Do not rerun the destructive stages against the delivered map.

The evidence and recovery root is `Artifacts/Map4200/20260922`. `backup/` contains the pre-migration map, associated assets and source files; `terrain/source-*` contains the original height and weight exports. Preserve these before changing the migration.

Execution order used:

1. `capture_baseline.py` in the live source editor; copy the original map and managed assets before replacement.
2. `prepare_terrain.py` and `prepare_density.py` in local Python with NumPy/Pillow. These prepare imports without opening binary UE assets.
3. After approved native compilation and restart, `replace_landscape.py` then `migrate_scene.py` in the editor. The replacement explicitly requires the original 1024-component landscape.
4. Terrain refinements use `refine_roads_and_notify_bounds.py` on the existing replacement landscape, followed by `bake_ground_and_flight.py` and `bake_resources.py`. Imported terrain and navigation bounds must be up to date before resources are baked.
5. `capture_layout.py` captures temporary editor views and removes its capture actor. `inspect_baked_routes.py` reads static source navigation without entering gameplay.
6. `save_delivery.py` saves only the five explicitly managed packages; `readback_scene.py` checks the saved entities and native source signatures. The final readback and route inspection were repeated after restarting the latest source build.

The subsequent `tighten_flight_bounds.py` refinement was run once after a separate stage backup. It replaces the initial 278300 cm horizontal half extent with 262300 cm and moves the lower Z from -33884.765625 cm to -25000 cm. Its guard expects the initial extent, so it deliberately cannot be repeated after application. The initial migration now uses the corrected horizontal envelope formula directly. The dedicated refinement script remains a record of the already performed second stage.

Editor scripts run serially through `Scripts/commander_editor_python.py --file <script> --timeout <seconds> --output <response.json>`. A transport timeout does not cancel work on the game thread: inspect completion before sending another bake. Never execute editor calls concurrently.

No script authorizes compiling, shutting down unrelated editor sessions, PIE, automated gameplay tests, or performance runs. Compilation and this editor's restart were explicitly approved for the recorded migration. Current visual captures show authored terrain only; resource actors and units are spawned by the normal gameplay flow.
