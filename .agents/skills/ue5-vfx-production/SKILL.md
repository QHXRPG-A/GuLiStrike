---
name: ue5-vfx-production
description: End-to-end UE5.7 VFX production for GuLiStrike. Create or modify Niagara systems, emitters, particle/mesh/decal/post-process materials, combat effects such as impacts, sparks, beams, trails, explosions, afterimages, heatwaves, distortion, and dissolves; integrate them with Blueprint or C++; and validate visuals and performance. Use for 特效, Niagara, 粒子, 火花, 爆炸, 拖尾, 尾焰, 光束, 弹道, 技能表现, 残影, 热波, 折射, 扭曲, 溶解, or VFX optimization. Do not use for ordinary surface materials, lighting-only work, or general scene building without an effect.
---

# GuLiStrike UE5.7 VFX Production

Build production-ready effects with the UE5.7 editor and the project's installed VibeUE 4.0 APIs. Treat gameplay state as authoritative logic and VFX as reconstructable presentation.

## Mandatory GuLiStrike Art Direction

Before creating or changing project VFX, read [GuLiStrike 美术规范](../../../Progress/RequirementDocument/GuLiStrike美术规范.md), especially the explosion identity, approved references, existing-effect exceptions, and review workflow. It is the single source of visual rules. Preserve evolving fire/smoke shapes, pale-yellow cores, orange-red heat edges, readable smoke, sparse sparks and clear shockwaves; a uniformly scaling ball is not sufficient.

For dedicated meshes, follow reference approval → Blender reconstruction approval → UE import. For Niagara/material animation, approve references/storyboards, then build a playable candidate in an isolated UE preview; obtain the required visual approval before replacing combat references. Existing explicit approvals and waivers remain valid; do not re-request them. When approval is missing, show the reviewable result and cite this skill and the standard explaining the pause. Record art revisions and review evidence through `gulistrike-progress`. Compilation, particle count and technical installation are separate from user visual approval.

## Choose The Effect Architecture

- Use Niagara for particles, bursts, trails, beams, multi-emitter timing, procedural motion, or effects that need scalable particle counts.
- Use a material, material instance, mesh, decal, or post process for afterimages, heatwaves, dissolves, screen-space effects, and simple animated surfaces.
- Use a hybrid for hero effects: Niagara orchestrates particles while materials provide emissive, distortion, dissolve, or decal layers.
- Keep collision, damage, cooldowns, targeting, and other gameplay truth outside the effect. A visual collision module is not gameplay authority.
- Preserve working material-based effects. Do not migrate `ABlinkVFX`, `M_BlinkAfterimage`, or `M_BlinkHeatwave` to Niagara unless the user explicitly asks for that migration.

## Load References On Demand

- Read [references/niagara-authoring.md](references/niagara-authoring.md) for systems, emitters, stages, renderers, parameters, Data Interfaces, or Scratch HLSL.
- Read [references/material-vfx.md](references/material-vfx.md) for particle materials, afterimages, heatwaves, distortion, dissolves, decals, or post process.
- Read [references/runtime-integration.md](references/runtime-integration.md) when Blueprint, C++, lifecycle, pooling, networking, or dedicated-server behavior is involved.
- Read [references/validation-performance.md](references/validation-performance.md) whenever modifying an asset or claiming completion.
- Read [references/advanced-niagara.md](references/advanced-niagara.md) only for Stateless Emitters, Simulation Stages, Data Channels, Niagara Fluids, custom Data Interfaces, or unusually large simulations.

## Tool Order And Capability Checks

1. Prefer callable VibeUE MCP tools for Niagara, material, asset, screenshot, and editor operations.
2. In this project, when those MCP tools are not exposed or direct MCP authorization is unavailable, run the read-only capability probe and use `Scripts/ue_exec.py` with the VibeUE Python API on the existing port `12029`.
3. Use generic UnrealMCP for actor placement, Blueprint work, and assigning an already-created material. Do not assume it can author Niagara graphs.
4. If the editor, bridge, or required service is unavailable, continue with C++, Blueprint, or precise manual-editor steps where possible. State exactly what was not applied; never claim an asset changed without readback evidence.

Run the probe before first mutation when service availability is unknown:

```powershell
python Scripts/ue_exec.py .agents/skills/ue5-vfx-production/scripts/probe_vfx_capabilities.py
python .agents/skills/ue5-vfx-production/scripts/probe_vfx_capabilities.py
```

The first command checks the live UE Python process through the project's default bridge. The optional second command checks import-level availability in the current shell interpreter. The probe is introspection-only and must not create, modify, save, compile, or dirty assets.

## Asset Safety And Ownership

- Follow the repository `AGENTS.md`: never read `.uasset`, `.umap`, `.uexp`, `.ubulk`, or `.pak` contents and never search excluded large directories.
- Search for an existing System or material before creating a new one. Inspect its graph or summary before deciding to reuse, duplicate, or replace it.
- Treat `/Game/Assets/VFX` and all `/Game/Assets/**` marketplace content as read-only templates. Duplicate a chosen asset into a project-owned directory before editing it.
- Default to `/Game/GuLiStrike/FX/<Feature>` unless the feature already has a clearer project-owned home.
- Use `NS_` for Niagara Systems, `NE_` for emitters, `M_` for materials, `MI_` for material instances, `MF_` for material functions, and `T_` for textures.
- Save only the explicit asset paths changed during this task. Never use a broad save-all operation as a shortcut.
- For review, explanation, audit, or diagnosis requests, remain read-only. Mutate assets only when the user asks to create, modify, fix, or implement.

## Production Workflow

1. Translate the request into visual beats: trigger, start, peak, decay, duration, attachment, viewing distance, color/value hierarchy, and gameplay cue.
2. Choose Niagara, material-only, post-process, or hybrid architecture. Identify which state belongs to authoritative gameplay and which is cosmetic.
3. Search project-owned effects first, then inspect marketplace assets only as possible templates. Record every source and destination path.
4. Probe the actual editor services. Use only methods confirmed by the installed VibeUE version or its local skill documentation; never invent an API from a newer GitHub revision.
5. Duplicate before modifying vendor content. Create the minimum viable System/material, compile it, and read it back before adding complexity.
6. Author in dependency order: material and textures, emitters/modules/renderers, System parameters, then Blueprint/C++ triggering and lifecycle.
7. Compile and diagnose after each coherent change. Resolve the first root error rather than stacking edits on an invalid graph.
8. Save only confirmed targets. Re-query modules, stages, renderers, parameters, material outputs, and references after saving.
9. Inspect the start, peak, and dissipation of the effect. Run PIE when timing, attachment, pooling, replication, or gameplay triggering matters.
10. Apply the performance acceptance checks appropriate to the expected spawn rate and platform. Report measurements separately from visual judgment.

## Completion Contract

Do not call an implementation complete until all applicable items pass:

- Niagara compiles with zero errors, and modules, stages, renderers, and exposed parameters are read back from the saved System.
- Material diagnostics pass; required textures, blend/shading settings, renderer usage, and output connections match the design.
- Only explicitly modified project-owned assets are saved.
- Visual evidence covers effect start, peak, and dissipation. A single still is insufficient for a time-varying effect.
- PIE verifies any runtime trigger, attachment, lifetime, pooling, or multiplayer behavior.
- Frequently spawned effects have an explicit Pooling, Bounds, Scalability/culling, material-overdraw, and dedicated-server suppression review.
- The final report lists created/modified asset paths, integration points, validation evidence, known limits, and any steps that could not be executed.

## Project Integration Defaults

- The GuLiStrike game module already depends on `Niagara`; do not add duplicate dependencies without checking the current `Build.cs`.
- Follow the existing missile visual-subsystem pattern: the server owns combat truth, while clients reconstruct transient presentation. Replicate compact semantic state or events, not Niagara particles or per-particle state.
- On a dedicated server, skip cosmetic component and System creation unless gameplay code explicitly depends on a nonvisual abstraction.
- Keep Chaos destruction as a separate routed concern. Niagara may visualize Chaos output, but this skill does not own Chaos simulation authoring.
