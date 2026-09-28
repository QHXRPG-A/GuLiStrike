"""Read marketplace card material/texture contracts; export reference pixels only."""
import json, traceback
from pathlib import Path
import unreal

OUT=Path('D:/UE5.7/test1/ArtSource/UI/WarMachineTarotCards')
(OUT/'References').mkdir(parents=True,exist_ok=True)
(OUT/'Inspection').mkdir(exist_ok=True)
SRC='/Game/Assets/card/ParallaxCardMaterial'
report={'textures':{},'graphs':{},'success':False}
try:
    report['world']=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world().get_path_name()
    report['pie']=unreal.WidgetService.is_pie_running()
    for name in ['M_Card_Demo','M_Card_Demo_UI']:
        path=SRC+'/Materials/'+name
        graph=unreal.MaterialNodeService.export_material_graph(path)
        (OUT/'Inspection'/(name+'.json')).write_text(graph,encoding='utf-8')
        report['graphs'][name]=str(unreal.MaterialService.get_material_info(path))
    for name in ['Moon','Star','Tower']:
        for suffix in ['', '_UI']:
            report['graphs']['MI_Card_'+name+suffix]=str(unreal.MaterialService.get_instance_info(SRC+'/Materials/MI_Card_'+name+suffix))
        for suffix in ['D','M','UI']:
            name2='T_Card_'+name+'_'+suffix
            t=unreal.load_asset(SRC+'/Textures/'+name2)
            if not t:continue
            task=unreal.AssetExportTask()
            task.object=t
            task.filename=str(OUT/'References'/(name2+'.png'))
            task.automated=True
            task.prompt=False
            task.replace_identical=True
            ok=unreal.Exporter.run_asset_export_task(task)
            report['textures'][name2]={'size':[t.blueprint_get_size_x(),t.blueprint_get_size_y()],
                'srgb':t.srgb,'compression':str(t.compression_settings),'exported':ok,'errors':list(task.errors)}
    report['success']=True
except Exception: report['error']=traceback.format_exc()
(OUT/'Inspection'/'source-contract.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:report[k] for k in ['success','error','world','pie','textures'] if k in report}))
