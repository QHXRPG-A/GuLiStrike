"""Adapt the vendor lightning flipbook to a frozen pair of world positions."""
import ast
import hashlib
import json
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
SOURCE = "/Game/Assets/ExternalPacks/NiagaraRealisticStarter/Realistic_Starter_VFX_Pack_Niagara/Niagara/Environment/NS_Environment_Lightning"
DEST = "/Game/GuLiStrike/FX/GroundMech/Electromagnetic/NS_GroundElectromagneticArc"
SOURCE_MATERIAL = SOURCE.rsplit("/Niagara/Environment/", 1)[0] + "/Materials/M_Lightning"
ARC_MATERIAL = DEST.rsplit("/", 1)[0] + "/M_GroundElectromagneticArc"
ATLAS_NAME = "T_GroundElectromagneticArc16"
ATLAS_PACKAGE = DEST.rsplit("/", 1)[0] + "/" + ATLAS_NAME
ATLAS_SOURCE = ROOT / "ArtSource/FX/GroundMech/Electromagnetic" / (ATLAS_NAME + ".png")
ATLAS_GRID = "(X=4.000000,Y=4.000000)"
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
NS, EM, SP = unreal.NiagaraService, unreal.NiagaraEmitterService, unreal.NiagaraScratchPadService
MAT = unreal.MaterialEditingLibrary

# All four emitters burst once at time zero. Complete lets their live particles
# finish after this spawn window, then Niagara can return the component to its pool.
BURST_WINDOW_SECONDS = 0.15
SHOT_SETTINGS = {
    "Lightning": (
        # Sample the flipbook duration once in InitializeParticle, before setting the total lifetime.
        ("ParticleSpawn", "UniformRangedFloat004.Minimum", 0.16),
        ("ParticleSpawn", "UniformRangedFloat004.Maximum", 0.32),
        ("ParticleSpawn", "InitializeParticle.Color", (0, 3191, 6750, 1)),
    ),
    "spark": (
        ("EmitterUpdate", "UniformRangedInt.Minimum", 4),
        ("EmitterUpdate", "UniformRangedInt.Maximum", 8),
        ("ParticleSpawn", "UniformRangedFloat003.Minimum", 0.10),
        ("ParticleSpawn", "UniformRangedFloat003.Maximum", 0.24),
    ),
    "glow": (),
    "smoke": (
        ("EmitterUpdate", "UniformRangedInt.Minimum", 1),
        ("EmitterUpdate", "UniformRangedInt.Maximum", 2),
        ("ParticleSpawn", "InitializeParticle.Lifetime", 0.35),
    ),
}

# SubImageIndex addresses the 4-by-4 lightning atlas in row-major order.
BOLT_MODULES = (
    ("ParticleSpawn", "GroundArcBeginPlayback",
     [("SampledSeconds", "float", "Particles.Lifetime")],
     [("PlaySeconds", "float", "Particles.GroundArcPlaybackSeconds"),
      ("TotalSeconds", "float", "Particles.Lifetime"), ("FirstFrame", "float", "Particles.SubImageIndex")],
     """PlaySeconds = SampledSeconds;
TotalSeconds = 0.5;
FirstFrame = 0.0;"""),
    ("ParticleUpdate", "GroundArcHoldLastFrame",
     [("AgeSeconds", "float", "Particles.Age"), ("PlaySeconds", "float", "Particles.GroundArcPlaybackSeconds"),
      ("TotalSeconds", "float", "Particles.Lifetime"), ("InitialTint", "Color", "Particles.Initial.Color")],
     [("FrameIndex", "float", "Particles.SubImageIndex"), ("Tint", "Color", "Particles.Color")],
     """float phase = saturate(AgeSeconds / PlaySeconds);
FrameIndex = min(floor(phase * 16.0), 15.0);
float fade = saturate((AgeSeconds - PlaySeconds) / (TotalSeconds - PlaySeconds));
Tint = InitialTint;
Tint.a *= 1.0 - fade * fade * (3.0 - 2.0 * fade);"""),
    # A reset supplies a fresh instance seed even when a pooled component repeats the same segment.
    # Three four-bit lateral offsets and one three-bit spacing value are fixed for this bolt's lifetime.
    ("ParticleSpawn", "GroundArcChooseShape",
     [("InstanceSeed", "int", "Engine.Emitter.InstanceSeed"),
      ("StartPos", "Vector", "User.ArcStart"), ("EndPos", "Vector", "User.ArcEnd")],
     [("Shape", "vec4", "Particles.DynamicMaterialParameter")],
     """int3 cell = (int3)floor((EndPos - StartPos) * 0.125);
int seed = (InstanceSeed ^ (cell.x * 73) ^ (cell.y * 151) ^ (cell.z * 313)) & 32767;
seed = (seed * 20077 + 12345) & 32767;
float3 bins = float3(seed & 15, (seed >> 4) & 15, (seed >> 8) & 15);
float3 bends = (bins * (2.0 / 15.0) - 1.0) * 0.28;
float spacing = (float((seed >> 12) & 7) * (2.0 / 7.0) - 1.0) * 0.56;
Shape = float4(bends, spacing);"""),
)

BEND_CHANNELS = ("BendNear", "BendMiddle", "BendFar", "BendSpacing")
# UV0 already addresses the current frame. Only displace across the bolt, inside that frame.
BEND_UV = """float2 localUV = frac(AtlasUV * float2(4.0, 4.0));
float t = localUV.y + BendSpacing * localUV.y * (1.0 - localUV.y);
float3 weights = saturate(1.0 - abs(4.0 * t - float3(1.0, 2.0, 3.0)));
float bend = dot(weights, float3(BendNear, BendMiddle, BendFar));
// Translate the core at full width; taper the offset to zero at both sides of the sprite.
float center = 0.5 + bend;
float edge = saturate(min(localUV.x / (center - 0.1), (1.0 - localUV.x) / (0.9 - center)));
return AtlasUV - float2(0.25 * bend * edge, 0.0);"""


def require(value, label):
    if not value:
        raise RuntimeError(label)
    return value


def prop(value, name):
    return value.get_editor_property(name)


def flipbook_texture():
    source_hash = hashlib.sha256(ATLAS_SOURCE.read_bytes()).hexdigest()
    tag = "GuLi.FlipbookSourceSHA256"
    texture = ASSETS.load_asset(ATLAS_PACKAGE) if ASSETS.does_asset_exist(ATLAS_PACKAGE) else None
    if texture and ASSETS.get_metadata_tag(texture, tag) == source_hash:
        return texture

    task = unreal.AssetImportTask()
    for key, value in dict(filename=str(ATLAS_SOURCE), destination_path=ATLAS_PACKAGE.rsplit("/", 1)[0],
                           destination_name=ATLAS_NAME, automated=True, replace_existing=True, save=False).items():
        task.set_editor_property(key, value)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    imported = task.get_objects()
    require(len(imported) == 1 and imported[0].get_path_name() == ATLAS_PACKAGE + "." + ATLAS_NAME, ATLAS_PACKAGE)
    texture = imported[0]
    texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_DEFAULT)
    texture.set_editor_property("srgb", True)
    ASSETS.set_metadata_tag(texture, tag, source_hash)
    require(ASSETS.save_loaded_asset(texture, False), ATLAS_PACKAGE)
    return texture


def shape_material(atlas):
    material = (ASSETS.load_asset(ARC_MATERIAL) if ASSETS.does_asset_exist(ARC_MATERIAL)
                else ASSETS.duplicate_asset(SOURCE_MATERIAL, ARC_MATERIAL))
    require(material, ARC_MATERIAL)
    emission = MAT.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    sample = next(node for node in MAT.get_inputs_for_material_expression(material, emission)
                  if isinstance(node, unreal.MaterialExpressionTextureSample))
    warp = next((node for node in MAT.get_inputs_for_material_expression(material, sample)
                 if isinstance(node, unreal.MaterialExpressionCustom)), None)
    dirty = material.get_outermost() in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    if warp is not None and prop(warp, "code") == BEND_UV and prop(sample, "texture") == atlas and not dirty:
        return material

    material.modify()
    sample.set_editor_property("texture", atlas)
    if warp is None:
        def find_or_create(node_type, x, y):
            node = next((n for n in unreal.ObjectIterator(node_type) if n.get_outer() == material), None)
            return node or MAT.create_material_expression(material, node_type, x, y)

        uv = find_or_create(unreal.MaterialExpressionTextureCoordinate, -1510, 180)
        shape = find_or_create(unreal.MaterialExpressionDynamicParameter, -1510, 380)
        shape.set_editor_property("param_names", list(BEND_CHANNELS))
        shape.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 0))
        warp = find_or_create(unreal.MaterialExpressionCustom, -1040, 240)
        warp.set_editor_property("description", "Bend lightning inside its atlas frame")
        warp.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT2)
        inputs = []
        for name in ("AtlasUV", *BEND_CHANNELS):
            pin = unreal.CustomInput()
            pin.set_editor_property("input_name", name)
            inputs.append(pin)
        warp.set_editor_property("inputs", inputs)
        require(MAT.connect_material_expressions(uv, "", warp, "AtlasUV"), "Lightning UV0")
        for channel, name in zip("RGBA", BEND_CHANNELS):
            require(MAT.connect_material_expressions(shape, channel, warp, name), name)
        require(MAT.connect_material_expressions(warp, "", sample, "UVs"), "Lightning atlas coordinates")
    warp.set_editor_property("code", BEND_UV)
    MAT.recompile_material(material)
    require(MAT.get_statistics(material).num_pixel_shader_instructions > 0, "Lightning pixel shader")
    diagnostics = unreal.MaterialNodeService.get_material_diagnostics(ARC_MATERIAL)
    require(diagnostics.is_compiled_ok and not diagnostics.compile_errors, str(diagnostics))
    require(ASSETS.save_loaded_asset(material, False), ARC_MATERIAL)
    return material


def configure_shot_settings(*, apply):
    changed = False
    for emitter, settings in SHOT_SETTINGS.items():
        current = {(str(p.script_type), str(p.parameter_name)): p.value
                   for p in NS.list_rapid_iteration_params(DEST, emitter)}
        for stage, name, target in (("EmitterUpdate", "EmitterState.Loop Duration", BURST_WINDOW_SECONDS), *settings):
            key = f"Constants.{emitter}.{name}"
            require((stage, key) in current, f"Missing {stage} {key}")
            if ast.literal_eval(current[stage, key]) != target:
                require(apply and NS.set_rapid_iteration_param_by_stage(DEST, emitter, stage, key, str(target)), key)
                changed = True

    # The bolt timeline owns frame and opacity. Decorative sparks need no collision or noise.
    for emitter, modules in (("Lightning", ("SubUVAnimation", "ScaleColor")), ("spark", ("Collision", "CurlNoiseForce"))):
        for name in modules:
            if EM.get_module_info(DEST, emitter, name).is_enabled:
                require(apply and EM.enable_module(DEST, emitter, name, False), f"{emitter}.{name}")
                changed = True
    return changed


def scratch_author():
    # Reuse the project's pin-finalized Niagara scratch graph author.
    tree = ast.parse((ROOT/"Scripts/build_commander_combat_effects.py").read_text(encoding="utf-8"))
    fn = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == "scratch")
    namespace = dict(globals())
    exec(compile(ast.Module(body=[fn], type_ignores=[]), "GuLiScratch", "exec"), namespace)
    return namespace["scratch"]


def bolt_geometry():
    settings = EM.get_emitter_properties(DEST, "Lightning")
    local = getattr(settings, "local_space", getattr(settings, "is_local_space", False))
    center = "EndPos - 0.5 * Delta" if local else "Origin + EndPos - 0.5 * Delta"
    # SpriteSize is the full width. Its half-width stays inside the 150 cm segment bounds padding.
    return ("float3 Delta=EndPos-StartPos; float Span=max(length(Delta),0.01); Center=" + center +
            "; Aligned=Delta/Span; Size=float2(clamp(Span*0.14,11.2,210.0), Span); Rotation=0.0;")


def hlsl_node(module):
    nodes = [n for n in SP.list_nodes(DEST, "Lightning", module) if "hlsl" in n.node_type.lower()]
    require(len(nodes) == 1, module + " HLSL node")
    node_id = nodes[0].node_id
    return node_id, SP.get_custom_hlsl_code(DEST, "Lightning", module, node_id)


def describe(asset, *, expected_geometry=None):
    renderer = EM.get_renderer_details(DEST, "Lightning", 0)
    params = sorted(str(v.parameter_name) for v in NS.list_parameters(DEST))
    require({"User.ArcStart", "User.ArcEnd"} <= set(params), "Endpoint parameters")
    material_path = str(renderer.material_path).split(".")[0]
    require(material_path in (SOURCE_MATERIAL, ARC_MATERIAL) and renderer.alignment == "CustomAlignment", "Lightning sprite")
    if expected_geometry is not None:
        require(material_path == ARC_MATERIAL, "Per-bolt shape material")
        require(renderer.sub_image_size == ATLAS_GRID, "Sixteen-frame lightning atlas")
    stages = {}
    for stage in ("ParticleSpawn", "ParticleUpdate"):
        name = "GroundArc"+stage
        _, hlsl = hlsl_node(name)
        require("Delta=EndPos-StartPos" in hlsl, name+" endpoints")
        if expected_geometry is not None:
            require(hlsl == expected_geometry, name+" bolt geometry")
        wires = {(str(x.from_pin), str(x.to_pin)) for x in SP.list_connections(DEST, "Lightning", name)}
        expected = {("User.ArcStart", "StartPos"), ("User.ArcEnd", "EndPos"), ("Center", "Particles.Position"),
                    ("Aligned", "Particles.SpriteAlignment"), ("Size", "Particles.SpriteSize")}
        require(expected <= wires, name+" bindings")
        stages[stage] = sorted(list(expected))
    emitters = [str(v.emitter_name) for v in NS.list_emitters(DEST)]
    require(all(EM.get_emitter_properties(DEST, name).loop_behavior=="Once" for name in emitters), "One shot")
    return {"path": asset.get_path_name(), "parameters": params, "material": renderer.material_path,
            "alignment": renderer.alignment, "emitters": emitters, "bindings": stages}


def finish(asset, *, graph_changed=False):
    material = shape_material(flipbook_texture())
    geometry = bolt_geometry()
    sources = {"GroundArcParticleSpawn": geometry, "GroundArcParticleUpdate": geometry}
    modules = {str(item.module_name) for item in EM.list_modules(DEST, "Lightning")}
    for stage, module, inputs, outputs, code in BOLT_MODULES:
        if module not in modules:
            require(scratch_author()(DEST, "Lightning", stage, module, inputs, outputs, code) == module, module)
            graph_changed = True
        info = EM.get_module_info(DEST, "Lightning", module)
        require(info.is_enabled and info.module_type == stage, module + " stage")
        wires = {(c.from_pin, c.to_pin) for c in SP.list_connections(DEST, "Lightning", module)}
        required = {(source, pin) for pin, _, source in inputs} | {(pin, dest) for pin, _, dest in outputs}
        require(required <= wires, module + " bindings")
        sources[module] = code

    code_changed = False
    for module, code in sources.items():
        node_id, current = hlsl_node(module)
        if current != code:
            require(SP.set_custom_hlsl_code(DEST, "Lightning", module, node_id, code), module)
            code_changed = True
    if code_changed:
        require(SP.apply_changes(DEST), "Compile updated bolt modules")
    renderer = EM.get_renderer_details(DEST, "Lightning", 0)
    if renderer.sub_image_size != ATLAS_GRID:
        require(EM.set_renderer_property(DEST, "Lightning", 0, "SubImageSize", ATLAS_GRID), "4-by-4 lightning atlas")
        graph_changed = True
    if renderer.material_path != material.get_path_name():
        require(EM.set_renderer_property(DEST, "Lightning", 0, "Material", material.get_path_name()), "Bind bolt material")
        graph_changed = True
    settings_changed = configure_shot_settings(apply=True)
    dirty = asset.get_outermost() in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    if graph_changed or code_changed or settings_changed or dirty:
        result = NS.compile_with_results(DEST)
        require(result.success and not result.errors, str(result))
        require(ASSETS.save_loaded_asset(asset, False), DEST)
    configure_shot_settings(apply=False)
    return describe(asset, expected_geometry=geometry)


def main():
    require(not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor(), "Active play session")
    asset = ASSETS.load_asset(DEST) if ASSETS.does_asset_exist(DEST) else ASSETS.duplicate_asset(SOURCE, DEST)
    require(asset, DEST)
    existing={str(item.module_name) for item in EM.list_modules(DEST,"Lightning")}
    if {"GroundArcParticleSpawn", "GroundArcParticleUpdate"} <= existing:
        try:
            describe(asset)
        except RuntimeError:
            pass
        else:
            return finish(asset)
    for name in ("ArcStart", "ArcEnd"):
        if not NS.parameter_exists(DEST, "User."+name):
            require(NS.add_user_parameter(DEST, name, "Vector", "(X=0,Y=0,Z=0)"), name)
    scratch = scratch_author()
    emitter = "Lightning"
    for stage in ("ParticleSpawn", "ParticleUpdate"):
        module = "GroundArc" + stage
        for item in EM.list_modules(DEST, emitter):
            if str(item.module_name)==module:
                require(EM.remove_module(DEST, emitter, module), module)
        scratch(DEST, emitter, stage, module,
            [("StartPos", "Vector", "User.ArcStart"), ("EndPos", "Vector", "User.ArcEnd"),
             ("Origin", "Position", "Engine.Owner.Position")],
            [("Center", "Position", "Particles.Position"), ("Aligned", "Vector", "Particles.SpriteAlignment"),
             ("Size", "vec2", "Particles.SpriteSize"), ("Rotation", "float", "Particles.SpriteRotation")],
            bolt_geometry())
    require(EM.set_renderer_property(DEST, emitter, 0, "Alignment", "CustomAlignment"), "Flipbook alignment")
    for item in NS.list_emitters(DEST):
        name=str(item.emitter_name)
        require(EM.set_module_input(DEST,name,"EmitterState","Life Cycle Mode","NewEnumerator1"),name)
        require(EM.set_module_input(DEST,name,"EmitterState","Loop Behavior","NewEnumerator1"),name)
    asset.set_editor_property("max_pool_size", 64)
    return finish(asset, graph_changed=True)


if __name__ == "__main__":
    unreal.MCPythonHelper.submit_result(json.dumps(main(),default=str))
