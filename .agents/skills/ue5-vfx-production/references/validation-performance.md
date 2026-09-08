# Validation And Performance Acceptance

Use this reference for every VFX asset mutation. Validation is evidence that the saved asset, runtime behavior, temporal shape, and expected workload agree with the request.

## Compile, Diagnose, Read Back, Save

For each changed asset:

1. Compile or run the relevant diagnostics before saving.
2. Resolve all errors and review warnings in context. Do not hide or ignore a warning merely to obtain a clean count.
3. Read back the Niagara emitter/module/renderer/parameter structure or the material graph/settings/outputs.
4. Confirm every referenced asset resolves to the intended project-owned path.
5. Save only the explicit changed assets.
6. Re-query after saving so the report reflects persisted state, not only an in-memory command result.

For Niagara, require zero compile errors and verify module stage/order, renderer bindings, user parameter types/defaults, simulation target, and bounds. For materials, require valid diagnostics and verify domain, blend/shading settings, usage, textures/functions, and output connections.

## Temporal Visual QA

Inspect at least three moments:

- Start: latency, spawn origin, initial size/color, attachment, orientation, and first-frame flashes.
- Peak: silhouette, hierarchy, readability against bright/dark backgrounds, scale, overdraw, and gameplay alignment.
- Dissipation: fade curve, lingering particles/decals, abrupt cuts, pooling release, and final cleanup.

Use the asset editor for isolated authoring checks and PIE for runtime timing. Capture screenshots or short time-stamped observations through the installed screenshot/editor service. A single still cannot validate a time-varying effect.

When comparison matters, keep camera, effect parameters, scalability, resolution, and time sample consistent. Record which view and moment each image represents.

## PIE Acceptance

Run PIE when the effect depends on gameplay triggering, animation, sockets, movement, pooling, networking, or teardown:

1. Compile and save the explicit assets before PIE.
2. Reproduce a controlled scenario with known parameters.
3. Exercise normal trigger, repeated trigger, interruption, owner destruction, and extreme distance/angle as applicable.
4. For multiplayer, test listen server, remote client, and dedicated-server behavior.
5. Stop PIE cleanly, inspect output logs, and distinguish pre-existing warnings from task-caused errors.

Do not report editor-preview success as proof of runtime or multiplayer correctness.

## Performance Review

For frequently spawned or screen-filling effects, review and measure:

- simulation target and actual particle count;
- spawn rate/burst count and worst-case concurrent Systems;
- fixed/dynamic bounds and premature culling;
- Effect Type, significance, distance/visibility culling, quality levels, and Scalability response;
- translucent layers, screen coverage, material instruction/texture cost, and light renderers;
- collision, events, Data Interfaces, Simulation Stages, and readback cost;
- component ticks, System lifetime, inactive response, pooling behavior, and cleanup;
- dedicated-server suppression and client reconstruction cost.

Use the same controlled scene and camera for before/after measurement. Prefer Unreal's Niagara debugger/profiling views, `stat Niagara`, `stat GPU`, Unreal Insights, and renderer/material diagnostics as appropriate. Tool availability varies; record the exact measurement source.

Do not invent a universal particle-count or millisecond threshold. Compare against the project's target hardware, frame budget, spawn frequency, and neighboring effects. If no measurement was possible, label the result as a design review rather than a measured optimization.

## Scalability And Failure Modes

- Define what can be reduced first: secondary smoke, sparks, lights, ribbon tessellation, collision, or update frequency.
- Preserve the primary gameplay cue at lower quality: origin, direction, hit location, team/hostility color, and timing.
- Check that culled or deactivated Systems still release pooled components and do not retain references.
- Test bounds with maximum velocity, beam length, ribbon history, and displaced vertices.
- Ensure decals and post-process effects have bounded lifetime and spatial/screen influence.

## Packaging Readiness

- Confirm runtime references cause Systems, materials, functions, meshes, and textures to cook.
- Avoid editor-only modules or assets in runtime paths.
- Validate soft-reference loading and failure fallback.
- Check logs in a packaged Development build for high-risk or dynamically loaded effects when the task scope includes release readiness.

## Completion Report

Report:

- created and modified asset paths;
- source template paths, if any;
- Blueprint/C++ integration points;
- compile and material diagnostic results;
- start/peak/dissipation evidence;
- PIE configurations tested;
- performance measurements or clearly labeled unmeasured review;
- remaining warnings, constraints, and unexecuted steps.

