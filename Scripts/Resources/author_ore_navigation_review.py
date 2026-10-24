"""Save opt-in player ore-navigation review markers in the existing Commander map. Never starts PIE."""
import json
import math
import unreal

MAP = "/Game/Maps/LVL_CommanderMassPrototype"
TAG = "OreNavigationReview20261024"
FOLDER = "Development/OreNavigation20261024"
assert not unreal.EditorLevelLibrary.get_pie_worlds(True), "PIE must be stopped before scene authoring"
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
assert world.get_path_name() == MAP + ".LVL_CommanderMassPrototype", world.get_path_name()
actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
definition = unreal.load_asset("/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap")
nodes = {n.get_editor_property("node_id"): n for n in definition.get_editor_property("nodes")}
clusters = {c.get_editor_property("cluster_id"): c for c in definition.get_editor_property("clusters")}
existing = {a.get_actor_label(): a for a in actors.get_all_level_actors()}
created = []


def point(node):
    return node.get_editor_property("world_transform").translation


def cluster_nodes(cluster):
    first = cluster.get_editor_property("first_node_index") + 1
    return list(range(first, first + cluster.get_editor_property("node_count")))


def entity(cls, label, location, case, tags=(), editor_only=False):
    actor = existing.get(label)
    if actor:
        assert actor.actor_has_tag(TAG) and actor.get_class() == cls.static_class(), label
    else:
        actor = actors.spawn_actor_from_class(cls, location)
        assert actor, label
        actor.set_actor_label(label)
    actor.modify()
    actor.set_actor_location(location, False, True)
    actor.set_actor_enable_collision(False)
    actor.set_folder_path(FOLDER)
    actor.set_editor_property("is_editor_only_actor", editor_only)
    actor.set_editor_property("tags", [unreal.Name(t) for t in (TAG, "OreReviewCase:" + case, *tags)])
    created.append((actor, location, case, editor_only))
    return actor


cases = {"single": 235, "channel": 236, "boundary": 234, "batch": 233}
batch_clusters = [235, 236, 233, 234, 209, 232, 207, 206, 208]
scene = {}
with unreal.ScopedEditorTransaction("Prepare per-node ore navigation review"):
    for case, cluster_id in cases.items():
        cluster = clusters[cluster_id]
        center = cluster.get_editor_property("center")
        ids = cluster_nodes(cluster)
        target = next(i for i in ids if nodes[i].get_editor_property("initial_amount") == 3)
        neighbor = min((i for i in ids if i != target), key=lambda i: (point(nodes[i]) - point(nodes[target])).length_squared())
        channel_nodes = [i for i in ids if abs(point(nodes[i]).y - center.y) <= 250]
        tags = ("OreCluster:" + str(cluster_id), "OreNode:" + str(target), "OreNeighbor:" + str(neighbor))
        location = center + unreal.Vector(0, -1600, 2200)
        camera = entity(unreal.CameraActor, "OreNavReview_" + case + "_Camera", location, case, tags)
        camera.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location, center), True)
        camera.get_component_by_class(unreal.CameraComponent).set_editor_property("field_of_view", 75.0)
        camera.set_editor_property("auto_activate_for_player", unreal.AutoReceiveInput.DISABLED)
        entity(unreal.TargetPoint, "OreNavReview_" + case + "_Node", point(nodes[target]), case, tags)
        entity(unreal.TargetPoint, "OreNavReview_" + case + "_Neighbor", point(nodes[neighbor]), case,
               ("OreCluster:" + str(cluster_id), "OreNode:" + str(neighbor), "KeepBlocking"))
        for suffix, dx in (("RouteStart", -1000), ("RouteEnd", 1000)):
            entity(unreal.TargetPoint, "OreNavReview_" + case + "_" + suffix,
                   center + unreal.Vector(dx, 0, 0), case, ("OreCluster:" + str(cluster_id),))
        instructions = (
            "单颗矿石延迟合批导航恢复（默认不触发，未编译新代码）\n"
            f"场景 {case}；矿簇 {cluster_id}；完整矿石 NodeId={target}；保留邻矿 NodeId={neighbor}。\n"
            f"地图 {MAP}。使用现有 Hybrid 正式会话和真实采矿订单。\n"
            f"观察相机：gs.Resources.OreReview.Focus {case}；回原相机：gs.Resources.OreReview.Focus player。\n"
            f"向矿车下采矿命令后，可用 gs.Resources.OreReview.PrepareMiners {cluster_id} 跳过赴矿旅程；只移动本机拥有合法预占位的矿车。\n"
            "采集仍由真实 Hybrid 租约/提取/提交完成，入口不改剩余量、不伪造提交。\n"
            "诊断：gs.Resources.OreObstacleStats。关注 pending、oldest_ms、last_batch_nodes、last_batch_gt_ms、collision_alive、avoidance_alive、ore_index_publications。\n"
            "单颗：3→2→1 维持初始阻挡；归零先消失，随后本颗解除。邻矿继续挡路，其他单位继续移动。\n"
            f"连续通道：本簇中心带候选节点 {','.join(map(str, channel_nodes))}；RouteStart/RouteEnd 是玩家下令参考点。\n"
            "只有净空满足地形、邻矿和单位体型时才应通过；采空一个位置不承诺大型单位可通行。\n"
            "跨块：boundary 位于矿簇234，接近当前两套Recast的 y=-65000cm / 5000cm Tile边界；分别采掉两侧节点后观察局部恢复。\n"
            f"批量：已标记9簇 {','.join(map(str, batch_clusters))}，共234节点；玩家用多矿车同时真实采集，并读回队列/每批数量。\n"
            "大于64个同窗采空事件的上限与跨帧行为需编译后另行实测，本次未制造采空、未运行性能测试。\n"
            "重同步：有待处理事件时执行 gs.Resources.OreReview.Resync；队列清空、代次推进、从最新已提交剩余量重建；再退出重进验证重连。\n"
            "200ms、64颗/帧、0.5ms是默认参数；8颗物理操作时间片后检查软预算，最后统一发布可能超出软预算；没有耗时实测结论。\n"
            "先编译GuLiStrikeEditor并加载新代码，再由玩家开始游玩；场景保存/属性读回不等于玩家验收。"
        )
        note = entity(unreal.Note, "OreNavReview_" + case + "_Instructions", center + unreal.Vector(0, -1000, 300), case, tags, True)
        note.set_editor_property("text", instructions)
        scene[case] = {"cluster": cluster_id, "target": target, "neighbor": neighbor,
                       "position": [round(v, 2) for v in center.to_tuple()], "channel_nodes": channel_nodes}
    for cluster_id in batch_clusters:
        cluster = clusters[cluster_id]
        entity(unreal.TargetPoint, f"OreNavReview_BatchCluster_{cluster_id}", cluster.get_editor_property("center"),
               "batch", (f"OreCluster:{cluster_id}", "OreNodes:" + ",".join(map(str, cluster_nodes(cluster)))))

assert unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).save_current_level(), "Map save failed"
for actor, location, case, editor_only in created:
    assert actor.get_world().get_path_name() == world.get_path_name()
    assert actor.actor_has_tag(TAG) and actor.actor_has_tag("OreReviewCase:" + case)
    assert actor.get_editor_property("is_editor_only_actor") == editor_only
    assert not actor.get_actor_enable_collision()
    assert (actor.get_actor_location() - location).length() < 0.01
    if isinstance(actor, unreal.CameraActor):
        assert actor.get_editor_property("auto_activate_for_player") == unreal.AutoReceiveInput.DISABLED
    if isinstance(actor, unreal.Note):
        assert "gs.Resources.OreReview.Resync" in actor.get_editor_property("text")

navs = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.RecastNavMesh)
boundary_nodes = [point(nodes[i]) for i in cluster_nodes(clusters[234])]
tile_sides = sorted({math.floor(p.y / 5000.0) for p in boundary_nodes})
assert len(tile_sides) >= 2
assert all(n.get_editor_property("tile_size_uu") == 5000 for n in navs)
print(json.dumps({"saved": True, "world": world.get_path_name(), "entities": len(created),
                  "cameras": 4, "notes": 4, "target_points": len(created) - 8,
                  "cases": scene, "batch_clusters": batch_clusters, "batch_nodes": 234,
                  "boundary_y_tiles": tile_sides, "new_code_loaded": False, "pie_started": False}, ensure_ascii=False))
