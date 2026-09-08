# Material-Based VFX

Use this reference for particle materials, animated mesh effects, afterimages, heatwaves, refraction/distortion, dissolves, decals, and post-process effects. A material-only solution is often simpler and more stable than Niagara when no particle population or emitter timing is needed.

## Choose The Material Domain

- Surface material: animated meshes, ghost silhouettes, projectile shells, and geometry-based dissolves.
- Niagara sprite/mesh/ribbon material: renderer-driven particles with particle attributes or dynamic parameters.
- Deferred decal: impacts, scorch marks, splashes, and surface-local marks where decal projection is acceptable.
- Post Process: full-screen or camera-local treatment such as damage feedback, blink transition, color separation, or screen distortion.
- Light Function or volume-specific material: only when the requested effect truly operates through that rendering path.

Keep existing `M_BlinkAfterimage` and `M_BlinkHeatwave` material architecture unless a requested result requires a different system.

## Inspect Before Editing

1. Query material metadata and settings with `MaterialService.get_material_info` or the equivalent installed method.
2. Export/read the graph with `MaterialNodeService.export_material_graph`.
3. Record material domain, blend mode, shading model, two-sided flag, usage flags, parameters, texture references, and output connections.
4. Inspect existing instances and renderer consumers before renaming parameters or changing the material contract.
5. Use graph diagnostics, not only a texture-usage query; static switches and functions can hide relevant paths.

Use installed VibeUE service documentation as the method-signature authority. Relevant capabilities include material creation, node/expression creation, output connection, graph export, diagnostics, compilation, and targeted save.

## Core Patterns

### Particle Materials

- Use Unlit when lighting interaction is unnecessary; drive visibility through Emissive and Opacity/Opacity Mask.
- Select Translucent, Additive, Alpha Composite, or Masked based on the visual target and overdraw budget, not habit.
- Bind particle color, normalized age, dynamic parameters, position, velocity, or ribbon attributes only when the renderer actually provides them.
- Enable the correct Niagara/sprite/mesh usage and recompile after assigning the renderer material.

### Afterimage

- Use a poseable or captured mesh with an Unlit translucent/additive material.
- Drive opacity and emissive decay through a material instance parameter or curve.
- Freeze or intentionally update the source pose; accidental continued animation breaks the silhouette read.
- Cap overlapping ghosts and destroy or return them to a pool after fade completion.

### Heatwave And Distortion

- Distort screen/scene sampling with low-frequency noise plus a controlled mask.
- Keep UV displacement small, stable across resolution, and zero outside the effect mask.
- Guard against unsupported refraction paths and test foreground/background edges, temporal artifacts, and split-screen or resolution scaling if relevant.
- Prefer a project-local mesh or post-process volume/material contract that can be parameterized at runtime.

### Dissolve

- Combine a scalar threshold with texture/noise and a controllable edge band.
- Route the body through Opacity Mask or Opacity according to the domain and blend mode.
- Create an emissive edge from two nearby thresholds; expose threshold, edge width, color, and intensity.
- Validate shadow behavior, depth sorting, Nanite compatibility, and whether collision/gameplay visibility changes separately from the visual dissolve.

### Decal

- Expose size, lifetime, fade, color, and normal/roughness influence.
- Check receiver compatibility and DBuffer/deferred constraints for the project renderer.
- Prevent unbounded accumulation through lifetime, pooling, distance culling, or a strict active-count policy.

### Post Process

- Mask the effect spatially or temporally unless a full-screen treatment is intended.
- Expose blend strength and time controls to Blueprint/C++.
- Verify the chosen blendable location and scene texture inputs in UE5.7.
- Test with UI, exposure, motion blur, upscaling, and camera cuts when those systems are present.

## Runtime Parameters

- Use a Material Instance for authored variants and a Dynamic Material Instance for per-instance runtime animation.
- Keep parameter names stable and semantic, such as `EffectAlpha`, `EdgeWidth`, `Tint`, `Intensity`, or `DistortionStrength`.
- Initialize every runtime parameter before the effect becomes visible; avoid one-frame default flashes.
- Use Niagara User parameters or renderer bindings when particle material values differ per System instance.

## Texture And Asset Safety

- Import or duplicate textures into `/Game/GuLiStrike/FX/<Feature>` and use the `T_` prefix.
- Never modify a marketplace texture or material in place.
- With VibeUE, use its safe asset-management/import path. Do not invoke raw `AssetTools.import_asset_tasks` through the bridge when the installed plugin documents a safer importer.
- Verify compression, sRGB, alpha, tiling, mip behavior, and virtual-texture compatibility for the actual use.

## Diagnostics And Completion

After graph edits:

1. Run material diagnostics and compile.
2. Read back output connections, parameters, functions, texture paths, and material settings.
3. Assign the material to the intended Niagara renderer, mesh, decal, or post-process consumer and re-read that consumer.
4. Save only the material, instances, functions, and imported project-owned textures changed by the task.
5. Inspect start, peak, and fade states. Check sorting, overdraw, clipping, depth intersections, temporal stability, and extreme parameter values.

