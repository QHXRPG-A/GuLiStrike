"""Per-frame cosmetic input probe. Run in our prepared PIE, outside all timing windows."""
import unreal,json
from pathlib import Path
OUT=Path(unreal.Paths.project_dir())/'outputs/performance/20261010-muzzle-batch'
_muzzle_probe_worlds=list(unreal.EditorLevelLibrary.get_pie_worlds(True))[1:]
assert len(_muzzle_probe_worlds)==2
_muzzle_probe_frames=[]
_muzzle_probe_fx=[next(s for s in unreal.ObjectIterator(unreal.GuLiCombatEffectPresentationSubsystem) if s.get_outer()==w) for w in _muzzle_probe_worlds]
_muzzle_probe_handle=None
def _muzzle_probe_tick(dt):
 global _muzzle_probe_handle
 rows=[]
 for w,fx in zip(_muzzle_probe_worlds,_muzzle_probe_fx):
  owner=json.loads(fx.get_muzzle_protocol_snapshot())
  slots=[s for s in owner['slots'] if s['shot'].upper().startswith('47534D5A')]
  identities={(s['slot'],s['generation']) for s in slots}
  systems=[]
  for c in unreal.ObjectIterator(unreal.NiagaraComponent):
   if c.get_world()!=w or not c.is_active() or not c.get_asset() or not c.get_asset().get_name().startswith('NS_MachineGunMuzzle_Batch'):continue
   data=json.loads(unreal.GuLiCombatEffectAuthoringLibrary.inspect_impact_batch_component(c))
   for e in data.get('emitters',[]):e['rows']=[r for r in e['rows'] if (r['slot'],r['generation']) in identities]
   systems.append(data)
  rows.append({'world':w.get_path_name(),'frame':owner['frame'],'slots':slots,'systems':systems})
 _muzzle_probe_frames.append(rows)
 if len(_muzzle_probe_frames)>=20:
  unreal.unregister_slate_post_tick_callback(_muzzle_probe_handle);_muzzle_probe_handle=None
  (OUT/'contract-frame-readback.json').write_text(json.dumps({'frames':_muzzle_probe_frames},ensure_ascii=False,indent=2),encoding='utf-8')
_muzzle_probe_inputs=[]
for wi,(w,fx) in enumerate(zip(_muzzle_probe_worlds,_muzzle_probe_fx)):
 fx.reset_review_muzzles()
 for i in range(8):
  identity=100001+wi*10000+i
  p=unreal.Vector(-10800+i*150,65000+wi*400,1600)
  accepted=fx.emit_review_muzzle_input(identity,p,unreal.Rotator(0,i*35,0),i%2==0,2,unreal.Vector(700,200,0))
  duplicate=fx.emit_review_muzzle_input(identity,p,unreal.Rotator(0,i*35,0),i%2==0,2,unreal.Vector(700,200,0))
  assert accepted and not duplicate
  _muzzle_probe_inputs.append({'world':w.get_path_name(),'identity':identity,'position':list(p.to_tuple()),'heavy':i%2==0,'duplicate_rejected':True})
(OUT/'contract-inputs.json').write_text(json.dumps(_muzzle_probe_inputs,indent=2),encoding='utf-8')
_muzzle_probe_handle=unreal.register_slate_post_tick_callback(_muzzle_probe_tick)
unreal.MCPythonHelper.submit_result(json.dumps({'success':True,'inputs':len(_muzzle_probe_inputs),'frames_requested':20}))
