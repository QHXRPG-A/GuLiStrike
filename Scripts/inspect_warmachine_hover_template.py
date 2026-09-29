import unreal,json,traceback
from pathlib import Path
out=Path('D:/UE5.7/test1/ArtSource/WarMachineHover_20260929');out.mkdir(parents=True,exist_ok=True)
r={}
try:
    ns,em=unreal.NiagaraService,unreal.NiagaraEmitterService
    path='/Game/GuLiStrike/FX/WingmanWeapons/NS_WingmanLaserPool'
    r['summary']=str(ns.summarize(path))
    r['settings']=str(ns.get_all_editable_settings(path))
    r['emitters']=[str(x) for x in ns.list_emitters(path)]
    r['modules']={n:[str(x) for x in em.list_modules(path,n)] for n in ['LaserBolts','LaserMuzzles']}
    r['renderers']={n:[str(x) for x in em.list_renderers(path,n)] for n in ['LaserBolts','LaserMuzzles']}
    r['service_methods']=[n for n in dir(ns) if any(k in n for k in ['setting','target','parameter','simulation'])]
    r['emitter_methods']=[n for n in dir(em) if any(k in n for k in ['setting','target','property','simulation'])]
except:r['error']=traceback.format_exc()
(out/'template-inspection.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
unreal.MCPythonHelper.submit_result(json.dumps({'error':r.get('error'),'summary':r.get('summary'),'service_methods':r.get('service_methods'),'emitter_methods':r.get('emitter_methods')}))
