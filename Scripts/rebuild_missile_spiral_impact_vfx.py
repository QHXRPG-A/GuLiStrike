# Rebuild the GuLiStrike missile spiral impact effect with project-owned,
# texture-free materials. Run through Scripts/ue_exec.py in the live editor.

import json
import unreal


DEST = "/Game/GuLiStrike/FX/MissileSpiralImpact"
SYSTEM = f"{DEST}/NS_MissileSpiralImpact"
CORE_MAT = f"{DEST}/M_MissileSpiral_Core"
RING_MAT = f"{DEST}/M_MissileSpiral_Ring"
SMOKE_MAT = f"{DEST}/M_MissileSpiral_Smoke"

SIMPLE_BURST = "/Niagara/DefaultAssets/Templates/Emitters/SimpleSpriteBurst.SimpleSpriteBurst"
FOUNTAIN = "/Niagara/DefaultAssets/Templates/Emitters/Fountain.Fountain"
OMNI_BURST = "/Niagara/DefaultAssets/Templates/Emitters/OmnidirectionalBurst.OmnidirectionalBurst"

MATERIALS = unreal.MaterialService
NODES = unreal.MaterialNodeService
NIAGARA = unreal.NiagaraService
EMITTERS = unreal.NiagaraEmitterService
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)

report = {
    "created_materials": [],
    "removed_emitters": [],
    "created_emitters": [],
    "removed_user_parameters": [],
    "material_diagnostics": {},
    "compile_checks": [],
}


def expression_id(info):
    return str(info.get_editor_property("id"))


def connect(material_path, source, target, target_input, source_output=""):
    if not NODES.connect_expressions(
        material_path, source, source_output, target, target_input
    ):
        raise RuntimeError(
            f"Failed material connection: {source}:{source_output} -> "
            f"{target}:{target_input}"
        )


def connect_output(material_path, source, material_property, source_output=""):
    if not NODES.connect_to_output(
        material_path, source, source_output, material_property
    ):
        raise RuntimeError(
            f"Failed material output connection: {source}:{source_output} -> "
            f"{material_property}"
        )


def configure_material(material_path, blend_mode):
    changed = MATERIALS.set_properties(
        material_path,
        {
            "BlendMode": blend_mode,
            "ShadingModel": "MSM_Unlit",
            "TwoSided": "true",
            "bUsedWithParticleSprites": "true",
            "bUsedWithNiagaraSprites": "true",
            "bUsedWithNiagaraRibbons": "true",
        },
    )
    if changed < 6:
        raise RuntimeError(f"Only {changed}/6 properties set on {material_path}")


def new_material(name, blend_mode):
    path = f"{DEST}/{name}"
    if ASSETS.does_asset_exist(path):
        ASSETS.load_asset(path)
        return path, False

    result = MATERIALS.create_material(name, DEST)
    if not bool(result.get_editor_property("success")):
        raise RuntimeError(
            f"Could not create {path}: {result.get_editor_property('error_message')}"
        )
    ASSETS.load_asset(path)
    configure_material(path, blend_mode)
    report["created_materials"].append(path)
    return path, True


def make_soft_sprite_material():
    path, created = new_material("M_MissileSpiral_Smoke", "BLEND_Translucent")
    if not created:
        return path

    uv = expression_id(NODES.create_expression(path, "MaterialExpressionTextureCoordinate", -1100, 0))
    center = expression_id(NODES.create_expression(path, "MaterialExpressionConstant2Vector", -1100, 160))
    mask = expression_id(NODES.create_expression(path, "MaterialExpressionSphereMask", -820, 40))
    particle_color = expression_id(NODES.create_expression(path, "MaterialExpressionParticleColor", -820, -300))
    relative_time = expression_id(NODES.create_expression(path, "MaterialExpressionParticleRelativeTime", -820, 300))
    time_one = expression_id(NODES.create_expression(path, "MaterialExpressionConstant", -820, 440))
    one_minus_time = expression_id(NODES.create_expression(path, "MaterialExpressionSubtract", -580, 300))
    mask_fade = expression_id(NODES.create_expression(path, "MaterialExpressionMultiply", -340, 180))
    color_mul = expression_id(NODES.create_expression(path, "MaterialExpressionMultiply", -340, -300))
    intensity_mul = expression_id(NODES.create_expression(path, "MaterialExpressionMultiply", -100, -300))
    emissive_mul = expression_id(NODES.create_expression(path, "MaterialExpressionMultiply", 140, -300))
    opacity_mul = expression_id(NODES.create_expression(path, "MaterialExpressionMultiply", -100, 100))
    opacity_fade = expression_id(NODES.create_expression(path, "MaterialExpressionMultiply", 140, 100))

    tint = expression_id(
        NODES.create_parameter(
            path,
            "Vector",
            "Tint",
            "Look",
            "(R=0.24,G=0.09,B=0.025,A=1.0)",
            -820,
            -480,
        )
    )
    intensity = expression_id(
        NODES.create_parameter(path, "Scalar", "Intensity", "Look", "1.6", -580, -480)
    )
    opacity = expression_id(
        NODES.create_parameter(path, "Scalar", "OpacityScale", "Look", "0.55", -340, 420)
    )

    NODES.set_expression_property(path, center, "R", "0.5")
    NODES.set_expression_property(path, center, "G", "0.5")
    NODES.set_expression_property(path, mask, "AttenuationRadius", "0.5")
    NODES.set_expression_property(path, mask, "HardnessPercent", "18")
    NODES.set_expression_property(path, time_one, "R", "1.0")

    connect(path, uv, mask, "A")
    connect(path, center, mask, "B")
    connect(path, time_one, one_minus_time, "A")
    connect(path, relative_time, one_minus_time, "B")
    connect(path, mask, mask_fade, "A")
    connect(path, one_minus_time, mask_fade, "B")

    connect(path, particle_color, color_mul, "A", "RGB")
    connect(path, tint, color_mul, "B", "RGB")
    connect(path, color_mul, intensity_mul, "A")
    connect(path, intensity, intensity_mul, "B")
    connect(path, intensity_mul, emissive_mul, "A")
    connect(path, mask_fade, emissive_mul, "B")
    connect_output(path, emissive_mul, "EmissiveColor")

    connect(path, particle_color, opacity_mul, "A", "A")
    connect(path, opacity, opacity_mul, "B")
    connect(path, opacity_mul, opacity_fade, "A")
    connect(path, mask_fade, opacity_fade, "B")
    connect_output(path, opacity_fade, "Opacity")

    NODES.layout_expressions(path)
    return path


def make_ring_material():
    path, created = new_material("M_MissileSpiral_Ring", "BLEND_Additive")
    if not created:
        return path

    uv = expression_id(NODES.create_expression(path, "MaterialExpressionTextureCoordinate", -1320, 20))
    center = expression_id(NODES.create_expression(path, "MaterialExpressionConstant2Vector", -1320, 180))
    relative_time = expression_id(NODES.create_expression(path, "MaterialExpressionParticleRelativeTime", -1320, 360))
    time_one = expression_id(NODES.create_expression(path, "MaterialExpressionConstant", -1320, 500))
    one_minus_time = expression_id(NODES.create_expression(path, "MaterialExpressionSubtract", -1080, 400))
    radius_lerp = expression_id(NODES.create_expression(path, "MaterialExpressionLinearInterpolate", -1040, -120))
    inner_radius = expression_id(NODES.create_expression(path, "MaterialExpressionSubtract", -800, -120))
    outer_mask = expression_id(NODES.create_expression(path, "MaterialExpressionSphereMask", -560, -20))
    inner_mask = expression_id(NODES.create_expression(path, "MaterialExpressionSphereMask", -560, 180))
    ring_mask = expression_id(NODES.create_expression(path, "MaterialExpressionSubtract", -80, 40))
    ring_fade = expression_id(NODES.create_expression(path, "MaterialExpressionMultiply", 160, 40))
    particle_color = expression_id(NODES.create_expression(path, "MaterialExpressionParticleColor", -320, -360))
    color_mul = expression_id(NODES.create_expression(path, "MaterialExpressionMultiply", -80, -360))
    intensity_mul = expression_id(NODES.create_expression(path, "MaterialExpressionMultiply", 160, -360))
    emissive_mul = expression_id(NODES.create_expression(path, "MaterialExpressionMultiply", 400, -300))
    opacity_mul = expression_id(NODES.create_expression(path, "MaterialExpressionMultiply", 400, 80))
    opacity_fade = expression_id(NODES.create_expression(path, "MaterialExpressionMultiply", 640, 80))

    start_radius = expression_id(
        NODES.create_parameter(path, "Scalar", "StartRadius", "Shape", "0.035", -1320, -260)
    )
    end_radius = expression_id(
        NODES.create_parameter(path, "Scalar", "EndRadius", "Shape", "0.49", -1320, -140)
    )
    width = expression_id(
        NODES.create_parameter(path, "Scalar", "RingWidth", "Shape", "0.045", -1040, 60)
    )
    tint = expression_id(
        NODES.create_parameter(
            path,
            "Vector",
            "Tint",
            "Look",
            "(R=1.0,G=0.16,B=0.01,A=1.0)",
            -320,
            -520,
        )
    )
    intensity = expression_id(
        NODES.create_parameter(path, "Scalar", "Intensity", "Look", "28.0", -80, -520)
    )
    opacity = expression_id(
        NODES.create_parameter(path, "Scalar", "OpacityScale", "Look", "0.9", 160, 220)
    )

    NODES.set_expression_property(path, center, "R", "0.5")
    NODES.set_expression_property(path, center, "G", "0.5")
    NODES.set_expression_property(path, outer_mask, "HardnessPercent", "78")
    NODES.set_expression_property(path, inner_mask, "HardnessPercent", "78")
    NODES.set_expression_property(path, time_one, "R", "1.0")

    connect(path, start_radius, radius_lerp, "A")
    connect(path, end_radius, radius_lerp, "B")
    connect(path, relative_time, radius_lerp, "Alpha")
    connect(path, radius_lerp, inner_radius, "A")
    connect(path, width, inner_radius, "B")

    for sphere, radius in ((outer_mask, radius_lerp), (inner_mask, inner_radius)):
        connect(path, uv, sphere, "A")
        connect(path, center, sphere, "B")
        connect(path, radius, sphere, "Radius")

    connect(path, outer_mask, ring_mask, "A")
    connect(path, inner_mask, ring_mask, "B")
    connect(path, time_one, one_minus_time, "A")
    connect(path, relative_time, one_minus_time, "B")
    connect(path, ring_mask, ring_fade, "A")
    connect(path, one_minus_time, ring_fade, "B")

    connect(path, particle_color, color_mul, "A", "RGB")
    connect(path, tint, color_mul, "B", "RGB")
    connect(path, color_mul, intensity_mul, "A")
    connect(path, intensity, intensity_mul, "B")
    connect(path, intensity_mul, emissive_mul, "A")
    connect(path, ring_fade, emissive_mul, "B")
    connect_output(path, emissive_mul, "EmissiveColor")

    connect(path, particle_color, opacity_mul, "A", "A")
    connect(path, opacity, opacity_mul, "B")
    connect(path, opacity_mul, opacity_fade, "A")
    connect(path, ring_fade, opacity_fade, "B")
    connect_output(path, opacity_fade, "Opacity")

    NODES.layout_expressions(path)
    return path


def compile_material(material_path):
    ASSETS.load_asset(material_path)
    if not MATERIALS.compile_material(material_path):
        raise RuntimeError(f"Material compile request failed: {material_path}")
    diagnostics = NODES.get_material_diagnostics(material_path)
    ok = bool(diagnostics.get_editor_property("success")) and bool(
        diagnostics.get_editor_property("is_compiled_ok")
    )
    texture_samples = int(diagnostics.get_editor_property("texture_sample_count"))
    textures = list(diagnostics.get_editor_property("referenced_texture_paths"))
    if not ok or texture_samples or textures:
        raise RuntimeError(
            f"Material validation failed for {material_path}: {diagnostics}"
        )
    if not MATERIALS.save_material(material_path):
        raise RuntimeError(f"Material save failed: {material_path}")
    report["material_diagnostics"][material_path] = {
        "compiled": True,
        "texture_samples": texture_samples,
        "referenced_textures": [str(item) for item in textures],
        "expression_count": int(diagnostics.get_editor_property("expression_count")),
    }


def tune_material_defaults():
    values = {
        CORE_MAT: {
            "Tint": "(R=1.0,G=0.32,B=0.015,A=1.0)",
            "Intensity": "4.5",
            "OpacityScale": "0.82",
        },
        RING_MAT: {
            "Tint": "(R=1.0,G=0.22,B=0.01,A=1.0)",
            "Intensity": "14.0",
            "OpacityScale": "0.90",
        },
        SMOKE_MAT: {
            "Tint": "(R=0.25,G=0.18,B=0.12,A=1.0)",
            "Intensity": "1.9",
            "OpacityScale": "0.48",
        },
    }
    for material_path, parameters in values.items():
        ASSETS.load_asset(material_path)
        for name, value in parameters.items():
            if not MATERIALS.set_parameter_default(material_path, name, value):
                raise RuntimeError(
                    f"Failed to set material default {material_path}/{name}={value}"
                )
        if material_path == CORE_MAT:
            MATERIALS.set_property(material_path, "bUsedWithNiagaraRibbons", "true")


def compile_system(label):
    result = NIAGARA.compile_with_results(SYSTEM)
    check = {
        "label": label,
        "success": bool(result.get_editor_property("success")),
        "errors": int(result.get_editor_property("error_count")),
        "warnings": int(result.get_editor_property("warning_count")),
    }
    report["compile_checks"].append(check)
    if not check["success"] or check["errors"]:
        raise RuntimeError(f"Niagara compile failed after {label}: {result}")


def add_emitter(template, name):
    actual = str(NIAGARA.add_emitter(SYSTEM, template, name))
    if not actual:
        raise RuntimeError(f"Failed to add emitter {name} from {template}")
    report["created_emitters"].append(actual)
    return actual


def add_module(emitter, script, stage):
    if not EMITTERS.add_module(SYSTEM, emitter, script, stage):
        raise RuntimeError(f"Failed to add {script} to {emitter}/{stage}")


def set_ri(emitter, stage, module, input_name, value):
    parameter = f"Constants.{emitter}.{module}.{input_name}"
    if not NIAGARA.set_rapid_iteration_param_by_stage(
        SYSTEM, emitter, stage, parameter, str(value)
    ):
        raise RuntimeError(
            f"Failed to set {emitter}/{stage}/{module}.{input_name}={value}"
        )


def assign_material(emitter, material_path, renderer_index=0):
    ASSETS.load_asset(material_path)
    if not EMITTERS.set_renderer_property(
        SYSTEM, emitter, renderer_index, "Material", material_path
    ):
        raise RuntimeError(f"Failed to assign {material_path} to {emitter}")


def rebuild_system():
    ASSETS.load_asset(SYSTEM)

    for info in list(NIAGARA.list_emitters(SYSTEM)):
        name = str(info.get_editor_property("emitter_name"))
        if not NIAGARA.remove_emitter(SYSTEM, name):
            raise RuntimeError(f"Failed to remove legacy emitter {name}")
        report["removed_emitters"].append(name)

    for parameter in list(NIAGARA.list_parameters(SYSTEM)):
        namespace = str(parameter.get_editor_property("namespace"))
        if namespace != "User":
            continue
        name = str(parameter.get_editor_property("parameter_name"))
        if not NIAGARA.remove_user_parameter(SYSTEM, name):
            raise RuntimeError(f"Failed to remove stale parameter {name}")
        report["removed_user_parameters"].append(name)

    core = add_emitter(SIMPLE_BURST, "MissileCore")
    add_module(
        core,
        "/Niagara/Modules/Spawn/Velocity/AddVelocity.AddVelocity",
        "ParticleSpawn",
    )
    set_ri(core, "EmitterUpdate", "EmitterState", "Loop Duration", "1.25")
    set_ri(core, "EmitterUpdate", "SpawnBurst_Instantaneous", "Spawn Count", "1")
    set_ri(core, "EmitterUpdate", "SpawnBurst_Instantaneous", "Spawn Time", "0.0")
    set_ri(core, "ParticleSpawn", "InitializeParticle", "Lifetime", "0.86")
    set_ri(core, "ParticleSpawn", "InitializeParticle", "Position Offset", "0,0,720")
    set_ri(core, "ParticleSpawn", "InitializeParticle", "Uniform Sprite Size", "92")
    set_ri(core, "ParticleSpawn", "InitializeParticle", "Color", "1.0,0.90,0.55,1.0")
    set_ri(core, "ParticleSpawn", "AddVelocity", "Velocity", "0,0,-860")
    assign_material(core, CORE_MAT)
    compile_system(core)

    spiral = add_emitter(FOUNTAIN, "SpiralTrail")
    add_module(
        spiral,
        "/Niagara/Modules/Update/Forces/VortexForce.VortexForce",
        "ParticleUpdate",
    )
    set_ri(spiral, "EmitterUpdate", "EmitterState", "Loop Duration", "0.82")
    set_ri(spiral, "EmitterUpdate", "SpawnRate", "SpawnRate", "90")
    set_ri(spiral, "ParticleSpawn", "InitializeParticle", "Lifetime Min", "0.35")
    set_ri(spiral, "ParticleSpawn", "InitializeParticle", "Lifetime Max", "0.58")
    set_ri(spiral, "ParticleSpawn", "InitializeParticle", "Position Offset", "0,0,720")
    set_ri(spiral, "ParticleSpawn", "InitializeParticle", "Uniform Sprite Size Min", "9")
    set_ri(spiral, "ParticleSpawn", "InitializeParticle", "Uniform Sprite Size Max", "18")
    set_ri(spiral, "ParticleSpawn", "InitializeParticle", "Ribbon Width", "18")
    set_ri(spiral, "ParticleSpawn", "InitializeParticle", "Color", "1.0,0.65,0.25,0.92")
    set_ri(spiral, "ParticleSpawn", "ShapeLocation", "Sphere Radius", "105")
    set_ri(spiral, "ParticleSpawn", "AddVelocity", "Velocity", "0,0,-830")
    set_ri(spiral, "ParticleUpdate", "GravityForce", "Gravity", "0,0,-110")
    set_ri(spiral, "ParticleUpdate", "Drag", "Drag", "0.04")
    set_ri(spiral, "ParticleUpdate", "VortexForce", "Vortex Axis", "0,0,1")
    set_ri(spiral, "ParticleUpdate", "VortexForce", "Vortex Force Amount", "3200")
    set_ri(spiral, "ParticleUpdate", "VortexForce", "Influence Falloff Radius", "300")
    set_ri(spiral, "ParticleUpdate", "VortexForce", "Origin Pull Amount", "220")
    assign_material(spiral, CORE_MAT)
    if not EMITTERS.add_renderer(SYSTEM, spiral, "Ribbon"):
        raise RuntimeError("Failed to add the SpiralTrail ribbon renderer")
    assign_material(spiral, CORE_MAT, renderer_index=1)
    compile_system(spiral)

    flash = add_emitter(SIMPLE_BURST, "ImpactFlash")
    set_ri(flash, "EmitterUpdate", "EmitterState", "Loop Duration", "1.35")
    set_ri(flash, "EmitterUpdate", "SpawnBurst_Instantaneous", "Spawn Count", "1")
    set_ri(flash, "EmitterUpdate", "SpawnBurst_Instantaneous", "Spawn Time", "0.82")
    set_ri(flash, "ParticleSpawn", "InitializeParticle", "Lifetime", "0.10")
    set_ri(flash, "ParticleSpawn", "InitializeParticle", "Position Offset", "0,0,24")
    set_ri(flash, "ParticleSpawn", "InitializeParticle", "Uniform Sprite Size", "220")
    set_ri(flash, "ParticleSpawn", "InitializeParticle", "Color", "1.0,1.0,0.90,1.0")
    assign_material(flash, CORE_MAT)
    compile_system(flash)

    fireball = add_emitter(OMNI_BURST, "ImpactFireball")
    set_ri(fireball, "EmitterUpdate", "EmitterState", "Loop Duration", "1.45")
    set_ri(fireball, "EmitterUpdate", "SpawnBurst_Instantaneous", "Spawn Count", "14")
    set_ri(fireball, "EmitterUpdate", "SpawnBurst_Instantaneous", "Spawn Time", "0.82")
    set_ri(fireball, "ParticleSpawn", "InitializeParticle", "Lifetime Min", "0.28")
    set_ri(fireball, "ParticleSpawn", "InitializeParticle", "Lifetime Max", "0.58")
    set_ri(fireball, "ParticleSpawn", "InitializeParticle", "Position Offset", "0,0,52")
    set_ri(fireball, "ParticleSpawn", "InitializeParticle", "Uniform Sprite Size Min", "48")
    set_ri(fireball, "ParticleSpawn", "InitializeParticle", "Uniform Sprite Size Max", "96")
    set_ri(fireball, "ParticleSpawn", "InitializeParticle", "Color", "1.0,0.80,0.30,0.92")
    set_ri(fireball, "ParticleSpawn", "ShapeLocation", "Sphere Radius", "62")
    set_ri(fireball, "ParticleSpawn", "AddVelocity", "Velocity Speed Scale", "3.2")
    set_ri(fireball, "ParticleUpdate", "GravityForce", "Gravity", "0,0,-90")
    set_ri(fireball, "ParticleUpdate", "Drag", "Drag", "1.25")
    assign_material(fireball, CORE_MAT)
    compile_system(fireball)

    ring = add_emitter(SIMPLE_BURST, "ImpactRing")
    set_ri(ring, "EmitterUpdate", "EmitterState", "Loop Duration", "1.65")
    set_ri(ring, "EmitterUpdate", "SpawnBurst_Instantaneous", "Spawn Count", "1")
    set_ri(ring, "EmitterUpdate", "SpawnBurst_Instantaneous", "Spawn Time", "0.82")
    set_ri(ring, "ParticleSpawn", "InitializeParticle", "Lifetime", "0.65")
    set_ri(ring, "ParticleSpawn", "InitializeParticle", "Position Offset", "0,0,6")
    set_ri(ring, "ParticleSpawn", "InitializeParticle", "Uniform Sprite Size", "900")
    set_ri(ring, "ParticleSpawn", "InitializeParticle", "Color", "1.0,0.75,0.25,0.95")
    add_module(
        ring,
        "/Niagara/Modules/Update/Renderers/Sprite/SpriteFacingAndAlignment.SpriteFacingAndAlignment",
        "ParticleUpdate",
    )
    set_ri(
        ring,
        "ParticleUpdate",
        "SpriteFacingAndAlignment",
        "Sprite Facing",
        "0,0,1",
    )
    set_ri(
        ring,
        "ParticleUpdate",
        "SpriteFacingAndAlignment",
        "Sprite Alignment",
        "1,0,0",
    )
    assign_material(ring, RING_MAT)
    if not EMITTERS.set_renderer_property(
        SYSTEM, ring, 0, "FacingMode", "CustomFacingVector"
    ):
        raise RuntimeError("Failed to set ImpactRing FacingMode")
    if not EMITTERS.set_renderer_property(
        SYSTEM, ring, 0, "Alignment", "CustomAlignment"
    ):
        raise RuntimeError("Failed to set ImpactRing Alignment")
    compile_system(ring)

    sparks = add_emitter(OMNI_BURST, "ImpactSparks")
    set_ri(sparks, "EmitterUpdate", "EmitterState", "Loop Duration", "2.0")
    set_ri(sparks, "EmitterUpdate", "SpawnBurst_Instantaneous", "Spawn Count", "54")
    set_ri(sparks, "EmitterUpdate", "SpawnBurst_Instantaneous", "Spawn Time", "0.82")
    set_ri(sparks, "ParticleSpawn", "InitializeParticle", "Lifetime Min", "0.45")
    set_ri(sparks, "ParticleSpawn", "InitializeParticle", "Lifetime Max", "1.10")
    set_ri(sparks, "ParticleSpawn", "InitializeParticle", "Position Offset", "0,0,38")
    set_ri(sparks, "ParticleSpawn", "InitializeParticle", "Uniform Sprite Size Min", "3")
    set_ri(sparks, "ParticleSpawn", "InitializeParticle", "Uniform Sprite Size Max", "8")
    set_ri(sparks, "ParticleSpawn", "InitializeParticle", "Color", "1.0,0.75,0.22,1.0")
    set_ri(sparks, "ParticleSpawn", "ShapeLocation", "Sphere Radius", "34")
    set_ri(sparks, "ParticleSpawn", "AddVelocity", "Velocity Speed Scale", "12")
    set_ri(sparks, "ParticleUpdate", "GravityForce", "Gravity", "0,0,-980")
    set_ri(sparks, "ParticleUpdate", "Drag", "Drag", "0.72")
    assign_material(sparks, CORE_MAT)
    compile_system(sparks)

    smoke = add_emitter(FOUNTAIN, "ImpactSmoke")
    set_ri(smoke, "EmitterUpdate", "EmitterState", "Loop Delay", "0.82")
    set_ri(smoke, "EmitterUpdate", "EmitterState", "Loop Duration", "0.50")
    set_ri(smoke, "EmitterUpdate", "SpawnRate", "SpawnRate", "26")
    set_ri(smoke, "ParticleSpawn", "InitializeParticle", "Lifetime Min", "1.05")
    set_ri(smoke, "ParticleSpawn", "InitializeParticle", "Lifetime Max", "1.75")
    set_ri(smoke, "ParticleSpawn", "InitializeParticle", "Position Offset", "0,0,28")
    set_ri(smoke, "ParticleSpawn", "InitializeParticle", "Uniform Sprite Size Min", "82")
    set_ri(smoke, "ParticleSpawn", "InitializeParticle", "Uniform Sprite Size Max", "168")
    set_ri(smoke, "ParticleSpawn", "InitializeParticle", "Color", "1.0,0.82,0.65,0.62")
    set_ri(smoke, "ParticleSpawn", "ShapeLocation", "Sphere Radius", "108")
    set_ri(smoke, "ParticleSpawn", "AddVelocity", "Velocity", "0,0,205")
    set_ri(smoke, "ParticleUpdate", "GravityForce", "Gravity", "0,0,-55")
    set_ri(smoke, "ParticleUpdate", "Drag", "Drag", "0.22")
    assign_material(smoke, SMOKE_MAT)
    compile_system(smoke)

    if not NIAGARA.save_system(SYSTEM):
        raise RuntimeError(f"Failed to save {SYSTEM}")
    compile_system("saved system readback")


try:
    make_ring_material()
    make_soft_sprite_material()
    tune_material_defaults()
    for material in (CORE_MAT, RING_MAT, SMOKE_MAT):
        compile_material(material)
    rebuild_system()

    summary = NIAGARA.summarize(SYSTEM)
    report["final_summary"] = {
        "system_path": str(summary.get_editor_property("system_path")),
        "emitter_count": int(summary.get_editor_property("emitter_count")),
        "emitter_names": [str(name) for name in summary.get_editor_property("emitter_names")],
        "has_gpu_emitters": bool(summary.get_editor_property("has_gpu_emitters")),
    }
    report["dirty_content"] = [
        str(package.get_name())
        for package in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    ]
    report["dirty_maps"] = [
        str(package.get_name())
        for package in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    ]
except Exception as exc:
    import traceback

    report["exception"] = f"{type(exc).__name__}: {exc}"
    report["traceback"] = traceback.format_exc()

print(json.dumps(report, ensure_ascii=False, sort_keys=True))
