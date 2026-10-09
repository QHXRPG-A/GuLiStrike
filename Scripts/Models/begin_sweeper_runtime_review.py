"""Begin the narrowly scoped live check explicitly authorized in this session."""
import json
from pathlib import Path
import unreal

OUT = Path('D:/UE5.7/test1/ArtSource/SweeperTeamColor_v1_20261008/UE')
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
if levels.is_in_play_in_editor():
    raise RuntimeError('Do not interrupt an existing player session.')
settings = unreal.load_object(None, '/Script/UnrealEd.Default__LevelEditorPlaySettings')
prior = {'clients': settings.get_editor_property('PlayNumberOfClients'),
         'one_process': settings.get_editor_property('RunUnderOneProcess')}
try:
    prior['mode'] = int(settings.get_editor_property('PlayNetMode'))
except Exception as error:
    # This enum is unexposed by the local engine; conversion reports its raw value.
    message = str(error)
    if "Cannot pythonize '2'" not in message:
        raise
    prior['mode'] = 2
    prior['mode_readback'] = 'Unexposed enum conversion reports current raw value 2'
record = {'prior_settings': prior,
          'authorization': '同一会话用户要求：导入新 DataTable 并验证运行效果；当前：导入 UE、接入自动改色。',
          'scope': 'Two clients; real Pioneer Q summons; ModelId1005 orange mask and CPD only'}
(OUT / 'runtime-session.json').write_text(json.dumps(record, ensure_ascii=False, indent=2), encoding='utf8')
if not unreal.GuLiTeleportQALibrary.configure_pie(2, 2):
    raise RuntimeError('PIE configuration rejected.')
levels.editor_request_begin_play()
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'began_two_client_pie':True,'prior_settings':prior}))
