"""Measure the horizontal envelope of the two 1x bombardment cores in the editor, without rendering."""
import json
import math
import re
import traceback
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT = ROOT / "Artifacts/GroundNoSurvivors"
SYSTEM = "/Game/GuLiStrike/FX/GroundMech/NoSurvivors/NS_NoSurvivorsImpact"
CORES = {"NE_Core01", "NE_Core02"}


def values(s):
    return tuple(float(v) for v in re.findall(r"[XYZW]=([-+0-9.eE]+)",s))


def vertices(path):
    mesh = unreal.load_asset(path)
    points = set()
    for section in range(mesh.get_num_sections(0)):
        result = unreal.ProceduralMeshLibrary.get_section_from_static_mesh(mesh,0,section)
        points.update(tuple(p.to_tuple()) for p in result[0])
    assert points, path
    return points


def horizontal_edge(particle, mesh_scale, points):
    px,py,_ = values(particle["position"])
    sx,sy,sz = [a*b for a,b in zip(values(particle["scale"]),mesh_scale)]
    x,y,z,w = values(particle.get("orientation","X=0 Y=0 Z=0 W=1"))
    # First two rows of the particle rotation, applied to real LOD0 vertices.
    a,b,c = (1-2*(y*y+z*z))*sx, 2*(x*y-z*w)*sy, 2*(x*z+y*w)*sz
    d,e,f = 2*(x*y+z*w)*sx, (1-2*(x*x+z*z))*sy, 2*(y*z-x*w)*sz
    return math.sqrt(max((px+a*i+b*j+c*k)**2+(py+d*i+e*j+f*k)**2 for i,j,k in points))


def measure():
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    system = unreal.load_asset(SYSTEM)
    geometry, maxima, observed = {}, [], set()
    for run in range(8):
        actor = unreal.NiagaraFunctionLibrary.spawn_system_at_location(
            world,system,unreal.Vector(0,0,1000),auto_destroy=False,auto_activate=False,pre_cull_check=False)
        assert actor
        peak, event = 0., {}
        try:
            actor.set_visibility(False)
            actor.set_variable_float("User.Area_Scale",1.)
            actor.activate(True)
            for frame in range(1,121):
                actor.advance_simulation(1,1/60)
                sample=json.loads(unreal.GuLiGroundMechFireEditorLibrary.read_explosive_impact_particles(actor))
                if not actor.is_active() and peak>0: break
                for emitter in sample.get("emitters",[]):
                    if emitter["emitter"] not in CORES or not emitter["particles"]: continue
                    observed.add(emitter["emitter"])
                    assert len(emitter["meshes"])==1, emitter["emitter"]
                    mesh=emitter["meshes"][0]
                    path=mesh["mesh"]
                    if path not in geometry: geometry[path]=vertices(path)
                    for particle in emitter["particles"]:
                        edge=horizontal_edge(particle,values(mesh["mesh_scale"]),geometry[path])
                        if edge>peak:
                            peak=edge
                            event=dict(emitter=emitter["emitter"],time_s=frame/60,mesh=path)
        finally:
            actor.deactivate()
            actor.destroy_component(actor.get_owner() or actor)
        assert peak>0, "No measurable live core geometry"
        maxima.append(dict(peak_cm=peak,**event))
    assert observed==CORES, observed
    return dict(success=True,system=SYSTEM,area_scale=1,component_scale=1,radius_cm=math.ceil(max(v["peak_cm"] for v in maxima)),
                samples=maxima,vertices={k:len(v) for k,v in geometry.items()},step_seconds=1/60,
                basis="Peak horizontal LOD0 core mesh envelope at its sampled particle position, scale and rotation. Smoke and debris excluded.")


try:
    report=measure()
except Exception:
    report=dict(success=False,error=traceback.format_exc())
OUT.mkdir(parents=True,exist_ok=True)
(OUT/"calibration-optimized.json").write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding="utf8")
unreal.MCPythonHelper.submit_result(json.dumps({k:v for k,v in report.items() if k in ("success","radius_cm","error")}))
