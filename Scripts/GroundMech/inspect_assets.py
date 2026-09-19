"""Read back the saved GroundMech contracts in a fresh editor process."""
import unreal,json,math,traceback
from pathlib import Path
BASE='/Game/GuLiStrike/GroundMech'
OUT=Path(unreal.Paths.project_dir())/'TestResults/GroundMech'
report={'success':False}
try:
    bp=unreal.load_asset(BASE+'/BP_GroundMech_Light')
    abp=unreal.load_asset(BASE+'/Animations/ABP_GroundMech')
    for asset in (bp,abp,unreal.load_asset(BASE+'/BP_GroundMech_DemoMode')):
        assert unreal.BlueprintService.compile_blueprint(asset.get_path_name())
    cdo=unreal.get_default_object(bp.generated_class())
    components={p:cdo.get_editor_property(p) for p in ('mesh','armor','shoulder','machinegun')}
    report['components']={p:{'mesh':(c.get_skinned_asset() if p in ('mesh','machinegun') else c.static_mesh).get_path_name(),'scale':list(c.relative_scale3d.to_tuple()),'socket':str(c.get_attach_socket_name())} for p,c in components.items()}
    assert components['mesh'].get_editor_property('anim_class')==abp.generated_class()
    report['animation_class']=abp.generated_class().get_path_name()
    assert unreal.get_default_object(abp.generated_class()).get_editor_property('root_motion_mode')==unreal.RootMotionMode.IGNORE_ROOT_MOTION
    report['root_motion']='IgnoreRootMotion: extracted from pose, discarded; CMC owns translation'
    for prop in ('mapping_context','move_action','sprint_action','zoom_action'):assert cdo.get_editor_property(prop)
    context=cdo.get_editor_property('mapping_context')
    mappings=context.get_editor_property('default_key_mappings').get_editor_property('mappings')
    report['input_modifiers']=[{'key':str(m.key.export_text()),'modifiers':[mod.get_class().get_name() for mod in m.modifiers]} for m in mappings]
    for mapping in mappings:
        for modifier in mapping.modifiers:assert modifier.get_outer()==context
    battle=unreal.load_asset(BASE+'/Input/IMC_BattleCommands')
    chord=next(m for m in battle.get_editor_property('default_key_mappings').get_editor_property('mappings') if str(m.key.get_editor_property('key_name'))=='Equals').triggers[0]
    assert chord.get_outer()==battle and chord.get_editor_property('chord_action')
    original='/Game/Assets/MechaController/Mech_Constructor_Lt_Med/Meshes_Skeletal/'
    report['reference_pose']={}
    for part,path in [('Legs','Mech_Legs_Lt'),('Machinegun','Weapons/Weapons_Machinegun_lvl1')]:
        source=list(unreal.SkeletonService.list_bones(original+path))
        target=list(unreal.SkeletonService.list_bones(BASE+'/Meshes/SK_'+part))
        assert len(source)==len(target)
        errors=[]
        for a,b in zip(source,target):
            assert a.bone_name==b.bone_name and a.parent_bone_name==b.parent_bone_name,(part,str(a.bone_name),str(b.bone_name))
            x,y=a.local_transform,b.local_transform
            distance=(x.translation-y.translation).length()
            scale_error=(x.scale3d-y.scale3d).length()
            q,r=x.rotation,y.rotation
            angle=math.degrees(2*math.acos(min(1,abs(q.x*r.x+q.y*r.y+q.z*r.z+q.w*r.w))))
            errors.append({'bone':str(a.bone_name),'cm':distance,'scale':scale_error,'degrees':angle})
        report['reference_pose'][part]=errors
        assert max(e['cm'] for e in errors)<.01,part
        assert max(e['degrees'] for e in errors)<.1,part
        assert max(e['scale'] for e in errors)<.001,part
    report['success']=True
except Exception:report['error']=traceback.format_exc()
(OUT/'asset-validation.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
