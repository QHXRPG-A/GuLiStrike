"""Save review instructions on the existing Ground area and certify startup dependencies."""
import ast
import json
import traceback
from pathlib import Path
import unreal

ROOT=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
OUT=ROOT/'Artifacts/GroundAlternatingLoop_20261108'
MAP='/Game/Maps/LVL_CommanderMassPrototype'
PREFIX='GroundAmmo_'

def require(value,message):
    if not value:raise RuntimeError(message)
    return value

def main():
    require(all(json.loads((OUT/('asset_install_'+stage+'.json')).read_text(encoding='utf8'))['success'] for stage in ('animation','missile','tables')),'Load and save animations, VFX and tables first')
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    require(not level.is_in_play_in_editor(),'Preserve active PIE')
    require(not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages() or globals().get('GULI_FIRE_MAP_RETRY',False),'Preserve unsaved map edits')
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    require(world.get_path_name().split('.')[0]==MAP,'Use the existing review map')
    sub=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors={a.get_actor_label():a for a in sub.get_all_level_actors()}
    require(all(PREFIX+n in actors for n in ['PrimaryDamage','BlastCenter','BlockingWall','ObserveDamage','ObserveExplosion']),'Existing review entities')
    before={k:a.get_actor_transform().to_tuple() for k,a in actors.items()}
    notes={
        'Entry':'新局进入红队Ground游骑兵。控制台执行 py "D:/UE5.7/test1/Scripts/GroundMech/Rogue/enter_ammo_review.py" 可到观察点；关闭控制台后左键开火、F4选牌。初始普攻6/9/12，强化10/15/20；初始弹长度3000cm、宽度不变、直线；先驱者/扫荡者当前长度再减半，重防号与强化弹不变。对准PrimaryDamage靶受击中心，使用ReadState读取flight_settlement.shots的ShotId、collision_hit、frozen_damage、damage_submitted、receipt_found、applied/absorbed/remaining_health，不能只看血条。',
        'FireNotify':'Anim_GroundFire_AlternatingLoop长1秒，单一Montage循环：右Fire=0、左Fire=0.5；播放倍率R/2。真实通知携带枪口侧，当前步骨骼更新后调用与Mass相同的公共Launch。按住持续循环，松开停止新增发射，重按保留下一侧与剩余冷却；升级保留相位。按fire_ordinal排序逐发检查严格右左交替及1/R间隔，不能只比较总数。回执不决定出弹顺序，失败单发不暂停战斗。',
        'AttackRate':'09.01一级、二级实际攻速R*1.08和R*1.08^2。R=4/8/12时最大等级26/17/11；下一等级不入候选且确认再次校验，超30组合换档拒绝。升级保留剩余冷却比例，动画与后坐恢复随实际攻速同步。',
        'Explosion':'11.01等级1/2/3每5/4/2发附爆炸弹；打空计数。左右当前枪口分别向上身外侧偏移50cm，方向平行普攻，不能随枪管旋转。弹速16800cm/s（普攻70%），射程120000cm、寿命约7.14s、半径15cm。主弹先命中后，附弹继续独立飞行；近墙外移路径受阻时立即在首阻点爆炸，不穿墙出生。弹体和拖尾尺寸2倍，命中爆炸大小不变。',
        'TrailAndDamage':'项目副本组合Missile_Cluster/飞弹载体、VFX_Missile飞行主体/烟火/Ribbon、NS_Explosion_19命中爆炸。世界空间尾迹停留0.12s、淡出0.38s；弹体结束后尾迹继续消散。未强化也造成20/30/40、半径1500cm、无衰减，仅一次范围伤害。分别观察Inside12m、Boundary15m、Outside24m、Friendly、Shield、BehindWall靶；沿用受击体/护盾/屏障规则，墙挡弹但现有范围规则不增加墙后射线过滤。超时/射程结束不爆炸，不触发折射、灼烧或递归。',
        'Recovery':'飞行中取得强化或升级爆破，旧弹保留旧快照；同一原始ShotId派生独立附弹ID。主弹、附弹、所有回执完成后才退休。恢复记录循环相位、下一枪侧别及通知序号。打空进度按PlayerGuid记录于Ground权威载体ground_shots能力，停火、载体休眠和同局恢复保留；死亡复活需有实际复活入口后验证；首次/升级11.01归零，新局清空。重复接纳/命中回执不再出弹或扣血。ReadState的attacks.primary_receipt和flight_settlement提供数值证据。新版协议40/Ground6/Linear10须开始新局。',
    }
    labels=[]
    for index,(name,text) in enumerate(notes.items()):
        label=PREFIX+'Instructions_'+name;note=actors.get(label)
        if note is None:
            note=require(sub.spawn_actor_from_class(unreal.Note,unreal.Vector(-2800+index*150,54200,1200)),'Add '+label)
            note.set_actor_label(label);actors[label]=note
        require(isinstance(note,unreal.Note),'Existing note class')
        note.modify();note.set_editor_property('text',text);labels.append(label)
    require(unreal.EditorLoadingAndSavingUtils.save_map(world,MAP),'Save review map')
    # Reuse the existing narrowly scoped certification workflow, without rerunning its scene creation.
    tree=ast.parse((ROOT/'Scripts/GroundMech/Rogue/prepare_initial_bullet_scene.py').read_text(encoding='utf8'))
    function=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='prepare_scene_dependencies')
    exec(compile(ast.Module(body=[function],type_ignores=[]),'existing_scene_dependencies','exec'),globals())
    dependencies=prepare_scene_dependencies(world,actors)
    require(level.load_level(MAP),'Reload saved review map')
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    saved={a.get_actor_label():a for a in sub.get_all_level_actors()}
    require(before=={k:saved[k].get_actor_transform().to_tuple() for k in before},'Preserve every existing transform')
    entities=[]
    for name,text in notes.items():
        label=PREFIX+'Instructions_'+name;note=require(saved.get(label),'Reload '+label)
        require(str(note.get_editor_property('text'))==text,'Saved instruction text '+label)
        entities.append({'label':label,'text':text,'position':note.get_actor_location().to_tuple()})
    manifests=[]
    for population in (200,600):
        path=ROOT/f'Config/Hybrid/RuntimeManifest-{population}.json'
        result=json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(population,str(path)))
        require(result.get('success'),'Export manifest '+str(result))
        readback=json.loads(unreal.GuLiHybridAuthoringLibrary.validate_runtime_manifest(str(path)))
        require(readback.get('success'),'Validate manifest '+str(readback))
        manifests.append({'population':population,'export':result,'readback':readback})
    resource=unreal.GuLiResourceAuthoringLibrary.validate_current_bake()
    nav=unreal.GuLiNavigationBakeLibrary.validate_world_navigation(world)
    require(resource.success and nav.success,'Resource/navigation readiness')
    dependencies.update(resource_readback=str(resource.message),navigation_readback=str(nav.message))
    return {'success':True,'map':MAP,'entry':saved['GroundRogue_Entry'].get_actor_location().to_tuple(),'scene_saved':True,
            'entities':entities,'existing_transforms_preserved':len(before),'manifests':manifests,'dependencies':dependencies,
            'PIE':False,'assistant_visual_test':False,'player_acceptance':'pending','runtime_evidence_report':str(OUT/'runtime_evidence_summary.json')}

try:report=main()
except Exception:report={'success':False,'error':traceback.format_exc()}
OUT.mkdir(parents=True,exist_ok=True)
(OUT/'scene_delivery.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
unreal.MCPythonHelper.submit_result(json.dumps({'success':report['success'],'error':report.get('error'),'report':str(OUT/'scene_delivery.json')},ensure_ascii=False))
