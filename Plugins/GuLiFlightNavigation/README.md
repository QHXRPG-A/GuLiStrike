# GuLi Flight Navigation

This plugin provides deterministic, pre-baked three-dimensional navigation without depending on GuLiStrike gameplay types.

## Modules

- `GuLiFlightNavigationRuntime`: map data, navigation volume, immutable query snapshot, connected-component gate, synchronous/worker-thread A*, and conservative path smoothing. Its only module dependencies are `Core`, `CoreUObject`, and `Engine`.
- `GuLiFlightNavigationEditor`: deterministic collision bake, adaptive octree generation, face portals, connected-component labelling, asset validation, and Bake/Validate/Clear editor APIs.

## Authoring flow

1. Create a `GuLiFlightNavigationData` Data Asset.
2. Place a `GuLiFlightNavigationVolume` and assign the asset.
3. Call `GuLiFlightNavigationEditorLibrary::BakeVolume` from an Editor Utility Blueprint or C++ tool.
4. Save the map and data asset, then run asset validation. Invalid or stale checksum data emits a cook error when referenced.

Each map should use its own data asset. Re-baking increments `DefinitionRevision`; the geometry signature and content checksum remain deterministic for identical source topology and settings. Runtime code copies a validated asset into an immutable graph once per checksum, so an async query never touches a `UObject`.

## Runtime API

- `UGuLiFlightNavigationSubsystem::FindPath` is the Blueprint-friendly synchronous entry point.
- `UGuLiFlightNavigationSubsystem::FindPathAsync` returns a `TFuture` and accepts a thread-safe cancellation token.
- `FGuLiFlightNavigationQuery` also exposes containing-cell and segment-navigability queries.

The bake inflates collision tests by `AgentRadius`. A runtime query may use the baked radius or a smaller one; larger agents are rejected instead of receiving an unsafe path.

## Tests

Run `Automation RunTests GuLi.FlightNavigation`. Tests cover deterministic baking, obstacle detours, disconnected components, checksum mutation, agent-radius gating, async A*, and cancellation.
