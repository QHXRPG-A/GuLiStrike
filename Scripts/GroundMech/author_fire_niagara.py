"""Refine only the two project-owned fire candidates; source marketplace assets are read-only."""
import unreal, json, traceback
from pathlib import Path

OUT=Path('D:/UE5.7/test1/TestResults/GroundMech/Fire')
BASE='/Game/GuLiStrike/FX/GroundMech/NS_GroundMech_'
NS=unreal.NiagaraService
EM=unreal.NiagaraEmitterService
REPORT={'success':False,'systems':{}}

def path(kind): return BASE+kind.capitalize()
def command(value): unreal.SystemLibrary.execute_console_command(None,value)
def override(kind,emitter,module,name,typ,value):
    # Uses the native Niagara editor override-pin API for linked/dynamic inputs.
    command('gs.MechFire.QA.Input '+ ' '.join(str(v).replace(' ','~') for v in [kind,emitter,module,name,typ,value]))
def switch(kind,e,module,name,value):
    assert EM.set_module_input(path(kind),e,module,name,value),(kind,e,module,name,value)
def remove(kind,e,name):
    if any(m.module_name==name for m in EM.list_modules(path(kind),e)):
        assert EM.remove_module(path(kind),e,name),(kind,e,name)
def once(kind,e,duration):
    for name,value in [('Life Cycle Mode','NewEnumerator1'),('Loop Behavior','NewEnumerator1'),('Inactive Response','NewEnumerator0'),('UseLoopDelay','false'),('Enable Distance Culling','false'),('Enable Visibility Culling','false'),('Scale Spawn Count','false')]:
        switch(kind,e,'EmitterState',name,value)
    override(kind,e,'EmitterState','Loop Duration','float',duration)
def keep(kind,names):
    for emitter in NS.list_emitters(path(kind)):
        if emitter.emitter_name not in names:
            assert NS.remove_emitter(path(kind),emitter.emitter_name)
def finish(kind):
    command('gs.MechFire.QA.AuthorEmitterSpace '+kind)
    result=NS.compile_with_results(path(kind))
    REPORT['systems'][kind]={'compile':str(result),'emitters':[]}
    assert result.success,str(result)
    assert NS.save_system(path(kind))
    for e in NS.list_emitters(path(kind)):
        REPORT['systems'][kind]['emitters'].append({'name':e.emitter_name,'properties':str(EM.get_emitter_properties(path(kind),e.emitter_name)), 'modules':[{'name':m.module_name,'stage':m.module_type,'path':m.script_asset_path,'enabled':m.is_enabled} for m in EM.list_modules(path(kind),e.emitter_name)],'renderers':[str(EM.get_renderer_details(path(kind),e.emitter_name,r.renderer_index)) for r in EM.list_renderers(path(kind),e.emitter_name)]})

try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    e='NE_forward_simple_bullet'
    keep('bullet',{e})
    for module in ['SpawnRate','SolveForcesAndVelocity','SpriteSizeScaleBySpeed']:
        remove('bullet',e,module)
    if not any(m.module_name=='SpawnBurst_Instantaneous' for m in EM.list_modules(path('bullet'),e)):
        scripts=list(EM.search_module_scripts('SpawnBurst_Instantaneous',''))
        assert len(scripts)==1,scripts
        assert EM.add_module(path('bullet'),e,scripts[0],'EmitterUpdate')
    once('bullet',e,.01)
    for module,name,typ,value in [('SpawnBurst_Instantaneous','Spawn Count','int',1),('SpawnBurst_Instantaneous','Spawn Time','float',0),('InitializeParticle','Lifetime','float',5.1),('InitializeParticle','Sprite Size','vec2','(X=12,Y=90)'),('AddVelocity','Velocity','vec3','(X=12000,Y=0,Z=0)')]:
        override('bullet',e,module,name,typ,value)
    finish('bullet')
    keep('muzzle',{'NE_gun_nozzle_fire','NE_spread_spark'})
    for e in ('NE_gun_nozzle_fire','NE_spread_spark'):
        remove('muzzle',e,'SpawnRate')
        once('muzzle',e,.01)
        override('muzzle',e,'InitializeParticle','Lifetime','float',.08 if e=='NE_gun_nozzle_fire' else .16)
    e='NE_spread_spark'
    for module,name,typ,value in [('SpawnBurst_Instantaneous','Spawn Count','int',5),('AddVelocityInCone','Cone Axis','vec3','(X=1,Y=0,Z=0)'),('AddVelocityInCone','Cone Angle','float',28),('AddVelocityInCone','Velocity Strength','float',900)]:
        override('muzzle',e,module,name,typ,value)
    e='NE_gun_nozzle_fire'
    for name in ('Beam Start Tangent','Beam End Tangent'):
        override('muzzle',e,'BeamEmitterSetup001',name,'vec3','(X=1,Y=0,Z=0)')
    assert NS.set_rapid_iteration_param(path('muzzle'),e,'Constants.'+e+'.Multiply_Float002.A','130')
    assert NS.set_parameter(path('muzzle'),'User.GlobleDuration','0.08')
    for kind in ('bullet','muzzle'):
        settings=NS.get_all_editable_settings(path(kind))
        for param in settings.user_parameters:
            if 'audio' in param.setting_path.lower() or 'ahdio' in param.setting_path.lower():
                assert NS.set_parameter(path(kind),param.setting_path,'false')
    finish('muzzle')
    assert NS.save_system(path('bullet'))
    command('gs.MechFire.QA.NiagaraReadback')
    graph=json.loads((OUT/'niagara-graph.json').read_text(encoding='utf-8'))
    def read(kind,e,name):
        return next(p for node in graph if node['system']==kind and node['emitter']==e for p in node['inputs'] if p['name']==name)
    for kind,e,name,value in [('bullet','NE_forward_simple_bullet','InitializeParticle.Lifetime',5.1),('bullet','NE_forward_simple_bullet','SpawnBurst_Instantaneous.Spawn Count',1),('muzzle','NE_spread_spark','InitializeParticle.Lifetime',.16),('muzzle','NE_spread_spark','SpawnBurst_Instantaneous.Spawn Count',5)]:
        pin=read(kind,e,name)
        assert not pin['links'] and abs(float(pin['default'])-value)<.0001,(kind,e,name,pin)
    REPORT['override_readback']=True
    REPORT['success']=True
except Exception:
    REPORT['error']=traceback.format_exc()
(OUT/'niagara-authoring.json').write_text(json.dumps(REPORT,indent=2,ensure_ascii=False),encoding='utf-8')
print(json.dumps({'success':REPORT['success'],'error':REPORT.get('error'),'compiles':{k:v['compile'] for k,v in REPORT['systems'].items()}}))
