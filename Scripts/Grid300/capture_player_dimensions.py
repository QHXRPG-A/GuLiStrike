"""Read the live player CDO before its integer-footprint migration."""
import json
from pathlib import Path
import unreal

out=Path(unreal.Paths.project_dir()).resolve()/'ArtSource/Navigation/Grid300/Baseline/player_dimensions.json'
if not out.exists():
    cdo=unreal.get_default_object(unreal.GuLiGroundMechCharacter)
    capsule=cdo.get_component_by_class(unreal.CapsuleComponent)
    mesh=cdo.get_component_by_class(unreal.SkeletalMeshComponent)
    result={'schema':'guli-grid300/player-dimensions-v1','class':cdo.get_class().get_path_name(),
            'radius_cm':capsule.get_scaled_capsule_radius(),'half_height_cm':capsule.get_scaled_capsule_half_height(),
            'mesh_scale':list(mesh.get_editor_property('relative_scale3d').to_tuple()),'mesh_location':list(mesh.get_editor_property('relative_location').to_tuple())}
    with out.open('x',encoding='utf-8') as f:json.dump(result,f,ensure_ascii=False,indent=2)
unreal.MCPythonHelper.submit_result(json.dumps({'path':str(out),'data':json.loads(out.read_text(encoding='utf-8'))}))
