"""Read motion function wiring and texture references while PIE is running."""
import json
from pathlib import Path
import unreal

report = []
for p in ['/Game/GuLiStrike/Commander/Units/WM01/LOD_3Tier_v1/Dependencies/Commander/Units/MechanicalAnimation/MF_GuLiRigidMechanical', '/Game/GuLiStrike/Commander/Units/DefaultSoldier/LOD_3Tier_v1/VAT/MF_Pioneer_BoneVAT']:
    function = unreal.load_asset(p)
    nodes = []
    for node in unreal.ObjectIterator(unreal.MaterialExpression):
        if node.get_outer() != function:
            continue
        detail = {'node': node.get_name(), 'type': node.get_class().get_name()}
        if isinstance(node, unreal.MaterialExpressionCustom):
            detail['code'] = node.get_editor_property('code')
        if isinstance(node, unreal.MaterialExpressionTextureCoordinate):
            detail['uv'] = node.get_editor_property('coordinate_index')
        if isinstance(node, unreal.MaterialExpressionTextureObject):
            texture = node.get_editor_property('texture')
            detail['texture'] = texture.get_path_name() if texture else None
        if isinstance(node, unreal.MaterialExpressionVertexColor):
            detail['vertex_color_payload'] = True
        nodes.append(detail)
    report.append({'path': p, 'nodes': nodes})
out = {'success': True, 'functions': report}
Path('D:/UE5.7/test1/Artifacts/ModelRegistryRuntime20261008/motion-functions-readback.json').write_text(json.dumps(out, indent=2), encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(out))
