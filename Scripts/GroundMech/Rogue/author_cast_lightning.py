"""Run with Scripts/ue_exec.py. Only the project-owned Niagara copy is edited."""
import json
from pathlib import Path
import unreal

library = unreal.EditorAssetLibrary
service = unreal.NiagaraService
source = "/Game/Assets/Niagara_Toon_Projectiles_2/Effects/NS_Projectile_2"
target = "/Game/GuLiStrike/FX/GroundMech/NoSurvivors/NS_NoSurvivorsCastLightning"
def isolate(path, keep):
    asset = library.load_asset(path) if library.does_asset_exist(path) else library.duplicate_asset(source, path)
    assert asset, path
    for emitter in list(service.list_emitters(path)):
        if emitter.emitter_name != keep:
            assert service.remove_emitter(path, emitter.emitter_name), emitter.emitter_name
    assert service.emitter_exists(path, keep)
    if keep == "Lightning":
        assert service.set_rapid_iteration_param_by_stage(path, keep, "EmitterUpdate", "Constants.Lightning.SpawnRate.SpawnRate", "60")
    else:
        root = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
        cards = json.loads((root / "Data/Json/DT_GuLiStrikeRogueCards_Cards.json").read_text(encoding="utf-8"))
        duration = next(r["CastFlashSeconds"] for r in cards if r["Id"] == "13.01")
        assert service.set_rapid_iteration_param_by_stage(path, keep, "ParticleSpawn", "Constants.Borders.InitializeParticle.Lifetime", str(duration))
    result = service.compile_with_results(path)
    assert result.success, str(result)
    assert library.save_loaded_asset(asset, False)
    return asset.get_path_name()


unreal.MCPythonHelper.submit_result(json.dumps({
    "lightning": isolate(target, "Lightning"),
    "flash": isolate(target.replace("Lightning", "Flash"), "Borders"),
}))
