# Ship Batch04 — Remaining original-mesh surface styling

User instruction: 全都处理了，燃烧弹发射舱 需要有个火焰的标志。

Four independent components: Bottom_Twin_Barrel_Turret, High_Rate_Fire_Cannon, Incendiary_Bomb_LaunchBay, Missile_Bay.

Preserve the original mesh, normals, dimensions, transforms, rig and sockets. Only edit surface colors/textures, internal lines, switchable outline and cel lighting. Flame signage is flat paint on existing exterior armor.

Workflow: actual original views → reference effect + front/right/rear → user A → actual original-mesh material work → user B → Blender/FBX export and existing readback. Art revision 1.0. Scope ends at Blender/FBX; formal UE integration is not run.

- `Source/source_snapshot_v1.json`: successful read-only UE capture; original packages unchanged.
- `Source/prototype_render_manifest_v1.json`: four preserved authoring sources, four inspection blends and twenty actual view hashes.
- `References/`: image-generation prompts, unmodified source outputs and selected reference sheets.
- `Review_A_v1.html`: Bottom_Twin v1, High_Rate v2, Incendiary v1, Missile_Bay v1 and expandable original-model views; user A pending. High_Rate v2 unifies the right-view cheek's steel-blue color; v1 is preserved.
- Actual production textures/Blender candidates, user B and final FBX: not started.

The two guns' frozen current rig meshes have 1684/1286 triangles; older static originals have 1240/920. This predates the surface task. Two static bays have 3978/368 triangles. Bones remain Root → BarrelPitch for the guns; mesh socket counts are 2/2/5/0.

Batch02's existing actual-material v2 review and export are tracked separately. Scope authorization does not change any previous A/B record. Six independent models from batches 01/03 are already delivered, three in batch02 await B/export, and these four await reference A.
