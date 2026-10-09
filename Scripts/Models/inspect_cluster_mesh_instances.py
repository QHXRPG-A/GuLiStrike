"""Identify mesh instances around the remaining dark cluster without changing them."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008')


def run():
    world = next(s.get_world() for s in unreal.ObjectIterator(unreal.GuLiLocalTeamColorSubsystem)
                 if s.get_world() and 'UEDPIE_2' in s.get_world().get_path_name())
    rows = []
    for component in unreal.ObjectIterator(unreal.InstancedStaticMeshComponent):
        if component.get_world() != world or not component.is_visible(): continue
        points = []
        for index in range(component.get_instance_count()):
            transform = component.get_instance_transform(index, True)
            point = transform.translation
            if -11000 < point.x < -6000 and 65000 < point.y < 70000:
                points.append({'index': index, 'position': list(point.to_tuple()), 'scale': list(transform.scale3d.to_tuple())})
        if points:
            rows.append({'component': component.get_path_name(), 'mesh': component.static_mesh.get_path_name(),
                         'materials': [component.get_material(i).get_path_name() for i in range(component.get_num_materials()) if component.get_material(i)], 'points': points})
    (OUT / 'dark-cluster-mesh-instances.json').write_text(json.dumps(rows, indent=2), encoding='utf8')
    return rows


unreal.MCPythonHelper.submit_result(json.dumps(run()))
