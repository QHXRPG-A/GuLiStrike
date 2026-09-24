from pathlib import Path
p=Path('Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp');s=p.read_text(encoding='utf-8-sig')
s=s.replace('int32 PlanningCursor = 0;', 'uint32 PlanningCursor = 0;')
s=s.replace('if (Soldier.LastValidNavLocation.NodeRef != Member.CommandStart.NodeRef)', 'if (!Soldier.LastValidNavLocation.NodeRef || Soldier.LastValidNavLocation.NodeRef != Member.CommandStart.NodeRef)',1)
s=s.replace('Job.Progress.Committed.Add(SoldierId);', 'if (!Job.FinishedIds.Contains(SoldierId.Value)) Job.Progress.Committed.Add(SoldierId);',1)
s=s.replace('else Job.Progress.Failed.Add(Member.SoldierId);', 'else if (!Job.FinishedIds.Contains(Member.SoldierId.Value)) Job.Progress.Failed.Add(Member.SoldierId);',1)
s=s.replace('Job.FinishedIds.Add(Member.SoldierId.Value);\n\t\t\t}', 'Job.FinishedIds.Add(Member.SoldierId.Value);\n\t\t\t\tJob.FrozenTaskGenerations.Remove(Member.SoldierId.Value);\n\t\t\t}',1)
s=s.replace('Job.Debug.FailedMembers = Job.FrozenTaskGenerations.Num() - Job.TotalAccepted;', 'Job.Debug.FailedMembers = Job.FinishedIds.Num() - Job.TotalAccepted;',1)
needle='if (!Job || Job->Stage == EMovePlanningStage::Completed) continue;'
s=s.replace(needle,needle+'''
\t\t\tif (Job->FrozenTaskGenerations.Remove(Id.Value) && !Job->FinishedIds.Contains(Id.Value))
\t\t\t{ Job->Progress.Failed.Add(Id); Job->FinishedIds.Add(Id.Value); }''',1)
a=s.index('\n}\nvoid UGuLiBattleAuthoritySubsystem::StopTaskSoldiers')
s=s[:a]+'''
\tfor (auto& Job : AuthorityState->MovePlanningJobs)
\t\tif (Job && Job->Stage!=EMovePlanningStage::Completed && Job->FrozenTaskGenerations.IsEmpty())
\t\t\tCompleteMovePlanningJobWithSystemFailure(*Job,EGuLiCommandAckResult::Cancelled);
'''+s[a:]
# Bounded concurrent plans use the same live ledger and serialize at commit. A partial replacement must not wait behind unaffected cohorts.
a=s.index('\t// Destination reservations are built in an isolated scratch table.'); b=s.index('\n\tUWorld* World',a)
s=s[:a]+'''\tint32 TeamJobs=0;
\tfor (const auto& Existing : AuthorityState->MovePlanningJobs)
\t\tif (Existing && Existing->Stage!=EMovePlanningStage::Completed && Existing->Team==PlayerState.GetTeam()) ++TeamJobs;
\tif (TeamJobs>=8) { OutImmediateAck.Result=EGuLiCommandAckResult::RateLimited; return false; }
'''+s[b:]
s=s.replace('for (const auto& Existing : AuthorityState->MovePlanningJobs)\n\t\tif (Existing && Existing->Team == Team && Existing->Stage != EMovePlanningStage::Completed) return {};', '''int32 TeamJobs=0;
\tfor (const auto& Existing : AuthorityState->MovePlanningJobs)
\t\tif (Existing && Existing->Team==Team && Existing->Stage!=EMovePlanningStage::Completed) ++TeamJobs;
\tif (TeamJobs>=4) return {};''',1)
s=s.replace('return !Job || (!Job->bAutomatic && !Job->PlayerState.IsValid());', 'return !Job || (!Job->bAutomatic && !Job->PlayerState.IsValid())\n\t\t\t\t|| (Job->bAutomatic && Job->Stage==EMovePlanningStage::Completed && FPlatformTime::Seconds()-Job->PlanningStartedAt>35.);',1)
# Move pure formation preparation to the planning budget. The commit only installs prepared data.
a=s.index('\t\t\tAssignFormationSlots(',s.index('void UGuLiBattleAuthoritySubsystem::CommitReadyMovePlans'))
b=s.index('\n\t\t\tAuthorityState->OrderFormations.Add',a)
s=s[:a]+s[b:]
s=s.replace('Job.PreparedFormations.Add(MoveTemp(Formation));', 'AssignFormationSlots(Formation,AuthorityState->Soldiers,AuthorityState->SoldierIndexById,MemberSpacingCentimeters,0,Formation.TransitColumnCount);\n\t\t\t\tJob.PreparedFormations.Add(MoveTemp(Formation));',1)
a=s.index('\t\t\t\tconst FMoveMemberPlan* Member = Job.Members.FindByPredicate(',s.index('void UGuLiBattleAuthoritySubsystem::CommitReadyMovePlans'));b=s.index('\t\t\t\tconst int32* SoldierIndex',a)
s=s[:a]+'\t\t\t\tconst int32* M=Job.MemberIndexById.Find(SoldierId.Value);\n\t\t\t\tconst FMoveMemberPlan* Member=M ? &Job.Members[*M] : nullptr;\n'+s[b:]
s=s.replace('Soldier.LastValidNavLocation = ValidatedSlot.NavigationLocation;', 'Soldier.LastValidNavLocation = ValidatedSlot.NavigationLocation;\n\t\t\t\tSoldier.FinalDestinationNavigationGeneration=AuthorityState->NavigationGeneration;',1)
s=s.replace('const bool bSurfaceMoveAccepted = World->GetSubsystem', 'const bool bSurfaceMoveAccepted = bSurfaceMoveSucceeded && World->GetSubsystem',1)
# Global obstacle constraint does not attenuate speed just because an obstacle is alongside a unit walking away.
s=s.replace('Pressure=FMath::Clamp(1.f-static_cast<float>(Gap)/Reach,0.f,1.f);', 'Pressure=FMath::Clamp(1.f-static_cast<float>(Gap)/Reach,0.f,1.f);',1)
# Prevent reuse of a validation across a different member waypoint on the same path.
s=s.replace('Soldier.SoldierId,Formation.PathRevision,LaneWaypoint', 'Soldier.SoldierId,HashCombine(Formation.PathRevision,GetTypeHash(*MemberPathPointIndex)),LaneWaypoint',1)
# Restore radius of a failed member rather than the old hardcoded 150 cm.
s=s.replace('Restored.Location = Member.OldReservation;\n\t\t\t\t\tAddHardReservationToMoveJob', 'Restored.Location = Member.OldReservation;\n\t\t\t\t\tRestored.RadiusCentimeters=Soldier.AvoidanceRadiusCentimeters;\n\t\t\t\t\tAddHardReservationToMoveJob',1)
# Death/transport are endpoint events immediately, not at the next movement iteration.
needle='Soldier.bPhased = bPhased; Soldier.bExternalActionsLocked = bLocked;'
# Refresh after the order/teleport assignments at the existing revision mutation below.
a=s.index(needle);b=s.index('++Soldier.StateRevision;',a)
s=s[:b]+s[b:].replace('++Soldier.StateRevision;', '++Soldier.StateRevision;\n\t\tRefreshSoldierNavigationState(Soldier.SoldierId);',1)
# Idle units only need a polygon refresh following a nav generation. Clear a stale NodeRef on teleport is handled by that work too.
s=s.replace('Soldier.LastValidNavLocation = FNavLocation(Soldier.Location);', 'Soldier.LastValidNavLocation = FNavLocation(Soldier.Location);\n\t\t\tSoldier.FinalDestinationNavigationGeneration=0;',1)
# Stop width shrink when a soft time limit interrupts the probe group.
needle='FitsByColumnCount[ColumnCount - 1] = 1u;\n\t\t\t\tbreak;\n\t\t\t}'
s=s.replace(needle,needle+'\n\t\t\tif (!Budget.CanWork()) return PreviousColumns;',1)
p.write_text(s,encoding='utf-8')

p=Path('Source/GuLiStrike/Commander/Orders/GuLiUnitTaskSubsystem.cpp');s=p.read_text(encoding='utf-8-sig').replace('\tfor (const auto& Batch : Planning) if (Batch.Owner.IsValid()) Busy.Add(Batch.Owner->GetTeam());','\t// Limit admission per tick; existing unrelated batches keep their progress under the authority budget.');p.write_text(s,encoding='utf-8')
p=Path('Source/GuLiStrike/Gameplay/Navigation/GuLiDynamicObstacleRegistry.cpp');s=p.read_text(encoding='utf-8-sig').replace('#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"', '#include "Gameplay/Navigation/GuLiDynamicObstacleRegistry.h"\n#include "Engine/World.h"',1).replace('\n#include "Engine/World.h"\n\nDEFINE_LOG_CATEGORY_STATIC','\nDEFINE_LOG_CATEGORY_STATIC',1).replace('ObstaclesChanged.Clear();','Snapshot.Reset();\n\tObstaclesChanged.Clear();',1);p.write_text(s,encoding='utf-8')
# Render lifetime/material and persistent same-order updates.
p=Path('Source/GuLiStrike/Commander/Presentation/GuLiCommanderRouteLineComponent.h');s=p.read_text(encoding='utf-8-sig').replace('virtual FPrimitiveSceneProxy* CreateSceneProxy() override;', 'virtual FPrimitiveSceneProxy* CreateSceneProxy() override;\n\tvirtual void GetUsedMaterials(TArray<UMaterialInterface*>& Out, bool bGetDebugMaterials=false) const override;').replace('bool bVisible=false;', 'uint32 Order=0; bool bVisible=false;');p.write_text(s,encoding='utf-8')
p=Path('Source/GuLiStrike/Commander/Presentation/GuLiCommanderRouteLineComponent.cpp');s=p.read_text(encoding='utf-8-sig')
s=s.replace('GEngine->DebugMeshMaterial ? GEngine->DebugMeshMaterial :', 'GEngine->DebugMeshMaterial ? GEngine->DebugMeshMaterial.Get() :',1)
s=s.replace('R->Vertices.PositionVertexBuffer.InitResource(RHICmdList); R->Vertices.StaticMeshVertexBuffer.InitResource(RHICmdList);\n\t\t\tR->Vertices.ColorVertexBuffer.InitResource(RHICmdList); R->Indices.InitResource(RHICmdList); R->Factory.InitResource(RHICmdList);', 'R->Indices.InitResource(RHICmdList); // InitFromDynamicVertex initializes vertex buffers and factory on this command list.')
s=s.replace('FBoxSphereBounds UGuLiCommanderRouteLineComponent::CalcBounds', 'void UGuLiCommanderRouteLineComponent::GetUsedMaterials(TArray<UMaterialInterface*>& Out,bool bGetDebugMaterials) const\n{ Out.Add(GEngine->DebugMeshMaterial ? GEngine->DebugMeshMaterial.Get() : UMaterial::GetDefaultMaterial(MD_Surface)); }\nFBoxSphereBounds UGuLiCommanderRouteLineComponent::CalcBounds',1)
s=s.replace('Hide(Id); Enqueue(Id);', '''const auto* Slot=Slots.Find(Id); const auto* Endpoint=NetSync.IsValid() ? NetSync->FindMoveEndpoint(Id) : nullptr;
\t\tif (Slot && (!Endpoint || Chunks[*Slot/LinesPerChunk].Lines[*Slot%LinesPerChunk].Order!=Endpoint->ActiveOrderId)) Hide(Id);
\t\tEnqueue(Id);''',1)
s=s.replace('L.Start=A; L.End=B; L.bVisible=true;', 'L.Start=A; L.End=B; L.Order=E.ActiveOrderId; L.bVisible=true;',1)
p.write_text(s,encoding='utf-8')
p=Path('Source/GuLiStrike/GuLiStrike.Build.cs');s=p.read_text(encoding='utf-8-sig').replace('\n\t\t\t\t"RenderCore",','');p.write_text(s,encoding='utf-8')
# Macro-aware grammar inspection, with UE's conditional statements kept together.
p=Path('Artifacts/MassNavigation20260923/inspect_cpp.py');s=p.read_text(encoding='utf-8').replace('DECLARE_[A-Z0-9_]+','DECLARE_[A-Za-z0-9_]+')
s=s.replace("src=re.sub(r'\\b[A-Z]", "src=re.sub(r'^#include UE_INLINE_GENERATED_CPP_BY_NAME.*$', '', src, flags=re.M)\n src=re.sub(r'^#(?:if|ifdef|ifndef|endif|else|elif).*$', '', src, flags=re.M)\n src=src.replace('enum : uint16','enum InspectionMask : uint16')\n src=re.sub(r'\\b[A-Z]")
p.write_text(s,encoding='utf-8')
