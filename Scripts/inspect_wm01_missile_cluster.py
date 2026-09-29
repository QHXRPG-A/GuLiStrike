"""Read saved WM01 v3 assets through the editor; no gameplay or asset changes."""
import json
import traceback
import sys
import math
from pathlib import Path
import unreal

ROOT = Path('D:/UE5.7/test1')
OUT = ROOT/'Artifacts/WM01MissileCards/Visual_v3'
sys.path.insert(0,str(ROOT/'Scripts'))
from wm01_missile_visual_config import read_profile
BASE = '/Game/GuLiStrike/FX/WM01Missiles/'
NS = unreal.NiagaraService
EM = unreal.NiagaraEmitterService
SP = unreal.NiagaraScratchPadService
LIB = unreal.EditorAssetLibrary
report = {'success':False,'scope':'saved asset readback; no PIE or performance validation',
          'structure_success':False,'resources_ready':False,'systems':[],'materials':[]}

try:
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    profile, visual_parameters=read_profile(unreal)
    report['projectile_profile']=profile
    report['visual_parameters']=visual_parameters
    for prefix in ['', 'Preview']:
        for quality, lanes in [('Full',24),('Lite',8),('Minimal',0)]:
            path = BASE+'NS_WM01MissileCluster_'+prefix+quality
            system = unreal.load_asset(path)
            assert system, path
            contract = NS.get_parameter(path,'User.MissileContractVersion')
            assert contract.current_value == '2', str(contract)
            row = {'path':path,'lanes':lanes,'contract':int(contract.current_value),
                   'visual_revision':LIB.get_metadata_tag(system,'GuLi.WM01Missiles.VisualRevision'),
                   'summary':str(NS.summarize(path)), 'parameters':[str(p) for p in NS.list_parameters(path)],
                   'diagnostics':unreal.GuLiCombatEffectAuthoringLibrary.get_war_machine_hover_compile_diagnostics(system),
                   'emitters':[]}
            report['systems'].append(row)
            # A deferred/absent GPU shader can report no compile errors. It is
            # still unusable by the runtime readiness gate, so report it apart
            # from the persisted graph/parameter validation below.
            row['runtime_ready'] = ('System valid=1 ready=1 gpu=1' in row['diagnostics']
                and row['diagnostics'].count('GPU finished=1 complete=1') == (3 if lanes else 2))
            assert row['visual_revision'].startswith('3;'), row
            row['visual_defaults']={}
            for name,value in visual_parameters.items():
                actual=float(NS.get_parameter(path,name).current_value)
                assert math.isclose(actual,value,abs_tol=1e-3), (name,actual,value)
                row['visual_defaults'][name]=actual
            assert 'VM ERROR:' not in row['diagnostics'] and 'GPU ERROR:' not in row['diagnostics']
            for emitter in ['Body','Flame']+(['History'] if lanes else []):
                properties = EM.get_emitter_properties(path,emitter)
                assert properties.sim_target == 'GPUComputeSim', str(properties)
                e = {'name':emitter,'properties':str(properties),
                     'renderer':str(EM.get_renderer_details(path,emitter,0)),
                     'burst':str(EM.get_module_info(path,emitter,'SpawnBurst_Instantaneous')),
                     'modules':[str(m) for m in EM.list_modules(path,emitter)],'hlsl':{}}
                row['emitters'].append(e)
                for module in SP.list_scratch_modules(path,emitter):
                    for n in SP.list_nodes(path,emitter,module):
                        if str(n.node_type) == 'CustomHlsl':
                            e['hlsl'][str(module)] = SP.get_custom_hlsl_code(path,emitter,module,str(n.node_id))
                code='\n'.join(e['hlsl'].values())
                if emitter=='Flame':
                    assert 'float2(FlameWidth,FlameLength)' in code and 'float4(9,3.3,.66' in code, code
                    assert 'M_WM01MissileFlame' in e['renderer'], e['renderer']
                if emitter=='History':
                    assert 'pow(life,1.25)' in code and '.70*' in code and 'min(MaximumWidth,' in code, code
                    assert 'float3(.055,.060,.065)' in code and 'smoothstep(0,.08,age)' in code, code
                    assert f'lifetime/({lanes}-1)' in code and '(BucketOut+1)' in code, code
                    assert 'M_WM01MissileHistory' in e['renderer'], e['renderer']
                    assert 'distance(Head,SavedSample)>600' not in code
                    assert 'WidthOut=SmokeWidth' in code and 'FlameLength*(7.0/9.0)' in code
                if prefix:
                    assert 'float2(21,195)' in code and 't<flight?1:2,flight,2.4' in code, code
    for name in ['M_WM01MissileFlame','M_WM01MissileHistory','M_WM01_MissileBody']:
        path=BASE+name
        mat=unreal.load_asset(path)
        diag=unreal.MaterialNodeService.get_material_diagnostics(path)
        assert diag.is_compiled_ok, str(diag)
        row={'path':path,'compiled':bool(diag.is_compiled_ok),'diagnostics':str(diag),
             'blend_mode':str(mat.get_editor_property('blend_mode')),
             'shading_model':str(mat.get_editor_property('shading_model')),
             'outputs':[str(c) for c in unreal.MaterialNodeService.get_output_connections(path)],
             'graph':json.loads(unreal.MaterialNodeService.export_material_graph(path))}
        report['materials'].append(row)
    dt=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects')
    rows=json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(dt))
    report['vfx_rows']=[r for r in rows if str(r.get('Id')) in ['2','49','50','51']]
    assert len(report['vfx_rows'])==4, report['vfx_rows']
    definition=unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_WM01_Missile')
    report['production_cluster_enabled']=bool(definition.get_editor_property('use_missile_cluster_rendering'))
    assert not report['production_cluster_enabled']
    catalog=unreal.load_asset('/Game/GuLiStrike/Commander/Skills/DA_CommanderSkills_V1')
    skill=next(s for s in catalog.skills if str(s.skill_id)=='WM01_HomingMissile')
    report['active_skill']={k:str(skill.get_editor_property(k)) for k in ['skill_id','range_source_slot','range_multiplier','range_centimeters','cooldown_seconds','configuration']}
    assert math.isclose(skill.range_multiplier,3.2,rel_tol=1e-6) and skill.range_centimeters==9600 and skill.cooldown_seconds==6
    assert str(skill.range_source_slot)=='BasicAttack'
    assert skill.configuration.target_area_diameter_centimeters==1600
    fields=unreal.load_asset('/Game/GuLiStrike/Data/DT_GuLiStrikeSpellFields_Fields')
    explosion=next(r for r in json.loads(unreal.DataTableFunctionLibrary.export_data_table_to_json_string(fields)) if r['Name']=='WM01_MissileExplosion')
    report['explosion_field']=explosion
    assert explosion['RadiusCentimeters']==300 and explosion['Damage']==30
    impact=definition.get_editor_property('impact_field')
    report['explosion_visual_reference_radius']=impact.get_editor_property('visual_reference_radius')
    report['structure_success']=True
    report['resources_ready']=all(row['runtime_ready'] for row in report['systems'])
    assert report['resources_ready'], 'Saved structure passed, but Niagara resources are not ready: '+', '.join(
        row['path'] for row in report['systems'] if not row['runtime_ready'])
    report['success']=True
except Exception:
    report['error']=traceback.format_exc()
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'asset-readback.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'systems':len(report['systems']),
    'materials':len(report['materials']),'error':report.get('error')}))
