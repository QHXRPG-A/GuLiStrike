# Advanced Niagara Routing

Load this reference only when the request or measured bottleneck needs Stateless Emitters, Simulation Stages, Niagara Data Channels, Niagara Fluids, a custom Data Interface, or another advanced simulation path. Do not add these systems merely to make a simple combat effect appear sophisticated.

## Stateless Emitters

Consider Stateless Emitters for large quantities of comparatively simple, independent visual particles when their supported feature set matches the effect. In UE5.7:

- inspect actual module and renderer compatibility in the current editor;
- preserve a compiling fallback when a needed stateful feature is unsupported;
- compare spawn/update cost and visual parity in the same scenario;
- validate parameter exposure, bounds, Scalability behavior, and packaging.

VibeUE 4.0 has no assumed automatic stateful-to-stateless conversion contract. If a local method is not discovered and verified, use supported editor/manual work or state that the conversion was not applied. Never invent a conversion API.

## Simulation Stages

Use Simulation Stages for intentional iterative GPU computation or grid/data processing that cannot be represented cleanly by the normal spawn/update stack.

- Define iteration source, count, read/write targets, execution index, and stage dependencies.
- Confirm every Data Interface supports the GPU access pattern.
- Keep barriers and memory footprint in mind; fewer particles do not automatically mean a cheap grid simulation.
- Validate bounds, reset/initialization, deterministic needs, and fallback quality levels.
- Profile each stage rather than attributing the entire System cost to Niagara generically.

## Niagara Data Channels

Use Data Channels when many producers and consumers need a structured, decoupled stream of transient world data, such as impacts or crowd events.

- Define the schema, writer ownership, spatial query, lifetime, capacity, and frame semantics.
- Avoid replacing a simple function call or a single actor reference with a global channel.
- Treat channel data as presentation input unless authoritative gameplay independently owns and validates it.
- Test missing writers, late consumers, world transitions, network separation, and overload behavior.

## Niagara Fluids

Use Niagara Fluids for effects whose visual identity depends on fluid-like grid simulation and whose target hardware can support it. Keep a cheaper authored fallback for scalability.

- Establish grid resolution, world size, update rate, bounds, source injection, and renderer path.
- Measure GPU memory and frame cost in the representative camera and concurrency scenario.
- Validate warm-up, reset, teleport, culling, and transition between quality levels.
- Do not use a fluid simulation for ordinary smoke, fire, or shockwaves when sprites/meshes achieve the required read at much lower cost.

## Custom Data Interfaces And Engine Work

A custom Data Interface crosses into C++ architecture and render-thread concerns. Route implementation through `ue5-cpp-gameplay` and `ue5-architecture` as secondary skills, then return here for System integration and VFX validation.

Specify:

- UObject/game-thread data ownership;
- per-instance data and lifetime;
- render-thread proxy and transfer policy;
- CPU virtual-machine functions and GPU HLSL support;
- serialization, versioning, editor exposure, and packaging;
- synchronization, allocation, and profiling strategy.

Do not modify engine or plugin source unless the user explicitly expands the task scope.

## Chaos Boundary

Chaos destruction, Geometry Collections, fields, and physical fracture remain a separate concern. This skill may consume Chaos events to produce dust, sparks, trails, or impacts, but it does not author or tune the authoritative Chaos simulation. Route that portion to the most appropriate architecture/gameplay/scene skill and keep Niagara as the presentation layer.

## Unsupported Tooling

Do not emit calls for GitHub-only UE5.8 agents, UCP, DCC bridges, or other services that are not installed and verified in this project. Use the local UE5.7 editor, VibeUE 4.0, generic UnrealMCP within its proven scope, or explicit manual/C++ fallback steps.

