"""Save a player launch entry, production manifest and observation anchors; never launches gameplay."""
import json
from pathlib import Path
import unreal

def author():
    root=Path(unreal.Paths.project_dir()).resolve();data=root/'ArtSource/Navigation/Grid300'
    spec=json.loads((data/'review.json').read_text(encoding='utf-8'))
    grid=unreal.load_asset('/Game/GuLiStrike/Data/Navigation/DA_CommanderGroundGrid')
    assert grid and grid.get_editor_property('source_hash')==spec['terrain_hash']
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_path_name()==spec['map']+'.LVL_CommanderMassPrototype'
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem);assert not level.is_in_play_in_editor()
    service=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors=service.get_all_level_actors();tag=unreal.Name('Grid300Review20261022')
    def entry(label,cls,position):
        found=[a for a in actors if a.get_actor_label()==label];assert len(found)<=1,label
        if found:
            a=found[0];assert a.actor_has_tag(tag) and isinstance(a,cls),label
        else:
            a=service.spawn_actor_from_class(cls,unreal.Vector(*position));assert a
            a.set_actor_label(label);a.set_editor_property('tags',[tag])
        a.modify();a.set_editor_property('is_editor_only_actor',True)
        a.set_folder_path('Development/Grid300Review20261022');a.set_actor_location(unreal.Vector(*position),False,True)
        return a
    positions={r['id']:r['location'] for r in spec['targets']}
    for name,p in positions.items():entry('Grid300_'+name,unreal.TargetPoint,p)
    manifest=data/'PlayerReview/manifest.json';manifest.parent.mkdir(parents=True,exist_ok=True)
    report=json.loads(unreal.GuLiHybridAuthoringLibrary.export_runtime_manifest(spec['launch_population'],str(manifest)))
    assert report['success'],report
    launch=entry('Grid300_Launch200',unreal.GuLiHybridNetworkReviewActor,positions['Assembly'])
    defaults=unreal.get_default_object(unreal.GuLiHybridNetworkReviewActor)
    for key in ('transport','team','seat','run_mode'):
        launch.set_editor_property(key,defaults.get_editor_property(key))
    room,valid=unreal.GuidLibrary.parse_string_to_guid('31d23f7508b64ec310cc2bb563b4fb21');assert valid
    identity,valid=unreal.GuidLibrary.parse_string_to_guid('47f614b369c23157013c02280b22831a');assert valid
    launch.set_editor_property('room',room);launch.set_editor_property('development_identity',identity)
    launch.set_editor_property('units',spec['launch_population']);launch.set_editor_property('additional_flights',0)
    path=launch.get_editor_property('manifest');path.set_editor_property('file_path',str(manifest))
    launch.set_editor_property('manifest',path);launch.prepare_launch_arguments()
    assert not launch.get_editor_property('preparation_error'),launch.get_editor_property('preparation_error')
    assert '-GuLiManifest=' in launch.get_editor_property('launch_arguments')
    report['actor']=launch.get_path_name();report['gameplay_run']=False
    (data/'PlayerReview/entry.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
    note=entry('Grid300_Review',unreal.Note,positions['Assembly'])
    coordinates='\n'.join(f'{k}: ({p[0]:.0f}, {p[1]:.0f}, {p[2]:.2f}) cm' for k,p in positions.items())
    instructions='''3米整数格地面导航：玩家验证入口（默认停用）
本图保存了Grid300_Launch200启动入口、200名初始单位的真实玩法清单，以及测试点位。不会自动开始PIE或采集性能数据。
在World Outliner选择Grid300_Launch200，Details内的Launch Arguments为源码版编辑器启动参数；玩家手动启动该入口。
同场客户端使用同一Manifest和Room，分别设置不同Development Identity及Commander/Ground席位后生成各自启动参数。
功能观察建议选取先驱号8、重防号8，并用先驱号Q召唤8名扫荡者；UnitTypeId=6按现有建造入口准备。
性能前后比较需要相同200名初始单位及相同附加单位、指令顺序与可比地图条件，不能把选中的25名当成全场负载。
现有工程车、工厂、矿区、生产和联网验证入口保留。

1. 混合体型：从Assembly集结到CrossWest，观察大小占地互不重叠，最后位置冻结。
2. 新旧命令交叉：第一组CrossWest到CrossEast，再令第二组CrossNorth到CrossSouth；新命令优先，同批冲突稳定。
3. 窄路会车：在NarrowRoad附近用现有建造入口设置通道，分别保留1格、4格宽；大单位不能穿越不足自身占地的通道。
4. 斜角障碍：在Diagonal附近布置相接的建筑拐角，检查斜走不会切角、对向交换或交叉穿透。
5. 玩家穿行：直接驾驶地面机甲穿过停稳己方部队；只让实际挡路者以正常速度退出通道，让完停留新位置。
6. 受阻停止：堵住唯一通道后下令进入，连续物理阻挡3秒结束；之后等待至少10秒，单位保持位置。
7. 让路落位失败：不给周围留退出空间；单位等待或结束，不靠持续推挤制造空位。
8. 作业恢复：用部队阻挡矿车卸矿/建造车施工入口；解除阻挡后按5秒错峰重试，保留工单、资源和进度。
9. 出厂堵塞：堵住工厂出口并完成生产；无空位时延期，出现新单位前占用不释放，不重叠出生、不重复扣费。
10. Hybrid：另开客户端后，在移动、受阻、让路和出厂等待中验证断线恢复与所有权切换；旧所有者不得继续预占。

静止部队不应持续导航重算或返回理想站位。预算排队不算物理阻挡。玩家驾驶连续移动，飞行规则保留。
性能测量另行授权；比较相同数量、负载、地形条件的P95/P99、响应延迟、搜索、缓存、内存、网络CPU和流量。
此入口仅为实体交付，不代表已通过玩法、视觉或性能验收。
技术记录：Progress/DevelopmentDocumentation/20261022-3米整数格地面导航.md

点位：
'''+coordinates
    note.set_editor_property('text',instructions)
    assert note.get_editor_property('text')==instructions
    return {'review_entities':len(positions)+2,'default_inactive':True,'manifest':str(manifest),'launch_population':spec['launch_population']}

if __name__=='__main__':
    result=author();assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level()
    unreal.MCPythonHelper.submit_result(json.dumps({'success':True,**result},ensure_ascii=False))
