from pathlib import Path
p=Path('Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.h'); s=p.read_text(encoding='utf-8-sig').replace('void PublishMoveEndpointChanges();','''void PublishMoveEndpointChanges();
\tvoid TickSteeringValidation();
\tbool ResolveValidatedSteeringTargets(FGuLiSoldierId Id, uint32 PathRevision, const FVector& Lane, const FVector& Slot, FVector& OutLane, FVector& OutSlot);'''); p.write_text(s,encoding='utf-8')
p=Path('Source/GuLiStrike/Commander/Mass/Navigation/GuLiNavigationWorkBudget.h'); s=p.read_text(encoding='utf-8-sig').replace('\tstruct FScope','''\tstruct FQueryScope
\t{
\t\tFGuLiNavigationWorkBudget& Budget; double Started=FPlatformTime::Seconds();
\t\texplicit FQueryScope(FGuLiNavigationWorkBudget& In) : Budget(In) {}
\t\t~FQueryScope() { Budget.RecordQuery(Started); }
\t};
\tstruct FScope'''); p.write_text(s,encoding='utf-8')
p=Path('Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp'); s=p.read_text(encoding='utf-8-sig')
s=s.replace('#include "Commander/Mass/Navigation/GuLiNavigationWorkBudget.h"', '#include "Commander/Mass/Navigation/GuLiNavigationWorkBudget.h"\n#include "Containers/Queue.h"')
a=s.index('\n\tstruct FMoveDestinationReservation')
s=s[:a]+'''
\tstruct FSteeringValidation
\t{
\t\tFVector RequestedLane=FVector::ZeroVector, RequestedSlot=FVector::ZeroVector;
\t\tFNavLocation Origin, Lane, Slot;
\t\tuint32 Order=0, Path=0, Nav=0, Obstacles=0;
\t\tuint8 Stage=0;
\t\tbool bLaneValid=false, bSlotValid=false;
\t};
'''+s[a:]
s=s.replace('FGuLiNavigationWorkBudget PlanningBudget;', '''FGuLiNavigationWorkBudget PlanningBudget;
\tTMap<uint32,GuLiCommanderMassPrivate::FSteeringValidation> Steering;
\tTQueue<uint32> SteeringQueue;
\tTSet<uint32> SteeringQueued;''')
# Locate the shared budget setup and run target checks before main planning every third frame.
a=s.index('\n\t\twhile (AuthorityState->ReservationBootstrapCursor') if '\n\t\twhile (AuthorityState->ReservationBootstrapCursor' in s else -1
# Insert at TickMovePlanning call, avoid declaration.
needle='\tTickMovePlanning(RemainingProjectionBudget, RemainingPathBudget);'
if needle not in s:
 import re
 m=re.search(r'\n\tTickMovePlanning\([^;]+;',s)
 assert m; needle=m.group(0).lstrip('\n')
s=s.replace(needle,'\tif (GFrameCounter % 3 == 0) TickSteeringValidation();\n'+needle+'\n\tif (GFrameCounter % 3 != 0) TickSteeringValidation();',1)
a=s.index('\nvoid UGuLiBattleAuthoritySubsystem::TickLocalFlowFields()')
s=s[:a]+'''
bool UGuLiBattleAuthoritySubsystem::ResolveValidatedSteeringTargets(FGuLiSoldierId Id,uint32 PathRevision,
\tconst FVector& Lane,const FVector& Slot,FVector& OutLane,FVector& OutSlot)
{
\tusing namespace GuLiCommanderMassPrivate;
\tconst auto* Index=AuthorityState->SoldierIndexById.Find(Id.Value); if (!Index) return false;
\tconst auto& Soldier=AuthorityState->Soldiers[*Index];
\tconst auto* Registry=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
\tconst uint32 ObstacleVersion=Registry ? Registry->GetRevision() : 0;
\tauto& Cache=AuthorityState->Steering.FindOrAdd(Id.Value);
\tconst float Tolerance=FMath::Max(10.f,Soldier.AvoidanceRadiusCentimeters*.5f);
\tif (Cache.Order!=Soldier.ActiveOrderId || Cache.Path!=PathRevision || Cache.Nav!=AuthorityState->NavigationGeneration
\t\t|| Cache.Obstacles!=ObstacleVersion || Cache.Origin.NodeRef!=Soldier.LastValidNavLocation.NodeRef
\t\t|| !Cache.RequestedLane.Equals(Lane,Tolerance) || !Cache.RequestedSlot.Equals(Slot,Tolerance))
\t{
\t\tCache=FSteeringValidation{}; Cache.Order=Soldier.ActiveOrderId; Cache.Path=PathRevision;
\t\tCache.Nav=AuthorityState->NavigationGeneration; Cache.Obstacles=ObstacleVersion;
\t\tCache.Origin=Soldier.LastValidNavLocation; Cache.RequestedLane=Lane; Cache.RequestedSlot=Slot;
\t}
\tif (Cache.Stage<4 && !AuthorityState->SteeringQueued.Contains(Id.Value))
\t{ AuthorityState->SteeringQueued.Add(Id.Value); AuthorityState->SteeringQueue.Enqueue(Id.Value); }
\tif (Cache.Stage<4 || !Cache.bLaneValid) return false;
\tOutLane=Cache.Lane.Location;
\tOutSlot=Cache.bSlotValid ? Cache.Slot.Location : Soldier.Location;
\treturn true;
}
void UGuLiBattleAuthoritySubsystem::TickSteeringValidation()
{
\tFGuLiNavigationWorkBudget::FScope Scope(AuthorityState->PlanningBudget);
\tauto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
\tauto* Data=Nav ? GetCommanderNavigationData(*Nav) : nullptr;
\tauto* Registry=GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
\tif (!Data || !Registry) return;
\tconst auto Snapshot=Registry->GetSnapshot();
\tuint32 Id=0;
\tfor (int32 Visited=0; Visited<128 && AuthorityState->PlanningBudget.CanWork() && AuthorityState->PlanningBudget.Projections>0
\t\t&& AuthorityState->SteeringQueue.Dequeue(Id); ++Visited)
\t{
\t\tAuthorityState->SteeringQueued.Remove(Id);
\t\tauto* Cache=AuthorityState->Steering.Find(Id); const auto* Index=AuthorityState->SoldierIndexById.Find(Id);
\t\tif (!Cache || !Index) { AuthorityState->Steering.Remove(Id); continue; }
\t\tconst auto& Soldier=AuthorityState->Soldiers[*Index];
\t\tif (!Soldier.CanAct() || Cache->Order!=Soldier.ActiveOrderId || Cache->Nav!=AuthorityState->NavigationGeneration
\t\t\t|| Cache->Obstacles!=Snapshot->Revision || Cache->Origin.NodeRef!=Soldier.LastValidNavLocation.NodeRef)
\t\t{ AuthorityState->Steering.Remove(Id); continue; }
\t\tif (Cache->Stage>=4) continue;
\t\tif (!AuthorityState->PlanningBudget.TakeProjection()) break;
\t\tFGuLiNavigationWorkBudget::FQueryScope Query(AuthorityState->PlanningBudget);
\t\tconst bool bLane=Cache->Stage<2;
\t\tauto& Projected=bLane ? Cache->Lane : Cache->Slot;
\t\tconst auto& Requested=bLane ? Cache->RequestedLane : Cache->RequestedSlot;
\t\tbool& Valid=bLane ? Cache->bLaneValid : Cache->bSlotValid;
\t\tif ((Cache->Stage&1)==0)
\t\t{
\t\t\tValid=ProjectPointToCommanderNavigation(*Nav,*Data,Requested,FVector(10,10,500),Projected)
\t\t\t\t&& FVector::DistSquared2D(Requested,Projected.Location)<=100.
\t\t\t\t&& Snapshot->IsSegmentClear(Projected.Location,Projected.Location,Soldier.AvoidanceRadiusCentimeters);
\t\t}
\t\telse if (Valid)
\t\t\tValid=HasDirectSurfaceConnection(*Data,Cache->Origin,Projected.Location)
\t\t\t\t&& Snapshot->IsSegmentClear(Cache->Origin.Location,Projected.Location,Soldier.AvoidanceRadiusCentimeters,true);
\t\t++Cache->Stage;
\t\tif (Cache->Stage<4) { AuthorityState->SteeringQueued.Add(Id); AuthorityState->SteeringQueue.Enqueue(Id); }
\t}
}
'''+s[a:]
s=s.replace('const FVector LaneWaypoint = GuLiCommanderNavigationPolicy::CalculatePathLaneWaypoint(', 'FVector LaneWaypoint = GuLiCommanderNavigationPolicy::CalculatePathLaneWaypoint(',1)
a=s.index('\t\t\t\tFVector TravelDirection = (LaneWaypoint - Soldier.Location).GetSafeNormal();'); b=s.index('\t\t\t\tFVector SlotCorrectionTarget',a)
s=s[:a]+s[b:]
a=s.index('\t\t\t\tconst FVector SlotVelocity =')
s=s[:a]+'''\t\t\t\tFVector ValidLane,ValidSlot;
\t\t\t\tconst bool bValidated=ResolveValidatedSteeringTargets(Soldier.SoldierId,Formation.PathRevision,LaneWaypoint,SlotCorrectionTarget,ValidLane,ValidSlot);
\t\t\t\tLaneWaypoint=bValidated ? ValidLane : Formation.PathPoints[*MemberPathPointIndex];
\t\t\t\tSlotCorrectionTarget=bValidated ? ValidSlot : Soldier.Location;
\t\t\t\tconst FVector TravelDirection=(LaneWaypoint-Soldier.Location).GetSafeNormal();
'''+s[a:]
# Check projected destinations against environmental occupation under the existing position work unit.
needle='if (IsMoveCandidateBlockedByHardReservation(Job, Projected.Location))'
s=s.replace(needle,'''if (!World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->GetSnapshot()->IsSegmentClear(Projected.Location,Projected.Location,Job.MaximumMemberRadiusCentimeters)
\t\t\t\t\t\t|| IsMoveCandidateBlockedByHardReservation(Job, Projected.Location))''',1)
# Endpoints cannot enter or cut through registered obstacles even in recovery.
needle='const bool bSurfaceMoveAccepted =\n\t\t\t\t\tGuLiCommanderNavigationPolicy::IsSurfaceMoveResultAcceptable('
s=s.replace(needle,'''const bool bSurfaceMoveAccepted = World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->GetSnapshot()->IsSegmentClear(
\t\t\t\t\tSoldier.Location,SurfaceLocation.Location,Soldier.AvoidanceRadiusCentimeters,true)
\t\t\t\t\t&& GuLiCommanderNavigationPolicy::IsSurfaceMoveResultAcceptable(''',1)
s=s.replace('const bool bFinalSegmentValid = bFinalSurfaceMoveAccepted', 'const bool bFinalSegmentValid = bFinalSurfaceMoveAccepted\n\t\t\t\t\t&& World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>()->GetSnapshot()->IsSegmentClear(Soldier.Location,SurfaceDestination.Location,Soldier.AvoidanceRadiusCentimeters,true)',1)
# Clear all validation queues on population replacement.
s=s.replace('AuthorityState->MoveReservations = {};', 'AuthorityState->Steering.Reset(); AuthorityState->SteeringQueue.Empty(); AuthorityState->SteeringQueued.Reset();\n\tAuthorityState->MoveReservations = {};',1)
p.write_text(s,encoding='utf-8')
