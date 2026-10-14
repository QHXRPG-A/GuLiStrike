"""Read production asset references before removing retired native CS classes.

Run with the source editor Python commandlet. This never saves or compiles assets.
"""
import json
import re
from pathlib import Path
import unreal

RETIRED = (
    "GuLiCommanderWorldReplicationComponent",
    "GuLiArmySkillReplicationActor",
    "GuLiWingmanQASubsystem",
    "GuLiListenSmokeDiagnosticsSubsystem",
    "GuLiWingmanRelayComponent",
    "GuLiConstructionReviewLibrary",
)
RETIRED_FUNCTIONS = (
    "SetTerritoryOwner", "SetTerritoryOwnerByBoardCoordinate", "GetWingmanRelayComponent",
    "EnqueueCargo", "EnqueueRaw", "SpawnMiningVehicle", "MineOneRaw",
    "FundReview", "SpawnSample", "AdvanceSample", "JoinReviewClient",
    "BuildMissileFixture", "FireMissileFixture", "MissileMetrics", "ExecuteArmySkillCommand",
    "BuildFixture", "StartPIE", "StartMixedFlightPIE", "StartPerformancePIE", "SetPerformanceBandwidth",
    "PreparePerformancePopulation", "PerformancePopulationSnapshot", "OrderPerformancePopulation",
    "MulticastFlightBatch", "MulticastReliableStates", "MulticastActiveSnapshot", "MulticastWingmanFeedback", "MulticastRogueUpgrade",
    "ServerRequestSelection", "ServerReplaceSelection", "ServerRecoverCommanderCommands",
    "ClientRecoverCommanderCommands", "ServerIssueMove", "ServerRequestBootstrap",
    "ServerAcknowledgeBootstrap", "ServerAcknowledgeFacts", "ServerRequestStateResync",
    "ClientReceiveStateBatch", "ServerRequestSelectionFast", "ServerIssueMoveFast",
    "ClientBootstrapStarted", "ClientReceiveCommandAck", "ClientReceiveCommandAckFast",
    "ClientReceiveEncodedPoseBlock", "ServerAcknowledgePoseBlocks", "ServerPanelSelection",
    "ServerOrderedSelection", "ServerOrderedTask", "ServerControlGroup", "ClientOrderedSelection",
    "ClientTaskReceipt", "ClientTaskSnapshot", "MulticastReceiveWingmanBootstrap", "MulticastRevokeWingmanGroup",
)
retired_symbol = re.compile(r"\b(?:" + "|".join(map(re.escape, RETIRED + RETIRED_FUNCTIONS)) + r")\b")
registry = unreal.AssetRegistryHelpers.get_asset_registry()
report = {"schema": "guli-retired-cs-assets/v7", "retired_functions": RETIRED_FUNCTIONS, "assets": [], "references": [], "errors": []}
world = unreal.EditorLoadingAndSavingUtils.load_map("/Game/Maps/LVL_CommanderMassPrototype")
if not world:
    raise RuntimeError("Cannot inspect the original prototype map")
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if actor.get_class().get_name() in RETIRED:
        report["references"].append({"asset": actor.get_path_name(), "kind": "placed_actor", "class": actor.get_class().get_name()})
report["map"] = world.get_path_name()
for folder in ("/Game/GuLiStrike", "/Game/Commander", "/Game/Maps", "/Game/UI"):
    for data in registry.get_assets_by_path(folder, recursive=True):
        kind = str(data.asset_class_path.asset_name)
        if "Blueprint" not in kind or "GeneratedClass" in kind:
            continue
        path = str(data.package_name) + "." + str(data.asset_name)
        try:
            bp = data.get_asset()
            if not bp:
                raise RuntimeError("Asset failed to load")
            report["assets"].append(path)
            parent = data.get_tag_value("ParentClass") or data.get_tag_value("NativeParentClass")
            if not parent:
                raise RuntimeError("Asset Registry has no parent-class metadata")
            if any(name in str(parent) for name in RETIRED):
                report["references"].append({"asset": path, "kind": "parent", "value": str(parent)})
        except Exception as error:
            report["errors"].append({"asset": path, "error": str(error)})

assets = set(report["assets"])
for graph in unreal.ObjectIterator(unreal.EdGraph):
    owner = graph.get_outer()
    while owner and not isinstance(owner, unreal.Blueprint):
        owner = owner.get_outer()
    if not owner or owner.get_path_name() not in assets:
        continue
    try:
        body = unreal.MCPythonHelper.get_blueprint_graph_info(owner, graph.get_name())
        if retired_symbol.search(body):
            report["references"].append({"asset": owner.get_path_name(), "kind": "graph", "graph": graph.get_name(), "detail": json.loads(body)})
    except Exception as error:
        report["errors"].append({"asset": owner.get_path_name(), "graph": graph.get_name(), "error": str(error)})

for item in unreal.ObjectIterator(unreal.ActorComponent):
    name = item.get_class().get_name()
    if name not in RETIRED:
        continue
    owner = item.get_outer()
    # This exact default-subobject name is created by the native GameMode.
    # Additional Blueprint components have independent names and are reported.
    native_default = (name == "GuLiCommanderWorldReplicationComponent"
                      and item.get_name() == "SoldierWorldReplication"
                      and isinstance(owner, unreal.GuLiCommanderGameMode))
    native_default |= (name == "GuLiWingmanRelayComponent" and item.get_name() == "WingmanRelay"
                       and isinstance(owner, unreal.GuLiBattlePlayerController))
    if not native_default and not item.get_path_name().startswith("/Script/"):
        report["references"].append({"asset": owner.get_path_name() if owner else "", "kind": "component", "class": name})

report["passed"] = not report["errors"] and not report["references"]
out = Path(unreal.Paths.project_dir()) / "TestResults/HybridFullAcceptance/Implementation/retired-cs-asset-references-v7.json"
out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
unreal.log("Retired CS references: " + json.dumps({"assets": len(assets), "references": len(report["references"]), "errors": len(report["errors"])}))
