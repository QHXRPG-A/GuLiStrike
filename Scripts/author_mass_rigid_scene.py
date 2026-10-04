"""Persist editor-only WPO examples/observation points in the existing Mass map.

Real gameplay retains its 500-unit mixed red/blue army. These labelled ISM samples
are editor-only, have no tick/collision, and do not replace the runtime population.
"""
import unreal,json,math,traceback
from pathlib import Path
ROOT=Path('D:/UE5.7/test1');OUT=ROOT/'ArtSource/MechanicalAnimation_20260929'
TAG='MassRigidReview20260929'
api=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
sub=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
lib=unreal.SubobjectDataBlueprintFunctionLibrary
report={'success':False,'runtime_verified':False,'runtime_population':500,'runtime_render_cost_ms':None,'maximum_camera_arm_cm':36000,'actors':[]}
try:
    world=editor.get_editor_world();assert world.get_path_name()=='/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    owned={a.get_actor_label():a for a in api.get_all_level_actors() if TAG in [str(t) for t in a.tags]}
    def spawn(cls,label,location):
        a=owned.get(label) or api.spawn_actor_from_class(cls,unreal.Vector(*location))
        a.set_actor_label(label);a.set_editor_property('tags',[TAG]);a.set_editor_property('is_editor_only_actor',True)
        a.set_folder_path('Review/MassRigidWPO_20260929');return a
    for u in ['WarMachine','Sweeper']:
        a=spawn(unreal.Actor,'MassRigid_'+u,(8000,8000 if u=='WarMachine' else 5000,0))
        c=a.get_component_by_class(unreal.InstancedStaticMeshComponent)
        if not c:
            handles=sub.k2_gather_subobject_data_for_instance(a)
            h,reason=sub.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=handles[0],new_class=unreal.InstancedStaticMeshComponent))
            c=lib.get_object(lib.get_data(h));assert c,str(reason)
        c.set_static_mesh(unreal.load_asset('/Game/Commander/Units/Tactical/Cel/'+u+'/Meshes/SM_'+u+'_Rigid'))
        c.set_editor_property('num_custom_data_floats',51);c.clear_instances()
        c.set_cull_distances(0,0);c.set_cull_distance(0)
        c.set_editor_property('never_distance_cull',True);c.set_editor_property('allow_cull_distance_volume',False)
        c.set_cast_shadow(False);c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        c.set_component_tick_enabled(False)
        samples=[('Neutral',0,0,0,0),('AimLeft',90,15,0,0),('AimRight',-90,-15,0,0),
                 ('HighTarget',45,45 if u=='WarMachine' else 60,0,0),('Move720',0,0,0,5),
                 ('Move1440',0,0,0,15),('LeftShot',0,15,175,0),('RightShot',0,15,-175,0)]
        for i,(label,yaw,pitch,recoil,tilt) in enumerate(samples):
            t=unreal.Transform(location=unreal.Vector((i%4)*1800,(i//4)*1400,0),scale=unreal.Vector(.2,.2,.2))
            idx=c.add_instance(t,False)
            pose=[math.radians(yaw) if u=='WarMachine' else 0,math.radians(pitch),math.radians(pitch),
                  max(recoil,0) if u=='WarMachine' else 0,max(-recoil,0) if u=='WarMachine' else 0,
                  0,math.radians(tilt) if u=='WarMachine' else 0,0,0,0,0,600 if u=='WarMachine' else 0,0,0]
            if u=='Sweeper':pose[7:11]=[i*.7,i*.7,i*.7,i*.7]
            for j,v in enumerate([-1000]+pose+pose+[1,1]+[0]*20):c.set_custom_data_value(idx,j,v,j==50)
        report['actors'].append({'label':a.get_actor_label(),'instances':c.get_instance_count(),'custom_floats':51,'cull_distances':list(c.get_cull_distances()),'component':c.get_path_name()})
    notes=[('MassRigid_Entry',(8000,3000,0),'WPO验收样本：重防号20分区、扫荡者6分区；八列姿态涵盖回正、左右转向、高低目标、5/15度盘倾斜、左右35cm后坐。样本仅编辑器显示，真实PIE继续红蓝混编500单位。新C++需编译后验收。'),
      ('MassRigid_190m',(27000,8000,0),'190米观察点：对照210米与360米；目标不能因距离消失。'),
      ('MassRigid_210m',(29000,8000,0),'210米观察点：跨越旧200米阈值，主体/传送替身/寿命内残骸持续显示。'),
      ('MassRigid_MaxView',(44000,8000,0),'最大指挥视距：当前相机最大臂长36000cm。实机记录实例数量、Draw、GPU ms、Game ms；独立特效/UI可按自身距离预算关闭。'),
      ('MassRigid_RuntimeChecks',(8000,2000,0),'编译后PIE：移动/停止/原地转向/高低目标/连续射击/移速升级/传送/死亡/新生成与混编槽复用。依次执行Presentation.Get、正值Set拒绝、Reset、批次重建；确认两个阵营本体Cull=0。'),
      ('MassRigid_CameraDistance',(8000,1000,0),'指挥官距离采样（需编译新C++）：左上FPS下方显示离地高度和画面中心地面距离，单位米、一位小数，10Hz刷新。缩放、平移至不同高度地形，对照读数与FPS；镜头正下方/中心无地形时分别显示无地形。记录190m/210m/最大镜头的读数、实例数及Game/Draw/GPU ms，切换角色后距离栏应消失。')]
    for label,loc,text in notes:
        a=spawn(unreal.Note,label,loc);a.set_editor_property('text',text);report['actors'].append({'label':a.get_actor_label(),'position_cm':list(a.get_actor_location().to_tuple()),'text':a.get_editor_property('text')})
    report['bake_validation']=str(unreal.GuLiResourceAuthoringLibrary.validate_current_bake())
    assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    report['success']=True;report['saved_map']=world.get_path_name()
except:report['error']=traceback.format_exc()
(OUT/'scene-delivery.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps(report,ensure_ascii=False))
