from pathlib import Path
p=Path('Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp')
s=p.read_text(encoding='utf-8')
def rep(a,b,n=1):
 global s
 assert s.count(a)>=n,a[:90]
 s=s.replace(a,b,n)
rep('\t\tTMap<int32, FNavLocation> ProjectedNavByCandidateIndex;', '''		TMap<int32, FNavLocation> ProjectedNavByCandidateIndex;
		TMap<int32, FNavLocation> ProjectionCache;
		TSet<int32> InvalidProjectionCache;''')
# Connector validation is resumable; it shares the same 64-count position budget.
a=s.index('\t\tif (Job.Stage == EMovePlanningStage::Route)',s.index('void UGuLiBattleAuthoritySubsystem::TickMovePlanning'))
b=s.index('\n\t\tif (AuthorityState->PlanningBudget.CanWork() && Job.Stage == EMovePlanningStage::ReconcileReservations)',a)
t=s[a:b]
t=t.replace('Member.CommandStart = AuthorityState->Soldiers[*SoldierIndex].LastValidNavLocation;',
 'if (!Task.bPathQueried) Member.CommandStart = AuthorityState->Soldiers[*SoldierIndex].LastValidNavLocation;')
t=t.replace('[this, &Job](const int32 MemberPlanIndex)', '[this, &Job, &Task](const int32 MemberPlanIndex)')
t=t.replace('|| AuthorityState->Soldiers[*SoldierIndex].Team != Job.Team)',
 '|| AuthorityState->Soldiers[*SoldierIndex].Team != Job.Team\n\t\t\t\t\t\t|| AuthorityState->Soldiers[*SoldierIndex].TaskGeneration != Member.TaskGeneration)')
x=t.index('\t\t\t\tTArray<FVector> PathPoints;')
y=t.index('\n\t\t\t\tif (!bRouteValid && Task.MemberPlanIndices.Num() > 1)',x)
t=t[:x]+'''				TArray<FVector> PathPoints = MoveTemp(Task.CachedPath);
				if (!Task.bPathQueried)
				{
					if (!AuthorityState->PlanningBudget.TakePath())
					{ Job.RouteTasks.Insert(MoveTemp(Task),0); break; }
					++AuthorityState->PathQueries; ++AuthorityState->MovePlanningPathQueries;
					++Job.Debug.PathQueries; Job.LastProgressAt = FPlatformTime::Seconds();
					const double QueryStart = FPlatformTime::Seconds();
					Task.bPathValid = BuildCompletePathQuiet(*NavigationSystem, *NavigationData,
						StartMedoid.CommandStart, DestinationMedoid.Destination.WorldDestination, PathPoints);
					AuthorityState->PlanningBudget.RecordQuery(QueryStart);
					Task.bPathQueried = true;
				}
				bool bRouteValid = Task.bPathValid;
				EGuLiMovePlanFailureStage RouteFailure = EGuLiMovePlanFailureStage::SharedPath;
				bool bDeferredConnector = false;
				while (bRouteValid && Task.NextConnector < Task.MemberPlanIndices.Num() * 2)
				{
					if (!AuthorityState->PlanningBudget.TakeProjection()) { bDeferredConnector = true; break; }
					const auto& Member = Job.Members[Task.MemberPlanIndices[Task.NextConnector / 2]];
					const double QueryStart = FPlatformTime::Seconds();
					bRouteValid = (Task.NextConnector & 1) == 0
						? HasDirectSurfaceConnection(*NavigationData, Member.CommandStart, PathPoints[0])
						: HasDirectSurfaceConnection(*NavigationData, DestinationMedoid.DestinationNav, Member.Destination.WorldDestination);
					AuthorityState->PlanningBudget.RecordQuery(QueryStart);
					++Task.NextConnector; Job.LastProgressAt = FPlatformTime::Seconds();
					if (!bRouteValid) RouteFailure = EGuLiMovePlanFailureStage::Connector;
				}
				if (bDeferredConnector)
				{ Task.CachedPath = MoveTemp(PathPoints); Job.RouteTasks.Insert(MoveTemp(Task),0); break; }
'''+t[y:]
# A failed single-member candidate needs a fresh query when requeued.
t=t.replace('Job.RouteTasks.Add(MoveTemp(Task));', 'Task.bPathQueried = false; Task.NextConnector = 0; Task.CachedPath.Reset();\n\t\t\t\t\t\tJob.RouteTasks.Add(MoveTemp(Task));')
s=s[:a]+t+s[b:]

# Cache projected candidate positions across cohorts; dynamic reservations remain live.
a=s.index('\t\tif (Job.Stage == EMovePlanningStage::ProjectCandidates)',s.index('void UGuLiBattleAuthoritySubsystem::TickMovePlanning'))
b=s.index('\n\t\tif (AuthorityState->PlanningBudget.CanWork() && Job.Stage == EMovePlanningStage::AssignDestinations)',a)
t=s[a:b]
t=t.replace('''					--RemainingProjectionBudget;''','''					if (Job.InvalidProjectionCache.Contains(Candidate.CandidateIndex)) continue;
					const FNavLocation* CachedProjection = Job.ProjectionCache.Find(Candidate.CandidateIndex);
					if (!CachedProjection) --RemainingProjectionBudget;''')
t=t.replace('''if (!LandscapeQuery
						|| !LandscapeQuery->TryGetLandscapeHeight(
							FVector2D(Seed.X, Seed.Y), LandscapeHeight))''','''if (!CachedProjection && (!LandscapeQuery
						|| !LandscapeQuery->TryGetLandscapeHeight(FVector2D(Seed.X, Seed.Y), LandscapeHeight)))''')
t=t.replace('''					FNavLocation Projected;
					if (!ProjectPointToCommanderNavigation(''','''					FNavLocation Projected = CachedProjection ? *CachedProjection : FNavLocation{};
					const double ProjectionStarted = FPlatformTime::Seconds();
					if ((!CachedProjection && !ProjectPointToCommanderNavigation(''')
t=t.replace('''							Projected)
						||''','''							Projected))
						||''')
# Remember permanent nav failures within this nav generation, not transient reservation failures.
marker='''						++Job.Debug.FailureCounts[
							static_cast<uint8>(EGuLiMovePlanFailureStage::CandidateProjection)];'''
t=t.replace(marker, '\t\t\t\t\t\tJob.InvalidProjectionCache.Add(Candidate.CandidateIndex);\n'+marker)
t=t.replace('''
					if (IsMoveCandidateBlockedByHardReservation''','''
					if (!CachedProjection) { AuthorityState->PlanningBudget.RecordQuery(ProjectionStarted); Job.ProjectionCache.Add(Candidate.CandidateIndex, Projected); }
					if (IsMoveCandidateBlockedByHardReservation''')
t=t.replace('''					Job.ProjectedNavByCandidateIndex.Add(Slot.CandidateIndex, Projected);''', '''					Job.ProjectedNavByCandidateIndex.Add(Slot.CandidateIndex, Projected);
					Job.LegalSlotIndexByCandidate.Add(Slot.CandidateIndex, Job.LegalSlots.Num()-1);''')
s=s[:a]+t+s[b:]
rep('''			Job.NavigationGeneration = AuthorityState->NavigationGeneration;
			RebuildHardReservations(Job);''','''			Job.NavigationGeneration = AuthorityState->NavigationGeneration;
			Job.ProjectionCache.Reset(); Job.InvalidProjectionCache.Reset();
			RebuildHardReservations(Job);''')

# Native progress and authoritative endpoint cache.
a=s.index('void UGuLiBattleAuthoritySubsystem::BuildActiveMoveEndpointSnapshot(')
b=s.index('bool UGuLiBattleAuthoritySubsystem::TryGetLastMovePlanningDebug',a)
s=s[:a]+'''FGuLiMovePlanHandle UGuLiBattleAuthoritySubsystem::FindMovePlan(const AGuLiBattlePlayerState& Owner, uint32 Id) const
{
	if (AuthorityState) for (const auto& Job : AuthorityState->MovePlanningJobs)
		if (Job && Job->PlayerState.Get() == &Owner && Job->Request.ClientCommandId == Id) return Job->Handle;
	return {};
}

bool UGuLiBattleAuthoritySubsystem::ConsumeMovePlanProgress(FGuLiMovePlanHandle Handle, FGuLiMovePlanProgress& Out)
{
	Out = FGuLiMovePlanProgress{};
	if (!AuthorityState || !Handle.IsValid()) return false;
	for (int32 I = 0; I < AuthorityState->MovePlanningJobs.Num(); ++I)
	{
		auto& Job = AuthorityState->MovePlanningJobs[I];
		if (!Job || Job->Handle.Value != Handle.Value || Job->Handle.Epoch != Handle.Epoch) continue;
		Out = MoveTemp(Job->Progress); Out.Handle = Handle;
		Job->Progress = FGuLiMovePlanProgress{}; Job->Progress.Handle = Handle;
		Job->Progress.BatchOrderId = Job->SharedBatchOrderId;
		Job->Progress.bComplete = Job->Stage == GuLiCommanderMassPrivate::EMovePlanningStage::Completed;
		Out.bComplete = Job->Progress.bComplete;
		if (Out.bComplete && Job->bAutomatic) AuthorityState->MovePlanningJobs.RemoveAt(I);
		return true;
	}
	return false;
}

const FGuLiMoveEndpointSnapshot* UGuLiBattleAuthoritySubsystem::FindActiveMoveEndpoint(FGuLiSoldierId Id) const
{ return AuthorityState ? AuthorityState->ActiveEndpoints.Find(Id.Value) : nullptr; }

void UGuLiBattleAuthoritySubsystem::RefreshSoldierNavigationState(FGuLiSoldierId Id)
{
	if (!AuthorityState) return;
	const int32* Index = AuthorityState->SoldierIndexById.Find(Id.Value);
	const auto* Soldier = Index && AuthorityState->Soldiers.IsValidIndex(*Index) ? &AuthorityState->Soldiers[*Index] : nullptr;
	if (Soldier) AuthorityState->MoveReservations.Update(*Soldier); else AuthorityState->MoveReservations.Remove(Id.Value);
	if (!Soldier || !Soldier->CanAct() || !Soldier->ActiveOrderId || !Soldier->bHasFinalDestination)
	{
		if (AuthorityState->ActiveEndpoints.Remove(Id.Value))
		{ AuthorityState->DirtyEndpointIds.Remove(Id.Value); AuthorityState->RemovedEndpointIds.Add(Id.Value); }
		return;
	}
	const auto* Previous = AuthorityState->ActiveEndpoints.Find(Id.Value);
	if (Previous && Previous->ActiveOrderId == Soldier->ActiveOrderId && Previous->Team == Soldier->Team
		&& Previous->CommandStart.Equals(Soldier->CommandStartLocation,.01)
		&& Previous->FinalDestination.Equals(Soldier->FinalDestination.Location,.01)) return;
	FGuLiMoveEndpointSnapshot E; E.SoldierId = Id; E.Team = Soldier->Team;
	E.ActiveOrderId = Soldier->ActiveOrderId; E.Revision = Soldier->StateRevision;
	E.CommandStart = Soldier->CommandStartLocation; E.FinalDestination = Soldier->FinalDestination.Location;
	AuthorityState->ActiveEndpoints.Add(Id.Value,E);
	AuthorityState->DirtyEndpointIds.Add(Id.Value); AuthorityState->RemovedEndpointIds.Remove(Id.Value);
}

void UGuLiBattleAuthoritySubsystem::PublishMoveEndpointChanges()
{
	if (!AuthorityState || (AuthorityState->DirtyEndpointIds.IsEmpty() && AuthorityState->RemovedEndpointIds.IsEmpty())) return;
	FGuLiMoveEndpointDelta Delta;
	for (uint32 Id : AuthorityState->DirtyEndpointIds)
		if (const auto* E = AuthorityState->ActiveEndpoints.Find(Id)) Delta.Upserts.Add(*E);
	for (uint32 Id : AuthorityState->RemovedEndpointIds) Delta.Removed.Add(FGuLiSoldierId(Id));
	AuthorityState->DirtyEndpointIds.Reset(); AuthorityState->RemovedEndpointIds.Reset();
	OnMoveEndpointsChanged.Broadcast(Delta);
}

void UGuLiBattleAuthoritySubsystem::BuildActiveMoveEndpointSnapshot(EGuLiTeam Team, TArray<FGuLiMoveEndpointSnapshot>& Out) const
{
	Out.Reset(); if (!AuthorityState) return;
	for (const auto& Pair : AuthorityState->ActiveEndpoints) if (Pair.Value.Team == Team) Out.Add(Pair.Value);
	Out.Sort([](const auto& A, const auto& B) { return A.SoldierId < B.SoldierId; });
}

'''+s[b:]

# Existing safety repair work consumes the same time budget.
a=s.index('void UGuLiBattleAuthoritySubsystem::TickNavigationRepairs(')
b=s.index('void UGuLiBattleAuthoritySubsystem::CommitReadyNavigationRepairs()',a)
t=s[a:b]
brace=t.index('{')
t=t[:brace+1]+'''
	if (!AuthorityState) return;
	FGuLiNavigationWorkBudget::FScope BudgetScope(AuthorityState->PlanningBudget);
	if (!AuthorityState->PlanningBudget.CanWork()) return;'''+t[brace+1:]
t=t.replace('if (RemainingProjectionBudget <= 0)', 'if (!AuthorityState->PlanningBudget.CanWork() || RemainingProjectionBudget <= 0)')
t=t.replace('if (RemainingPathBudget <= 0)', 'if (!AuthorityState->PlanningBudget.CanWork() || RemainingPathBudget <= 0)')
s=s[:a]+t+s[b:]

# Recovery queries previously had their own per-fixed-step count (a budget bypass).
rep('''	for (int32 QueryIndex = 0; QueryIndex < PathQueryCount; ++QueryIndex)
	{
		FSoldierRuntime& Soldier''','''	for (int32 QueryIndex = 0; QueryIndex < PathQueryCount; ++QueryIndex)
	{
		FGuLiNavigationWorkBudget::FScope Scope(AuthorityState->PlanningBudget);
		if (!AuthorityState->PlanningBudget.TakePath()) break;
		FSoldierRuntime& Soldier''')
rep('''		const FPathFindingResult Result = NavigationSystem->FindPathSync(MoveTemp(Query));''','''		const double QueryStarted = FPlatformTime::Seconds();
		const FPathFindingResult Result = NavigationSystem->FindPathSync(MoveTemp(Query));
		AuthorityState->PlanningBudget.RecordQuery(QueryStarted);''')

p.write_text(s,encoding='utf-8')
print('Resumable connectors, shared budget, endpoint events and native progress added')
