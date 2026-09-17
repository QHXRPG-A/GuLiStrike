# Source UE 5.7 patches

## Passive Detour obstacles

`UE5.7-Detour-passive-obstacle-proximity.patch` fixes a runtime CrowdManager cost in the source engine at `D:/UnrealEngine-5.7`.

GuLi's Mass soldiers register as external crowd obstacles with `collisionQueryRange == 0`. UE still requested a shared wall-boundary sample for each one every frame. A moving zero-radius sample cannot reuse the previous location; the cache performs repeated linear scans. At 1200 soldiers this dominated the server game thread.

The patch skips those agents' own neighbour/wall queries **after every agent has entered the proximity grid**. Vehicles with a positive collision-query range continue to see and avoid the same soldiers. It does not change NavMesh baking, tile settings, soldier movement, or population limits.

For a fresh source engine checkout, first check and apply from that engine's repository root:

```powershell
git apply --check D:/UE5.7/test1/Scripts/EnginePatches/UE5.7-Detour-passive-obstacle-proximity.patch
git apply D:/UE5.7/test1/Scripts/EnginePatches/UE5.7-Detour-passive-obstacle-proximity.patch
```

If `git apply --reverse --check` succeeds instead, the patch is already installed. Do not apply it twice. Rebuild the project's Editor and Game targets using this engine, then compare the engine/project/plugin BuildIds.

Evidence: [crash and crowd investigation](../../TestResults/AccessRampCrash-20260916/REPORT.md).
