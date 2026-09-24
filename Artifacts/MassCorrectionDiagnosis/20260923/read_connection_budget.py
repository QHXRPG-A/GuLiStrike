import json
from pathlib import Path
import unreal

result={'worlds':[]}
for index in range(3):
    w=unreal.find_object(None,'/Game/Maps/UEDPIE_{}_LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'.format(index))
    if not w: continue
    row={'index':index,'delta':unreal.GameplayStatics.get_world_delta_seconds(w),'controllers':[]}
    for pc in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.PlayerController):
        item={'controller':pc.get_path_name(),'connections':[]}
        for prop in ('Player','NetConnection'):
            try:
                obj=pc.get_editor_property(prop)
                if not obj: continue
                objrow={'via':prop,'object':obj.get_path_name(),'class':obj.get_class().get_path_name()}
                for key in ('CurrentNetSpeed','ConfiguredInternetSpeed','ConfiguredLanSpeed'):
                    try: objrow[key]=obj.get_editor_property(key)
                    except Exception as exc: objrow[key]=str(exc)
                item['connections'].append(objrow)
            except Exception as exc:
                item[prop+'_read_error']=str(exc)
        row['controllers'].append(item)
    result['worlds'].append(row)
for path,keys in (
    ('/Script/Engine.Default__GameNetworkManager',('TotalNetBandwidth','MaxDynamicBandwidth','MinDynamicBandwidth')),
    ('/Script/OnlineSubsystemUtils.Default__IpNetDriver',('MaxClientRate','MaxInternetClientRate'))):
    obj=unreal.find_object(None,path)
    if obj:
        values={}
        for key in keys:
            try: values[key]=obj.get_editor_property(key)
            except Exception as exc: values[key]=str(exc)
        result[path]=values
unreal.MCPythonHelper.submit_result(json.dumps(result))
