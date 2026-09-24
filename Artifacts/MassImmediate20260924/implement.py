from pathlib import Path
p=Path('Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp')
s=p.read_text(encoding='utf-8-sig')
def replace(a,b,n=1):
 global s
 assert s.count(a)>=n, a[:100]
 s=s.replace(a,b,n)
replace('#include "Commander/Mass/Navigation/GuLiNavigationDependency.h"','#include "Commander/Mass/Navigation/GuLiNavigationDependency.h"\n#include "Commander/Mass/Navigation/GuLiSharedMoveRoutes.h"')
replace('\t\tbool bAutomaticAdvance = false;', '''		TSharedPtr<FGuLiSharedMoveIntent> MoveIntent;
		TSharedPtr<FGuLiSharedRouteGoal> RouteGoal;
		TSharedPtr<FGuLiSharedMoveRoute> SharedRoute;
		int32 RouteCursor = 0;
		uint32 DockLayoutRevision = 0;
		bool bHasDockTarget = false;
		GuLiMoveLatency::FContext MoveTrace;
		double CommandAcceptedAt = 0, DirectionAppliedAt = 0, FirstDisplacementAt = 0;
		bool bAutomaticAdvance = false;''')
replace('struct FCommandGroup { uint32 Batch = 0; double LastUsed = 0; };','struct FCommandGroup { uint32 Batch = 0; double LastUsed = 0; TWeakPtr<FGuLiSharedMoveIntent> Intent; };')
replace('\tuint64 NextPlanId = 1;', '''	FGuLiSharedMoveRoutePool SharedRoutes;
	TArray<TWeakPtr<FGuLiSharedMoveIntent>> MoveIntents;
	int32 SharedDiscoveryCursor[2] = {}, SharedDiscoveryRemaining[2] = {}, IntentCursor = 0;
	uint64 SharedDiscoveryFrame[2] = {MAX_uint64,MAX_uint64};
	uint64 NextPlanId = 1;''')
# Drop the reservation bootstrap and old recovery/steering/flow scheduling. No legacy planning entry remains.
start=s.index('\t{\n\t\tFGuLiNavigationWorkBudget::FScope Scope(AuthorityState->PlanningBudget);',s.index('void UGuLiBattleAuthoritySubsystem::Tick('))
end=s.index('\n\t// Discrete command state commits',start)
s=s[:start]+'''	auto& Budget = AuthorityState->PlanningBudget;
	FGuLiNavigationWorkBudget::FAllowance Shares[3]={{.0012,16,2},{.0004,16,2},{.0004,32,0}};
	auto RunCategory=[&](int32 Category,FGuLiNavigationWorkBudget::FAllowance& Allowance)
	{
		if (!Budget.CanWork() || Allowance.Seconds<=0) return;
		FGuLiNavigationWorkBudget::FSlice Slice(Budget,Allowance);
		if (Category==0)
		{
			if (auto* Tasks=GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>()) Tasks->PumpMoveAdmissions(Budget);
			TickMovePlanning(Budget.Projections,Budget.Paths,true);
			TickSharedNavigation(0);
		}
		else if (Category==1) { TickMovePlanning(Budget.Projections,Budget.Paths,false); TickSharedNavigation(1); }
		else TickSharedNavigation(2);
	};
	for (int32 Round=0; Round<8 && Budget.CanWork(); ++Round)
	{
		RunCategory(0,Shares[0]);
		for (int32 Offset=0; Offset<2; ++Offset) { const int32 C=1+(GFrameCounter+Offset)%2; RunCategory(C,Shares[C]); }
	}
	for (int32 C=0; C<3; ++C) AuthorityState->CategoryUsage[C]=Shares[C];
	AuthorityState->CategoryUsage[3]={};
	for (int32 Round=0; Round<8 && Budget.CanWork(); ++Round)
		for (int32 Offset=0; Offset<3 && Budget.CanWork(); ++Offset)
		{
			const int32 C=(GFrameCounter+Offset)%3;
			FGuLiNavigationWorkBudget::FAllowance Borrowed{Budget.LimitSeconds-Budget.Elapsed(),Budget.Projections,Budget.Paths};
			RunCategory(C,Borrowed);
			auto& Used=AuthorityState->CategoryUsage[C]; Used.UsedSeconds+=Borrowed.UsedSeconds;
			Used.UsedProjections+=Borrowed.UsedProjections; Used.UsedPaths+=Borrowed.UsedPaths;
		}''' +s[end:]
replace('CommitReadyMovePlans(); CommitReadyNavigationRepairs();','CommitReadyMovePlans();')
# Keep the final aggregation portion of PrepareNextMoveBatch, replace its planning preparation.
start=s.index('\tJob.Members.Reset(); Job.Cohorts.Reset();',s.index('void UGuLiBattleAuthoritySubsystem::PrepareNextMoveBatch'))
end=s.index('EGuLiMovePlanningStatus UGuLiBattleAuthoritySubsystem::PollMovePlanning',start)
s=s[:start]+'''	Job.Members.Reset(); Job.Ack.CohortResults.Reset();
	const auto& Source=Job.FullSelection.Cohorts[Job.NextCohort++];
	auto& Receipt=Job.Ack.CohortResults.AddDefaulted_GetRef();
	Receipt.CohortId=Source.CohortId; Receipt.MemberCount=uint8(Source.MemberIds.Num());
	for (int32 I=0; I<Source.MemberIds.Num(); ++I)
	{
		auto& M=Job.Members.AddDefaulted_GetRef(); M.SoldierId=Source.MemberIds[I]; M.CohortId=Source.CohortId; M.CohortMemberIndex=I;
		const auto* Index=AuthorityState->SoldierIndexById.Find(M.SoldierId.Value);
		const auto* Version=Job.FrozenTaskGenerations.Find(M.SoldierId.Value);
		if (!Index || !Version) continue;
		const auto& Soldier=AuthorityState->Soldiers[*Index]; M.TaskGeneration=*Version;
		M.bEligible=Soldier.CanAct() && Soldier.TaskGeneration==*Version && Soldier.Team==Job.Team
			&& (!Job.bAutomatic || Soldier.bAutomaticAdvance);
		if (M.bEligible) Receipt.EligibleMemberMask|=1u<<I;
	}
	Job.TotalEligible+=FPlatformMath::CountBits(Receipt.EligibleMemberMask);
	Job.bCommitReady=true; Job.Stage=EMovePlanningStage::ReadyToCommit;
}

void UGuLiBattleAuthoritySubsystem::TickMovePlanning(int32& RemainingProjectionBudget,int32& RemainingPathBudget,bool bManualFirst)
{
	using namespace GuLiCommanderMassPrivate;
	if (!AuthorityState) return;
	FGuLiNavigationWorkBudget::FScope Scope(AuthorityState->PlanningBudget);
	AuthorityState->MovePlanningJobs.RemoveAll([](const auto& J)
	{ return !J || (!J->bAutomatic && !J->PlayerState.IsValid()) || (J->bAutomatic && J->Stage==EMovePlanningStage::Completed && FPlatformTime::Seconds()-J->PlanningStartedAt>35.); });
	int32 Remaining=AuthorityState->MovePlanningJobs.Num(); auto& Cursor=AuthorityState->PlanningCursor[bManualFirst?0:1];
	while (Remaining-- && AuthorityState->PlanningBudget.CanWork() && !AuthorityState->MovePlanningJobs.IsEmpty())
	{
		Cursor%=AuthorityState->MovePlanningJobs.Num(); auto& Job=*AuthorityState->MovePlanningJobs[Cursor++];
		if (Job.bAutomatic==bManualFirst || Job.Stage!=EMovePlanningStage::PrepareBatch) continue;
		if (!Job.bFirstWorkRecorded)
		{
			Job.bFirstWorkRecorded=true; auto Trace=GuLiMoveLatency::Context(Job.PlayerState.Get(),Job.SourceCommandId,Job.SharedBatchOrderId);
			Trace.Execution=Job.Request.ClientCommandId; Trace.Plan=Job.Handle.Value; GuLiMoveLatency::Record(TEXT("first-work"),Trace);
		}
		PrepareNextMoveBatch(Job.Handle.Value);
	}
}

void UGuLiBattleAuthoritySubsystem::CommitReadyMovePlans()
{
	using namespace GuLiCommanderMassPrivate;
	if (!AuthorityState || !AuthorityState->MassEntitySubsystem.IsValid()) return;
	UWorld* World=GetWorld(); auto& A=*AuthorityState;
	auto& Manager=A.MassEntitySubsystem->GetMutableEntityManager();
	FGuLiNavigationWorkBudget::FScope Scope(A.CommitBudget);
	for (auto& Ptr : A.MovePlanningJobs)
	{
		if (!Ptr || !Ptr->bCommitReady || Ptr->Stage==EMovePlanningStage::Completed) continue;
		auto& Job=*Ptr;
		if (!A.CommitBudget.CanWork() || A.CommitBudget.Members<Job.Members.Num()) break;
		const auto* Owner=Job.PlayerState.Get(); const auto* Game=World->GetGameState<AGuLiBattleGameState>();
		if (!Game || Game->GetMatchEpoch()!=Job.AuthorityEpoch)
		{ CompleteMovePlanningJobWithSystemFailure(Job,EGuLiCommandAckResult::Cancelled); continue; }
		if (!Job.bAutomatic && (!Owner || !Owner->IsCommander() || Owner->GetTeam()!=Job.Team || !IsMovePlanningOwnerCurrent(Job,*Owner)))
		{ CompleteMovePlanningJobWithSystemFailure(Job,EGuLiCommandAckResult::Unauthorized); continue; }
		if (!Job.bActorsSubmitted && Owner && !Job.FullSelection.ActorIds.IsEmpty())
		{
			FGuLiMiningCommand C; C.RequestId=Job.Request.ClientCommandId; C.Type=EGuLiMiningOrderType::Move;
			C.Target=Job.Request.Target; C.SelectionRevision=Job.Request.SelectionRevision;
			Job.bActorsAccepted=World->GetSubsystem<UGuLiCommanderResourceAdapter>()->IssueMiningCommand(*Owner,Job.FullSelection.ActorIds,C);
			Job.bActorsSubmitted=true;
		}
		FGuLiBattleAuthorityState::FCommandGroup* Group=nullptr;
		if (Owner && Job.SourceCommandId) Group=&A.CommandGroups.FindOrAdd({FObjectKey(Owner),Job.SourceCommandId});
		if (Group) { Job.SharedBatchOrderId=Group->Batch; Group->LastUsed=FPlatformTime::Seconds(); }
		if (!Job.SharedBatchOrderId) Job.SharedBatchOrderId=AllocateNonZero(A.NextBatchOrderId);
		if (Group) Group->Batch=Job.SharedBatchOrderId;
		auto Intent=Group ? Group->Intent.Pin() : TSharedPtr<FGuLiSharedMoveIntent>();
		if (!Intent)
		{
			Intent=MakeShared<FGuLiSharedMoveIntent>(); Intent->Click=Job.Request.Target; Intent->Goal=A.SharedRoutes.Goal(Intent->Click);
			A.MoveIntents.Add(Intent); if (Group) Group->Intent=Intent;
		}
		auto Trace=GuLiMoveLatency::Context(Owner,Job.SourceCommandId,Job.SharedBatchOrderId);
		Trace.Epoch=Job.AuthorityEpoch; Trace.Execution=Job.Request.ClientCommandId; Trace.Plan=Job.Handle.Value;
		int32 Accepted=0;
		for (auto Receipt : Job.Ack.CohortResults)
		{
			auto& Updated=Job.UpdatedSelection.Cohorts.AddDefaulted_GetRef(); Updated.CohortId=Receipt.CohortId; Updated.ActiveOrderId=Job.SharedBatchOrderId;
			for (auto& M : Job.Members)
			{
				const auto* Index=A.SoldierIndexById.Find(M.SoldierId.Value);
				auto* Soldier=Index ? &A.Soldiers[*Index] : nullptr;
				const bool Valid=M.bEligible && Soldier && Soldier->CanAct() && Soldier->Team==Job.Team && Soldier->TaskGeneration==M.TaskGeneration
					&& Manager.IsEntityValid(Soldier->Entity) && (!Job.bAutomatic || Soldier->bAutomaticAdvance);
				if (!Valid) { Job.Progress.Failed.Add(M.SoldierId); Job.Debug.FailedSoldierIds.Add(M.SoldierId); Job.FinishedIds.Add(M.SoldierId.Value); continue; }
				auto& S=*Soldier; S.CommandStartLocation=S.Location; S.ActiveOrderId=Job.SharedBatchOrderId;
				S.MoveIntent=Intent; S.RouteGoal=Intent->Goal; S.SharedRoute.Reset(); S.ActiveNavigation.Reset();
				S.bHasDockTarget=false; S.DockLayoutRevision=Intent->LayoutRevision; S.RouteCursor=0;
				S.FinalDestination=FNavLocation(Intent->Click); S.bHasFinalDestination=true;
				S.bAutomaticAdvance=Job.bAutomatic; S.bAttackMoveHolding=false;
				S.NavigationState=EGuLiSoldierNavigationState::Normal; S.NavigationFailure=EGuLiSoldierNavigationFailure::None;
				S.PersonalPathPoints.Reset(); S.PersonalPathRetries=0; S.ConsecutiveSurfaceFailures=S.TotalSurfaceFailures=0;
				S.NoProgressSeconds=0; S.BestWaypointDistanceCentimeters=TNumericLimits<float>::Max();
				S.bForceMovementUpdate=true; S.LastMovementUpdateSimulationSeconds=A.SimulationSeconds;
				S.CurrentNavigationWaypoint=Intent->Click; S.CommandAcceptedAt=FPlatformTime::Seconds();
				S.DirectionAppliedAt=S.FirstDisplacementAt=0; S.MoveTrace=Trace; ++S.StateRevision;
				auto& Order=Manager.GetFragmentDataChecked<FGuLiMassOrderFragment>(S.Entity);
				Order.ActiveOrderId=S.ActiveOrderId; Order.OrderRevision=S.StateRevision; Order.FormationTarget=Intent->Click; Order.bHasMoveTarget=true;
				auto& Move=Manager.GetFragmentDataChecked<FMassMoveTargetFragment>(S.Entity);
				Move.CreateNewAction(EMassMovementAction::Move,*World); Move.IntentAtGoal=EMassMovementAction::Stand;
				Move.Center=Intent->Click; Move.DesiredSpeed=FMassInt16Real(MovementSpeedCentimetersPerSecond);
				Manager.GetFragmentDataChecked<FGuLiMassAvoidanceOutputFragment>(S.Entity).Value=FVector::ZeroVector;
				RefreshSoldierNavigationState(S.SoldierId); Job.Progress.Committed.Add(S.SoldierId); Job.FinishedIds.Add(S.SoldierId.Value);
				Receipt.AcceptedMemberMask|=1u<<M.CohortMemberIndex; Updated.MemberIds.Add(S.SoldierId); ++Accepted;
			}
			Updated.AliveCount=uint8(Updated.MemberIds.Num());
			Receipt.Result=Receipt.AcceptedMemberMask==Receipt.EligibleMemberMask && Receipt.AcceptedMemberMask ? EGuLiCommandAckResult::Accepted
				: Receipt.AcceptedMemberMask ? EGuLiCommandAckResult::PartiallyAccepted : EGuLiCommandAckResult::PathFailed;
			Job.AggregateAck.CohortResults.Add(Receipt);
		}
		Intent->AddMembers(Accepted,Job.MaximumMemberRadiusCentimeters);
		A.CommitBudget.Members-=Job.Members.Num(); A.bForceManualAvoidanceRefresh|=Accepted>0;
		Job.TotalAccepted+=Accepted; if (Accepted && !Job.FirstCommitAt) Job.FirstCommitAt=FPlatformTime::Seconds();
		Job.LastProgressAt=FPlatformTime::Seconds(); Job.Progress.BatchOrderId=Job.SharedBatchOrderId;
		Job.bCommitReady=false; Job.Stage=EMovePlanningStage::PrepareBatch;
		GuLiMoveLatency::Record(TEXT("commit"),Trace,Accepted);
		if (Job.NextCohort>=Job.FullSelection.Cohorts.Num()) PrepareNextMoveBatch(Job.Handle.Value);
	}
}

void UGuLiBattleAuthoritySubsystem::TickSharedNavigation(int32 Category)
{
	if (!AuthorityState) return;
	auto& A=*AuthorityState; auto& Budget=A.PlanningBudget;
	FGuLiNavigationWorkBudget::FScope Scope(Budget);
	auto* System=FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	auto* Data=System ? GetCommanderNavigationData(*System) : nullptr;
	if (!Data || !Budget.CanWork()) return;
	if (Category==2)
	{
		A.SharedRoutes.Maintain(*Data,A.NavigationGeneration,Budget);
		int32 Remaining=A.MoveIntents.Num();
		while (Remaining-- && !A.MoveIntents.IsEmpty() && Budget.CanWork())
		{
			A.IntentCursor%=A.MoveIntents.Num(); auto Intent=A.MoveIntents[A.IntentCursor].Pin();
			if (!Intent) { A.MoveIntents.RemoveAtSwap(A.IntentCursor); continue; }
			++A.IntentCursor; Intent->Generate(*Data,A.NavigationGeneration,Budget);
		}
		return;
	}
	if (A.SharedDiscoveryFrame[Category]!=GFrameCounter)
	{ A.SharedDiscoveryFrame[Category]=GFrameCounter; A.SharedDiscoveryRemaining[Category]=A.Soldiers.Num(); }
	int32 Work=0;
	while (A.SharedDiscoveryRemaining[Category]>0 && !A.Soldiers.IsEmpty() && Budget.CanWork() && Work++<32)
	{
		auto& Cursor=A.SharedDiscoveryCursor[Category]; Cursor%=A.Soldiers.Num(); auto& S=A.Soldiers[Cursor++]; --A.SharedDiscoveryRemaining[Category];
		if (!S.MoveIntent || !S.ActiveOrderId || !S.CanAct() || S.bAutomaticAdvance!=(Category==1)) continue;
		if (S.SharedRoute && S.SharedRoute->Goal==S.RouteGoal && S.SharedRoute->State==FGuLiSharedMoveRoute::EState::Ready
			&& S.SharedRoute->Navigation.Data.Get()==Data && S.SharedRoute->Navigation.Path->IsUpToDate()
			&& S.SharedRoute->PolygonIndex.Contains(S.LastValidNavLocation.NodeRef)) { S.SharedRoute->LastUsed=FPlatformTime::Seconds(); continue; }
		if (!S.RouteGoal || !S.RouteGoal->Target.Equals(S.FinalDestination.Location,.01)) S.RouteGoal=A.SharedRoutes.Goal(S.FinalDestination.Location);
		S.SharedRoute=A.SharedRoutes.Request(S.RouteGoal,S.LastValidNavLocation,Category==1,*Data,A.NavigationGeneration);
	}
	A.SharedRoutes.Tick(Category==1,*System,*Data,A.NavigationGeneration,Budget);
}

''' +s[end:]
# No NavMesh is required to accept a movement intent; queries and surface movement may wait for it.
replace('if (!World || !NavigationSystem || !NavigationData || CurrentAuthorityEpoch == 0u)','if (!World || CurrentAuthorityEpoch == 0u)')
replace('if (TeamJobs>=8)', 'if (TeamJobs>=128)')
replace('if (TeamJobs>=4)', 'if (TeamJobs>=128)')
# Reservations are gone from the authoritative endpoint updater. Clearing releases all command-owned references.
replace('if (Soldier) AuthorityState->MoveReservations.Update(*Soldier); else AuthorityState->MoveReservations.Remove(Id.Value);','''if (Index && (!Soldier->ActiveOrderId || !Soldier->CanAct()))
	{
		auto& S=AuthorityState->Soldiers[*Index]; S.MoveIntent.Reset(); S.RouteGoal.Reset(); S.SharedRoute.Reset();
	}''')
replace('const auto* Previous = AuthorityState->ActiveEndpoints.Find(Id.Value);','const FVector DisplayTarget=Soldier->MoveIntent ? Soldier->MoveIntent->Click : Soldier->FinalDestination.Location;\n\tconst auto* Previous = AuthorityState->ActiveEndpoints.Find(Id.Value);')
replace('Previous->FinalDestination.Equals(Soldier->FinalDestination.Location,.01)','Previous->FinalDestination.Equals(DisplayTarget,.01)')
replace('E.FinalDestination = Soldier->FinalDestination.Location;','E.FinalDestination = DisplayTarget;')
# Arrival never teleports to a goal. The movement integrator owns the real surface position.
replace('\t\t\tSoldier.Location = Soldier.FinalDestination.Location;\n\t\t\tSoldier.LastValidNavLocation = Soldier.FinalDestination;','')
# Replace formation movement preparation entirely: no lane validation, slot matching, recovery or flow sampling.
start=s.index('\tfor (FOrderFormationRuntime& Formation : AuthorityState->OrderFormations)',s.index('void UGuLiBattleAuthoritySubsystem::TickAuthority'))
end=s.index('\n\tconst bool bPeriodicManualAvoidanceRefresh',start)
s=s[:start]+'''	for (int32 I=0; I<AuthorityState->Soldiers.Num(); ++I)
	{
		auto& S=AuthorityState->Soldiers[I];
		if (!S.MoveIntent || !S.ActiveOrderId || !S.CanAct() || !bRunsMovementUpdate[I]) continue;
		auto& Intent=*S.MoveIntent;
		if (S.DockLayoutRevision!=Intent.LayoutRevision)
		{ S.DockLayoutRevision=Intent.LayoutRevision; S.bHasDockTarget=false; S.FinalDestination=FNavLocation(Intent.Click); S.SharedRoute.Reset(); S.RouteGoal=Intent.Goal; }
		const float Distance=FVector::Dist2D(S.Location,S.FinalDestination.Location);
		if (Distance<S.BestWaypointDistanceCentimeters-ProgressDistanceCentimeters)
		{ S.BestWaypointDistanceCentimeters=Distance; S.NoProgressSeconds=0; }
		else S.NoProgressSeconds+=MovementUpdateDeltaSeconds[I];
		if ((!S.bHasDockTarget && FVector::DistSquared2D(S.Location,Intent.Click)<=FMath::Square(Intent.Pitch))
			|| (S.NoProgressSeconds>=1.f && Intent.InCoverage(S.Location)))
		{
			FNavLocation Dock;
			if (Intent.Nearby(S.Location,Dock) && (!S.bHasDockTarget || !S.FinalDestination.Location.Equals(Dock.Location,.01)))
			{
				S.bHasDockTarget=true; S.FinalDestination=Dock; S.SharedRoute.Reset(); S.RouteGoal.Reset();
				S.NoProgressSeconds=0; S.BestWaypointDistanceCentimeters=TNumericLimits<float>::Max();
			}
		}
		const float Tolerance=FMath::Max(20.f,MovementSpeedCentimetersPerSecond*MovementUpdateDeltaSeconds[I]);
		if (S.bHasDockTarget && FVector::DistSquared2D(S.Location,S.FinalDestination.Location)<=FMath::Square(Tolerance)
			&& FMath::Abs(S.Location.Z-S.FinalDestination.Location.Z)<=MaximumSurfaceStepZCentimeters)
		{ SetTerminalNavigationState(S,EGuLiSoldierNavigationState::Arrived,EGuLiSoldierNavigationFailure::None); continue; }
		FVector Waypoint=S.FinalDestination.Location;
		if (S.SharedRoute && S.SharedRoute->Goal==S.RouteGoal && S.SharedRoute->Navigation.Data.Get()==CommanderNavigationData)
			S.SharedRoute->Sample(S.LastValidNavLocation,FMath::Min(50.f,Tolerance),S.RouteCursor,Waypoint);
		S.CurrentNavigationWaypoint=Waypoint;
		DesiredVelocities[I]=(Waypoint-S.Location).GetSafeNormal2D()*MovementSpeedCentimetersPerSecond;
		bReceivesAvoidance[I]=true; ++AuthorityState->MovementUpdateCalls;
		AuthorityState->ForcedMovementUpdateCalls+=bForcedMovementUpdate[I]?1u:0u;
		if (!S.DirectionAppliedAt && !DesiredVelocities[I].IsNearlyZero())
		{
			S.DirectionAppliedAt=FPlatformTime::Seconds();
			const FString Detail=FString::Printf(TEXT("soldier=%u acceptedMs=%.3f route=%llu"),S.SoldierId.Value,(S.DirectionAppliedAt-S.CommandAcceptedAt)*1000.,S.SharedRoute?S.SharedRoute->Handle:0);
			GuLiMoveLatency::Record(TEXT("direction"),S.MoveTrace,1,*Detail);
		}
		auto& Move=EntityManager.GetFragmentDataChecked<FMassMoveTargetFragment>(S.Entity);
		Move.Center=Waypoint; Move.Forward=DesiredVelocities[I].GetSafeNormal(); Move.DesiredSpeed=FMassInt16Real(MovementSpeedCentimetersPerSecond);
		EntityManager.GetFragmentDataChecked<FGuLiMassSlotTargetFragment>(S.Entity).WorldTarget=Waypoint;
	}
''' +s[end:]
# Remove terminal personal-path timeout loop between avoidance and the integration loop.
start=s.index('\n\tfor (int32 SoldierIndex',s.index('AuthorityState->bForceManualAvoidanceRefresh = false;',s.index('void UGuLiBattleAuthoritySubsystem::TickAuthority')))
end=s.index('\n\tfor (int32 SoldierIndex',start+10)
s=s[:start]+s[end:]
# Immediate intent cannot fall into the old surface-failure recovery state machine.
replace('if (Soldier.NavigationState == EGuLiSoldierNavigationState::Normal\n\t\t\t\t\t\t&& GuLiCommanderNavigationPolicy::ShouldEnterCenterlineRecovery(', 'if (!Soldier.MoveIntent && Soldier.NavigationState == EGuLiSoldierNavigationState::Normal\n\t\t\t\t\t\t&& GuLiCommanderNavigationPolicy::ShouldEnterCenterlineRecovery(')
replace('else if (Soldier.NavigationState == EGuLiSoldierNavigationState::CenterlineRecovery\n\t\t\t\t\t\t&& GuLiCommanderNavigationPolicy::ShouldEnterPersonalPathRecovery(', 'else if (!Soldier.MoveIntent && Soldier.NavigationState == EGuLiSoldierNavigationState::CenterlineRecovery\n\t\t\t\t\t\t&& GuLiCommanderNavigationPolicy::ShouldEnterPersonalPathRecovery(')
replace('Soldier.Velocity=(SurfaceLocation.Location-Soldier.Location)/FMath::Max(MovementDeltaSeconds,UE_SMALL_NUMBER);', '''if (Soldier.MoveIntent && !Soldier.FirstDisplacementAt && FVector::DistSquared2D(SurfaceLocation.Location,Soldier.CommandStartLocation)>1.)
					{
						Soldier.FirstDisplacementAt=FPlatformTime::Seconds();
						const FString Detail=FString::Printf(TEXT("soldier=%u acceptedMs=%.3f"),Soldier.SoldierId.Value,(Soldier.FirstDisplacementAt-Soldier.CommandAcceptedAt)*1000.);
						GuLiMoveLatency::Record(TEXT("displacement"),Soldier.MoveTrace,1,*Detail);
					}
					Soldier.Velocity=(SurfaceLocation.Location-Soldier.Location)/FMath::Max(MovementDeltaSeconds,UE_SMALL_NUMBER);''')
p.write_text(s,encoding='utf-8')

h=Path('Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.h')
t=h.read_text(encoding='utf-8-sig').replace('void TickLocalFlowFields();','void TickLocalFlowFields();\n\tvoid TickSharedNavigation(int32 Category);').replace('Commits every ready plan at the start of one authoritative 10 Hz step.','Commits accepted intents each world frame, independently of the 10 Hz position step.')
h.write_text(t,encoding='utf-8')

h=Path('Source/GuLiStrike/Commander/Orders/GuLiUnitTaskSubsystem.h')
t=h.read_text(encoding='utf-8-sig').replace('uint64 AdmissionFrame = MAX_uint64;','uint64 AdmissionFrame = MAX_uint64;\n\tint32 AdmissionRemaining = 0;')
h.write_text(t,encoding='utf-8')
p=Path('Source/GuLiStrike/Commander/Orders/GuLiUnitTaskSubsystem.cpp')
t=p.read_text(encoding='utf-8-sig').replace('if (AdmissionFrame == GFrameCounter) { FGuLiNavigationWorkBudget::FScope Scope(Budget); StartMoveBatches(Budget); return; }\n\tAdmissionFrame = GFrameCounter;', 'if (AdmissionFrame != GFrameCounter) { AdmissionFrame = GFrameCounter; AdmissionRemaining = 128; }')
t=t.replace('const int32 Count=FMath::Min(25,AdmissionQueued.Num());','const int32 Count=FMath::Min3(8,AdmissionRemaining,AdmissionQueued.Num());')
t=t.replace('\t\tAdmissionOwnerCursor%=AdmissionOwners.Num();','\t\t--AdmissionRemaining;\n\t\tAdmissionOwnerCursor%=AdmissionOwners.Num();',1)
p.write_text(t,encoding='utf-8')
print('Immediate admission, shared-route scheduling and movement integration written.')
