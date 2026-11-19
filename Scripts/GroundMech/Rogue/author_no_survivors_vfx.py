"""Author the barrage's continuous exhaust and its reduced project-owned Niagara systems."""
import ast
import json
import traceback
from pathlib import Path

import unreal

OUT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / "Artifacts/GroundNoSurvivors"
BASE = "/Game/GuLiStrike/FX/GroundMech/NoSurvivors/"
FLIGHT = "/Game/Assets/VFX/Weapons/Missiles/HighSpeedThrusters/FX/NS_RocketExhaust_Meshes"
IMPACT = "/Game/GuLiStrike/FX/CombatExplosions/NS_WingmanBombardment_01"
SERVICE = unreal.NiagaraService
LIB = unreal.EditorAssetLibrary
EM = unreal.NiagaraEmitterService
SP = unreal.NiagaraScratchPadService
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
THRUSTERS = "NE_Thrusters"
STRAND = "/Game/GuLiStrike/FX/GroundMech/Blink/M_GroundMech_BlinkStrand"


def require(value, description):
    if not value:
        raise RuntimeError(description)
    return value


def scratch_author(path):
    # Use the project's pin-order and parameter-map wiring for Niagara scratch modules.
    source = OUT.parents[1] / "Scripts/build_wingman_toon_explosion.py"
    node = next(n for n in ast.parse(source.read_text(encoding="utf8")).body
                if isinstance(n, ast.FunctionDef) and n.name == "scratch")
    scope = dict(globals(), SYSTEM=path, need=require)
    exec(compile(ast.Module(body=[node], type_ignores=[]), str(source), "exec"), scope)
    return scope["scratch"]


def mesh_renderer(system):
    outer = system.get_path_name() + ":" + THRUSTERS
    return require(unreal.find_object(None, outer + ".NiagaraMeshRendererProperties_0"), outer)


def renderer_index(path, kind):
    return next((r.renderer_index for r in EM.list_renderers(path, THRUSTERS)
                 if str(r.renderer_type) == kind), None)


def flight_row():
    source = OUT.parents[1] / "data/Json/DT_GuLiStrikeVfx_Effects.json"
    return next(r for r in json.loads(source.read_text(encoding="utf8")) if r["Name"] == "GroundNoSurvivorsFlight")


def configure_flight(path, system):
    row = flight_row()
    for name, value in [("VisualScale", row["Scale"]["X"]), ("TrailLengthCm", row["TrailLengthCm"]),
                        ("TrailFadeSeconds", row["TrailFadeSeconds"]), ("Thrusters_SpawnRate", 60 / row["TrailFadeSeconds"])]:
        if not SERVICE.parameter_exists(path, "User." + name):
            require(SERVICE.add_user_parameter(path, name, "Float", str(value)), name)
        require(SERVICE.set_parameter(path, "User." + name, str(value)), name)
    if not SERVICE.parameter_exists(path, "User.Velocity"):
        require(SERVICE.add_user_parameter(path, "Velocity", "Vector", "(X=0,Y=0,Z=0)"), "Velocity")
    scratch = scratch_author(path)
    modules = {str(m.module_name) for m in EM.list_modules(path, THRUSTERS)}
    owner = ("Owner", "Position", "Engine.Owner.Position")
    unique = ("Id", "int", "Particles.UniqueID")
    birth = ("Birth", "Position", "Particles.BarrageTrailBirth")
    size = [("NativeSize", "Vector", "Particles.Scale"), ("Factor", "float", "User.VisualScale")]
    shape = [("Position", "Position", "Particles.Position"), ("Width", "float", "Particles.RibbonWidth"),
             ("Head", "int", "Particles.VisibilityTag"), ("Order", "float", "Particles.RibbonLinkOrder"),
             ("HeadSize", "Vector", "Particles.BarrageHeadScale")]
    if "RecordBarrageExhaust" not in modules:
        scratch(THRUSTERS, "ParticleSpawn", "RecordBarrageExhaust",
                [owner, unique, *size, ("LengthCm", "float", "User.TrailLengthCm"),
                 ("FadeSeconds", "float", "User.TrailFadeSeconds"), ("FlightVelocity", "Vector", "User.Velocity")],
                [birth, *shape, ("Life", "float", "Particles.Lifetime")],
                "Birth=Owner; Position=Owner; Width=24.0; Head=(Id==0)?1:0; Order=float(Id); "
                "HeadSize=NativeSize*Factor; Life=min(FadeSeconds,LengthCm/max(length(FlightVelocity),1.0));")
    if "ConnectBarrageExhaust" not in modules:
        scratch(THRUSTERS, "ParticleUpdate", "ConnectBarrageExhaust",
                [owner, unique, birth, *size, ("Count", "int", "Engine.Emitter.TotalSpawnedParticles"),
                 ("Age", "float", "Particles.Age"), ("Fraction", "float", "Particles.NormalizedAge")], shape,
                "Head=(Id+1==Count && Age<0.10)?1:0; Position=Head==1?Owner:Birth; "
                "Order=Head==1?10000000.0:float(Id); float Fade=1.0-saturate(Fraction); "
                "Width=2.0+22.0*Fade*Fade; HeadSize=NativeSize*Factor;")

    original = mesh_renderer(unreal.load_asset(FLIGHT)).get_editor_property("Meshes")[0]
    mesh = mesh_renderer(system)
    entries = list(mesh.get_editor_property("Meshes"))
    assert len(entries) == 1 and entries[0].mesh == original.mesh
    assert entries[0].scale == original.scale
    binding = mesh.get_editor_property("ScaleBinding").export_text()
    if "Particles.BarrageHeadScale" not in binding:
        assert '"Particles.Scale"' in binding
        binding = binding.replace('"Particles.Scale"', '"Particles.BarrageHeadScale"').replace('"Scale"', '"BarrageHeadScale"')
        require(EM.set_renderer_property(path, THRUSTERS, renderer_index(path, "Mesh"), "ScaleBinding", binding), "ScaleBinding")
    assert EM.set_renderer_property(path, THRUSTERS, renderer_index(path, "Mesh"), "RendererVisibility", "1")

    if renderer_index(path, "Ribbon") is None:
        require(EM.add_renderer(path, THRUSTERS, "Ribbon"), "Add exhaust ribbon renderer")
    ribbon = renderer_index(path, "Ribbon")
    material = require(unreal.load_asset(STRAND), STRAND)
    for prop, value in [("Material", f"/Script/Engine.Material'{material.get_path_name()}'"), ("Shape", "Plane"),
                        ("MaxNumRibbons", "1"), ("bLinkOrderUseUniqueID", "False"), ("bCastShadows", "False")]:
        require(EM.set_renderer_property(path, THRUSTERS, ribbon, prop, value), prop)


def flight_readback(path, system):
    source = mesh_renderer(unreal.load_asset(FLIGHT)).get_editor_property("Meshes")[0]
    mesh = mesh_renderer(system)
    entry = mesh.get_editor_property("Meshes")[0]
    original = list(source.scale.to_tuple())
    row = flight_row()
    assert "Particles.BarrageHeadScale" in mesh.get_editor_property("ScaleBinding").export_text()
    factor = float(SERVICE.get_parameter(path, "User.VisualScale").current_value)
    length = float(SERVICE.get_parameter(path, "User.TrailLengthCm").current_value)
    fade = float(SERVICE.get_parameter(path, "User.TrailFadeSeconds").current_value)
    rate = float(SERVICE.get_parameter(path, "User.Thrusters_SpawnRate").current_value)
    assert abs(length-row["TrailLengthCm"])<.01 and abs(fade-row["TrailFadeSeconds"])<.0001 and abs(rate*fade-60)<.01
    assert entry.scale == source.scale and factor == row["Scale"]["X"]
    assert mesh.get_editor_property("RendererVisibility") == 1 and renderer_index(path, "Ribbon") is not None
    return dict(head_mesh=entry.mesh.get_path_name(), source_mesh_scale=original, head_scale_multiplier=factor,
                trail_length_cm=length, trail_fade_seconds=fade, ribbon_samples_per_second=rate,
                ribbon_material=STRAND, ribbon_emitter=THRUSTERS, ribbons_per_missile=1)


def active_emitters(path):
    return [str(e.emitter_name) for e in SERVICE.list_emitters(path) if e.is_enabled]


def make_system(source, name, removed):
    dest = BASE + name
    before = active_emitters(source)
    assert set(removed) <= set(before)
    expected = [n for n in before if n not in removed]
    system = unreal.load_asset(dest) if LIB.does_asset_exist(dest) else LIB.duplicate_asset(source, dest)
    assert isinstance(system, unreal.NiagaraSystem) and system.get_path_name().split(".")[0] == dest
    for emitter in removed:
        if SERVICE.emitter_exists(dest, emitter):
            assert SERVICE.remove_emitter(dest, emitter), emitter
    if source == FLIGHT:
        configure_flight(dest, system)
    result = SERVICE.compile_with_results(dest)
    assert result.success and not result.error_count, result.export_text()
    assert active_emitters(dest) == expected and active_emitters(source) == before
    assert LIB.save_loaded_asset(system, False), dest
    ok, error = unreal.EditorLoadingAndSavingUtils.reload_packages(
        [system.get_outer()], unreal.ReloadPackagesInteractionMode.ASSUME_NEGATIVE)
    assert ok and not str(error), str(error)
    system = unreal.load_asset(dest)
    assert active_emitters(dest) == expected
    return dict(source=source, system=dest, originally_active=len(before), active=expected, removed=removed,
                **(flight_readback(dest, system) if source == FLIGHT else {}),
                warnings=list(result.warnings), errors=result.error_count,
                pool_prime=system.get_editor_property("pool_prime_size"))


def author():
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    flight = make_system(FLIGHT, "NS_NoSurvivorsFlight", ["NE_Particulates"])
    # NE_Dust_Trail reads NE_Rock, so keep that producer and remove the independent sprite layer.
    if globals().get("GULI_BARRAGE_FLIGHT_ONLY", False):
        impact = json.loads((OUT / "vfx-readback.json").read_text(encoding="utf8"))["impact"]
        assert active_emitters(impact["system"]) == impact["active"]
    else:
        impact = make_system(IMPACT, "NS_NoSurvivorsImpact", ["NE_Cloud_04", "NE_Bullet_02", "NE_Rock001"])
    return dict(success=True, flight=flight, impact=impact)


try:
    report = author()
    (OUT / "vfx-readback.json").write_text(json.dumps(report, indent=2), encoding="utf8")
except Exception:
    report = dict(success=False, error=traceback.format_exc())
unreal.MCPythonHelper.submit_result(json.dumps(report))
