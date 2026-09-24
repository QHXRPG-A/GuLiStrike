"""Scoped ID migration. Does not rebuild VFX or modify gameplay data. Run after table import."""
import json
import re
import sys
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
sys.path.insert(0,str(ROOT/'Scripts/Vfx'))
from vfx_registry import vfx_id, resource, scale

def configure_blueprints(paths=None):
    service=unreal.BlueprintService
    reports=[]
    for path in paths or ['/Game/GuLiStrike/Vehicles/ElectromagneticMiner/BP_MiningVehicle_TransporterLvl2',
                          '/Game/GuLiStrike/Vehicles/ConstructionVehicle/BP_ConstructionVehicle']:
        component='VfxRegistryBindings'
        changed=False
        if not service.component_exists(path,component):
            if not service.add_component(path,'GuLiVfxBindingComponent',component):
                raise RuntimeError('Failed to add VFX bindings: '+path)
            changed=True
        effect='BuildingConstructionLaser' if '/ConstructionVehicle/' in path else 'MiningLaser'
        value='('+','.join(f'(ComponentName="MiningLaser_{side}",VfxId={vfx_id(effect)})' for side in ['L','R'])+')'
        current=service.get_component_property(path,component,'Bindings')
        if current!=value:
            if not service.set_component_property(path,component,'Bindings',value):raise RuntimeError('Binding write failed')
            changed=True
        for side in ['L','R']:
            name='MiningLaser_'+side
            if service.get_component_property(path,name,'Asset')!='None':
                if not service.set_component_property(path,name,'Asset','None'):raise RuntimeError('Direct VFX removal failed')
                changed=True
        if changed:
            result=service.compile_blueprint(path)
            if not result.success:raise RuntimeError('Blueprint compile failed: '+str(result))
            if not unreal.EditorAssetLibrary.save_asset(path,only_if_is_dirty=True):raise RuntimeError('Blueprint save failed')
        readback=service.get_component_property(path,component,'Bindings')
        if str(vfx_id(effect)) not in readback:raise RuntimeError('Blueprint ID readback mismatch')
        reports.append({'path':path,'changed':changed,'compiled':changed,'bindings':readback})
    return reports

def apply():
    manifest=json.loads((ROOT/'Scripts/Vfx/migration-manifest.json').read_text(encoding='utf-8'))
    changed=set()
    report={'bindings':[], 'blueprints':[]}
    rename={'material':'material_vfx_id','preview_material':'preview_vfx_id','flight_system':'flight_vfx_id','gunfire_system':'gunfire_vfx_id',
            'wingman_laser_system':'wingman_laser_vfx_id','ground_machine_gun_length_scale':'ground_machine_gun_vfx_id',
            'waiting_system':'waiting_vfx_id','active_loop_system':'active_loop_vfx_id'}
    for entry in manifest['uses']:
        match=re.fullmatch(r'(/Game/[^.]+\.[^.]+)\.(.+)',entry['location'])
        if not match:continue
        path,field=match.groups()
        obj=unreal.load_asset(path)
        if obj is None:raise RuntimeError('Missing migration target: '+path)
        id=vfx_id(entry['name'])
        if field=='destruction_proxy_class':
            cdo=unreal.get_default_object(obj.generated_class())
            if cdo.get_editor_property('DestructionVfxId')!=id:
                cdo.set_editor_property('DestructionVfxId',id)
                result=unreal.BlueprintService.compile_blueprint(path)
                if not result.success:raise RuntimeError('NPC Blueprint compile failed')
                changed.add(path)
        elif field in rename:
            prop=rename[field]
            if obj.get_editor_property(prop)!=id:
                obj.set_editor_property(prop,id);changed.add(path)
        elif field=='machine_gun_impact':
            value=obj.get_editor_property(field)
            if value.get_editor_property('vfx_id')!=id:
                value.set_editor_property('vfx_id',id);obj.set_editor_property(field,value);changed.add(path)
        elif field.startswith('activation_variants.'):
            parts=field.split('.')
            variants=list(obj.get_editor_property('activation_variants'))
            value=variants[int(parts[1])]
            if len(parts)==2:
                if value.get_editor_property('vfx_id')!=id:
                    value.set_editor_property('vfx_id',id);variants[int(parts[1])]=value
                    obj.set_editor_property('activation_variants',variants);changed.add(path)
            else:
                layers=list(value.get_editor_property('additional_layers'));layer=layers[int(parts[3])]
                if layer.get_editor_property('vfx_id')!=id:
                    layer.set_editor_property('vfx_id',id);layers[int(parts[3])]=layer
                    value.set_editor_property('additional_layers',layers);variants[int(parts[1])]=value
                    obj.set_editor_property('activation_variants',variants);changed.add(path)
        report['bindings'].append({'location':entry['location'],'vfx_id':id})
    for path in sorted(changed):
        if not unreal.EditorAssetLibrary.save_asset(path,only_if_is_dirty=True):raise RuntimeError('Save failed: '+path)
    report['saved']=sorted(changed)
    report['blueprints']=configure_blueprints()
    return report

if __name__=='__main__':
    result=apply()
    output=ROOT/'outputs/vfx-registry-20260921/configure-report.json'
    output.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(result))
