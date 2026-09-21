"""Static registry/asset checks, no PIE, actors, particle playback or maps are created."""
import json
import re
import sys
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'outputs/vfx-registry-20260921'
sys.path.insert(0,str(ROOT/'Scripts/Vfx'))
from vfx_registry import rows, definition, resource, scale, vfx_id

def run():
    report={'errors':[], 'resources':[], 'bindings':[], 'blueprints':[], 'cook_dependencies':[]}
    def check(condition,message):
        if not condition:report['errors'].append(message)
    registry=unreal.AssetRegistryHelpers.get_asset_registry()
    materials={'GroundWarning','RocketFuelBar','TransitEnergy','TeamOutline','UnitWreck','UnitHit','UnitHitInstanced',
               'UnitHitHealthBar','BlinkAfterimage','BlinkHeatwave','TeleportGround','TeleportBeam','TeleportBody','UnitRingMaterial','BuildingPlacement'}
    meshes={'BlinkSphere','TeleportCylinder','UnitRingMesh','TransitOrb','TransitNode','TransitEdge','FuelBarPlane'}
    for row in rows():
        p=row['ResourcePath']
        asset=unreal.load_object(None,p)
        cls=unreal.Class if row['Name']=='NpcDestruction' else (unreal.MaterialInterface if row['Name'] in materials else (unreal.StaticMesh if row['Name'] in meshes else unreal.NiagaraSystem))
        check(isinstance(asset,cls),f'Wrong/missing resource type: {row["Id"]} {p}')
        result=unreal.GuLiVfxRegistrySubsystem.get_definition_without_world(row['Id'])
        check(result is not None,f'Native registry could not resolve {row["Id"]}')
        if result:
            check(result.resource_path.get_path_name()==p,f'Native path mismatch: {row["Id"]}')
            actual=result.scale
            check(all(abs(getattr(actual,a.lower())-row['Scale'][a])<1.e-5 for a in 'XYZ'),f'Native scale mismatch: {row["Id"]}')
        report['resources'].append({'id':row['Id'],'path':p,'class':asset.get_class().get_name() if asset else None,'scale':row['Scale']})
    manifest=json.loads((ROOT/'Scripts/Vfx/migration-manifest.json').read_text(encoding='utf-8'))
    rename={'material':'material_vfx_id','preview_material':'preview_vfx_id','flight_system':'flight_vfx_id','gunfire_system':'gunfire_vfx_id',
            'wingman_laser_system':'wingman_laser_vfx_id','ground_machine_gun_length_scale':'ground_machine_gun_vfx_id',
            'waiting_system':'waiting_vfx_id','active_loop_system':'active_loop_vfx_id'}
    for entry in manifest['uses']:
        id=entry['vfx_id']
        check(resource(id)==entry['resource'] and all(abs(a-b)<1.e-6 for a,b in zip(scale(id),entry['scale'])),
              'Migration resource/scale changed: '+entry['location'])
        match=re.fullmatch(r'(/Game/[^.]+\.[^.]+)\.(.+)',entry['location'])
        if match:
            p,field=match.groups();obj=unreal.load_asset(p)
            if field=='destruction_proxy_class':actual=unreal.get_default_object(obj.generated_class()).get_editor_property('DestructionVfxId')
            elif field in rename:actual=obj.get_editor_property(rename[field])
            elif field=='machine_gun_impact':actual=obj.machine_gun_impact.vfx_id
            else:
                parts=field.split('.');value=obj.activation_variants[int(parts[1])]
                actual=value.vfx_id if len(parts)==2 else value.additional_layers[int(parts[3])].vfx_id
                if len(parts)==2 and str(value.scale_parameter_name) not in ('', 'None'):
                    sizes=scale(actual)
                    check(max(sizes)-min(sizes)<1.e-6, 'Niagara float parameter requires uniform base scale: '+entry['location'])
            check(actual==id,'Saved asset ID mismatch: '+entry['location'])
        report['bindings'].append({'location':entry['location'],'vfx_id':id,'matched':True})
    options=unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,include_hard_package_references=True)
    packages=set(str(p) for p in registry.get_dependencies('/Game/GuLiStrike/Data/DT_GuLiStrikeVfx_Effects',options))
    for row in rows():
        package=row['ResourcePath'].split('.')[0]
        check(package in packages,'Missing DataTable Cook dependency: '+package)
    report['cook_dependencies']=sorted(packages)
    check('DirectoriesToAlwaysCook=(Path="/Game/GuLiStrike/Data")' in (ROOT/'Config/DefaultGame.ini').read_text(encoding='utf-8'),
          'VFX DataTable folder must be reachable by Cook')
    for path in ['/Game/GuLiStrike/GroundMech/BP_GroundMech_Light',
                 '/Game/GuLiStrike/Blueprints/AI/BP_GuLiStrikeNPC.BP_TwinStickNPC',
                 '/Game/GuLiStrike/Blueprints/AI/BP_GuLiStrikeNPCDestruction.BP_TwinStickNPCDestruction',
                 '/Game/GuLiStrike/Vehicles/ElectromagneticMiner/BP_MiningVehicle_TransporterLvl2',
                 '/Game/GuLiStrike/Vehicles/ConstructionVehicle/BP_ConstructionVehicle']:
        result=unreal.BlueprintService.compile_blueprint(path)
        check(result.success,'Blueprint compile failed: '+path+' '+str(result))
        check(unreal.EditorAssetLibrary.save_asset(path,only_if_is_dirty=True),'Blueprint save failed: '+path)
        if 'Vehicle' in path:
            for side in ['L','R']:
                check(unreal.BlueprintService.get_component_property(path,'MiningLaser_'+side,'Asset')=='None','Blueprint direct Niagara asset remains: '+path)
        report['blueprints'].append({'path':path,'compiled':bool(result.success)})
    settings=unreal.get_default_object(unreal.load_class(None,'/Script/GuLiStrike.GuLiUnitFeedbackSettings'))
    check(list(settings.get_editor_property('ExplosionVfxIds'))==[vfx_id('GroundDestruction')],'Ground destruction config mismatch')
    check(list(settings.get_editor_property('WingmanExplosionVfxIds'))==[vfx_id('WingmanDestruction')],'Wingman destruction config mismatch')
    catalog=unreal.load_asset('/Game/GuLiStrike/FX/CommanderWeapons/DA_CommanderCombatEffects')
    check(catalog.machine_gun_impact.vfx_id==vfx_id('MachineGunImpact'),'Shared three-gun impact ID mismatch')
    check(catalog.machine_gun_impact.maximum_lifetime==3,'Shared impact lifetime changed')
    report['success']=not report['errors']
    return report

if __name__=='__main__':
    try:
        result=run()
    except Exception:
        import traceback
        result={'success':False,'error':traceback.format_exc()}
    (OUT/'static-validation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({'success':result['success'],'errors':result.get('errors',[]),'error':result.get('error',''),
                     'resources':len(result.get('resources',[])),'bindings':len(result.get('bindings',[]))}))
