import os,unreal,json
from pathlib import Path
assert os.getpid()==39244
s=json.loads(Path('D:/UE5.7/test1/TestResults/AccessRampCrash-20260916/crowd-scene.json').read_text())['result']['corner']
c=unreal.find_object(None,s['component']);t=c.get_instance_transform(s['index'],True)
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'old':s['position'],'current':[t.translation.x,t.translation.y,t.translation.z]}))
