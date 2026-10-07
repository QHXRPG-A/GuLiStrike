import unreal
import json
slot = unreal.StaticMaterial()
unreal.MCPythonHelper.submit_result(json.dumps(dict(success=True,
    copy_doc=getattr(getattr(slot, 'copy', None), '__doc__', None),
    constructor=unreal.StaticMaterial.__doc__,
    struct_methods=[name for name in dir(slot) if 'copy' in name])))
