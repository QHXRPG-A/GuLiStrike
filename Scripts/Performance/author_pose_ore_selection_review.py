"""Save a review entry using the existing production map and launch configuration."""
import json
from pathlib import Path
import unreal

assert not unreal.EditorLevelLibrary.get_pie_worlds(True), "Authoring requires an idle editor"
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name() == "/Game/Maps/LVL_CommanderMassPrototype.LVL_CommanderMassPrototype"
subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors = subsystem.get_all_level_actors()

def one(label):
    matches = [actor for actor in all_actors if actor.get_actor_label() == label]
    assert len(matches) == 1, (label, len(matches))
    return matches[0]

launch = one("HybridNetworkReview")
camera = one("PerfSnapshotNetwork_Stress_View")
start = one("CommanderPlayerStart_Red")
anchor = one("PerfHybridCPU_Entry")
assert launch.get_class().get_name() == "GuLiHybridNetworkReviewActor"
assert launch.get_editor_property("units") == 600
assert launch.get_editor_property("additional_flights") == 500
manifest = launch.get_editor_property("manifest").get_editor_property("file_path")
assert manifest.replace("\\", "/").endswith("/Config/Hybrid/RuntimeManifest-600.json")
protocol = json.loads(Path(manifest).read_bytes())["protocol_version"]
label = "PerfPoseOreSelection_Entry"
tag = unreal.Name("PoseOreSelectionReview20261105")
matches = [actor for actor in all_actors if actor.get_actor_label() == label]
assert len(matches) <= 1
position = anchor.get_actor_location() + unreal.Vector(200, 0, 0)
instructions = f"""姿态同步、矿石与选择卡顿复测（2026-11-05）
在 /Game/Maps/LVL_CommanderMassPrototype 使用本地联机入口 HybridNetworkReview，双方使用协议 {protocol} 的清单。
入口配置为 600 单位和 500 额外弹丸。玩家起点为 CommanderPlayerStart_Red，观察相机为 PerfSnapshotNetwork_Stress_View。
1. 让编队静止，再移动、停止、改令和受击。在对端观察姿态、转向和动画是否及时更新。
2. 对兵种、建筑和工程车进行单选、框选、同类选择和加减选，再下达移动与停止指令，核对成员及反馈。
3. 在采矿期间占领或反占领据点，核对归属、供电和奖励。采空后只应移除对应矿石的阻挡。
4. 双方使用同一新版。开局第 10 秒，在单对端、本地约 300 名士兵时按 Alt+/ 采 5 秒。保留一队待机，对比报告“姿态发送队列”六个采样点的姿态、回执、组件待发数和年龄，以及整个窗口的替换次数。
5. 对照 17:24 基线的 6,146 条静止 Mass 实发、7,200 次姿态替换和 6,574 次回执替换。新版报告列出首次或重置、真实字段变化、动画纠偏、未确认补发和周期校正，编码前的完全重复与自然动画推进另计。
6. 另一次操作中让远端观察原地待机、停后马上再走、原地转炮管、死亡和传送。已确认的稳定士兵约每 0.5 秒校正一次。在受控丢包条件下检查首条停止姿态或首条回执丢失后的恢复，补发从上一次真正发出起等 200 毫秒，再在下一发布机会发送。
7. 带宽验收使用 18:38 的 160.43 KB/s 为基线，目标为同负载姿态通道实发量 ≤100 KB/s。查看工程车和采矿车各自的姿态、回执字节，以及总待发年龄。单独核对车辆停止、改矿点、施工续期、通行阶段和重连后的状态。
开发记录在 Progress/DevelopmentDocumentation/20261105-姿态同步与交互卡顿优化.md。"""

with unreal.ScopedEditorTransaction("Pose ore selection performance review entry"):
    if matches:
        note = matches[0]
        assert note.actor_has_tag(tag), "Refusing to replace an unrelated actor"
    else:
        note = subsystem.spawn_actor_from_class(unreal.Note, position)
        assert note
        note.set_actor_label(label)
        note.set_editor_property("tags", [tag])
    note.modify()
    note.set_editor_property("is_editor_only_actor", True)
    note.set_folder_path("Performance/PoseOreSelection20261105")
    note.set_actor_location(position, False, True)
    note.set_editor_property("text", instructions)
    launch.modify()
    launch.prepare_launch_arguments()
    assert not launch.get_editor_property("preparation_error"), launch.get_editor_property("preparation_error")
    assert "-GuLiAcceptancePopulation=600" in launch.get_editor_property("launch_arguments")
    tags = list(launch.get_editor_property("tags"))
    if tag not in tags:
        launch.set_editor_property("tags", tags + [tag])

assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level(), "Map save failed"
assert note.get_editor_property("text") == instructions and note.actor_has_tag(tag)
assert note.get_editor_property("is_editor_only_actor") and launch.actor_has_tag(tag)
assert all(abs(a-b) < .01 for a, b in zip(note.get_actor_location().to_tuple(), position.to_tuple()))
entities = [{"label": actor.get_actor_label(), "path": actor.get_path_name(),
             "class": actor.get_class().get_name(), "position_cm": list(actor.get_actor_location().to_tuple()),
             "rotation": str(actor.get_actor_rotation())} for actor in (note, launch, camera, start)]
unreal.MCPythonHelper.submit_result(json.dumps({
    "success": True, "saved": True, "map": world.get_path_name(), "entities": entities,
    "units": launch.get_editor_property("units"), "additional_flights": launch.get_editor_property("additional_flights"),
    "manifest": manifest, "protocol": protocol, "entry_text": note.get_editor_property("text"),
    "launch_arguments_validated": True,
    "validation_scope": "Saved scene and launch configuration. Player behavior and performance require runtime verification.",
}, ensure_ascii=False))
