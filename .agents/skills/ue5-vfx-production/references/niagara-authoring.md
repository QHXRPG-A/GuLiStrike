# Niagara Authoring In GuLiStrike

Use this reference for Niagara Systems, emitters, module stacks, renderers, parameters, Data Interfaces, or Scratch Pad HLSL. The installed VibeUE 4.0 plugin and the live UE5.7 editor are the API authority; names from newer repositories are not evidence that a local method exists.

## Inspect Before Creating

1. Run the capability probe if this is the first VFX operation in the session or editor state is uncertain.
2. Search exact and related names with `NiagaraService.search_systems`.
3. Read candidates with `NiagaraService.summarize`; inspect emitter names, module stages, renderers, user parameters, compile state, and asset path.
4. Prefer a project-owned System that already represents the same gameplay cue. Use marketplace content only as a visual or structural template.
5. If reusing `/Game/Assets/**`, duplicate it to `/Game/GuLiStrike/FX/<Feature>` before any mutation and verify the destination path.

Do not inspect a `.uasset` as a file. Asset Registry queries, VibeUE summaries, editor APIs, screenshots, and compiled diagnostics are the supported inspection paths.

## System Construction Order

Build the smallest compiling effect first, then add layers:

1. Create or duplicate the System.
2. Add one emitter and give it a semantic name.
3. Establish emitter state and spawn behavior.
4. Initialize particle attributes.
5. Add update forces and their solver in a valid order.
6. Add one renderer and its material.
7. Compile, inspect, and save the working baseline.
8. Add secondary emitters, user parameters, events, or Scratch modules one coherent unit at a time.

Useful installed service operations include:

- `NiagaraService.create_system`, `search_systems`, `summarize`, `add_emitter`, `list_emitters`, `get_all_editable_settings`, `compile_with_results`, and `save_system`.
- `NiagaraEmitterService.list_modules`, `add_module`, `list_renderers`, `add_renderer`, `get_module_input`, and `set_module_input`.
- `NiagaraScratchPadService.create_scratch_module`, node/pin creation, `add_custom_hlsl_node`, `connect_pins`, and `apply_changes`.

Confirm signatures from the installed plugin documentation before calling them. In VibeUE 4.0, `add_module` requires System path, emitter name, module asset path, and stage; do not omit the stage or guess a module path.

## Module Stages And Ordering

Place a module according to what it initializes or changes:

| Stage | Typical responsibilities |
| --- | --- |
| Emitter Spawn | One-time emitter initialization |
| Emitter Update | emitter state, spawn rate, bursts, looping behavior |
| Particle Spawn | initialize position, lifetime, velocity, color, sprite/mesh attributes |
| Particle Update | particle state, forces, drag, collision, color/size over life |
| Simulation Stage | iterative GPU work that intentionally reads/writes simulation data |

Within Particle Update, accumulate forces before `Solve Forces and Velocity`. Put lifetime-dependent scale/color modules after the attributes they read have been initialized. Verify actual stack order after each mutation; a successful API response does not prove the module landed in the intended stage.

Use module search/details APIs rather than guessing asset paths. If several scripts share a display name, resolve by full path, usage context, exposed inputs, and Niagara version compatibility.

## Renderers

- Sprite Renderer: sparks, smoke cards, flashes, motes, and camera-facing particles.
- Mesh Renderer: debris, shards, projectiles, stylized chunks, and volume-implying particles.
- Ribbon Renderer: trails, lightning, slashes, streams, and persistent paths.
- Light Renderer: reserve for a small number of important lights; it can be expensive and visually unstable at scale.

Create the renderer only after its required particle attributes and material exist. Read back renderer type, material path, alignment/facing, sort mode, binding names, and enabled state. A renderer with a missing or incompatible material is not a completed emitter.

## Parameters And Data Flow

Choose parameter scope deliberately:

- Use `User.*` parameters for values supplied by gameplay or designers: color, scale, intensity, lifetime multiplier, target position, beam endpoints, mesh, or texture.
- Use emitter/particle parameters for internal simulation state.
- Use rapid-iteration parameters for authored module defaults that do not need a public runtime contract.
- Use a Data Interface when the value represents structured engine data or a data source rather than a scalar/vector setting.

Give public parameters meaningful nonzero defaults so the System previews safely without runtime setup. When an input name occurs in multiple stages or emitters, update the intended occurrence explicitly and read back all matches.

Common Data Interface uses include scene queries, skeletal mesh sampling, spline sampling, render targets, grids, audio, and custom gameplay data. Confirm CPU/GPU support, thread ownership, update frequency, and packaged-build availability before selecting a DI. Never use a visual DI as the sole source of authoritative hit or damage state.

## Scratch Pad And Custom HLSL

Use a Scratch module when stock modules cannot express the behavior cleanly or when one reusable effect-specific transform replaces an unwieldy stack.

1. Define the target stage and execution behavior.
2. Create the Scratch module and typed input/output pins.
3. Add nodes or a Custom HLSL node only for the minimal missing math.
4. Connect every pin deliberately, apply changes, add the module to the correct stage, and compile.
5. Reopen/read back the script and System; capture diagnostics before saving.

Avoid HLSL for basic multiply, lerp, curve, or coordinate operations already covered by stable modules. Document coordinate space, units, fallback values, GPU assumptions, and any renderer binding expected by custom code.

## CPU, GPU, Bounds, And Determinism

- Prefer CPU simulation when low counts need CPU-only Data Interfaces, precise event handling, or CPU-side inspection.
- Prefer GPU simulation for large independent populations after confirming required modules and DIs support it.
- Set and validate fixed bounds for GPU emitters and fast-moving effects. Test the effect at extreme parameter values and camera angles for premature culling.
- Seed deterministic variation only when repeatability matters. Visual randomness should not be mistaken for gameplay randomness.
- Do not convert between simulation targets merely to chase a general rule; profile the representative effect and platform.

## Readback Checklist

Before declaring the System done, confirm:

- the saved asset path is project-owned;
- every emitter is present and enabled as intended;
- every module is in the correct stage and order;
- every renderer has compatible bindings and materials;
- user parameters have correct type and default;
- compile results contain zero errors;
- bounds and scalability behavior match the expected spawn context.

