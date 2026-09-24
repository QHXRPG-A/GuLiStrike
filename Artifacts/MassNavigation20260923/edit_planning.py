from pathlib import Path
p=Path('Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp')
s=p.read_text(encoding='utf-8')
def replace(a,b,count=1):
    global s
    assert s.count(a)>=count, a[:100]
    s=s.replace(a,b,count)

replace('\tTArray<TUniquePtr<GuLiCommanderMassPrivate::FMovePlanningJob>> MovePlanningJobs;', '''	TArray<TUniquePtr<GuLiCommanderMassPrivate::FMovePlanningJob>> MovePlanningJobs;
	GuLiCommanderMassPrivate::FMoveReservationLedger MoveReservations;
	FGuLiNavigationWorkBudget PlanningBudget;
	FGuLiNavigationWorkBudget CommitBudget;
	uint64 NextPlanId = 1;
	int32 PlanningCursor = 0;
	int32 ReservationBootstrapCursor = 0;
	TMap<uint32, FGuLiMoveEndpointSnapshot> ActiveEndpoints;
	TSet<uint32> DirtyEndpointIds;
	TSet<uint32> RemovedEndpointIds;''')
replace('''	int32 RemainingProjectionBudget = GuLiCommanderMassPrivate::MoveCandidateProjectionBudgetPerFrame;
	int32 RemainingPathBudget = GuLiCommanderMassPrivate::MovePathQueryBudgetPerFrame;
	TickNavigationRepairs(RemainingProjectionBudget, RemainingPathBudget);
	TickLocalFlowFields();
	TickMovePlanning(RemainingProjectionBudget, RemainingPathBudget);''','''	AuthorityState->PlanningBudget.Reset(MovePlanningMilliseconds, MovePositionQueriesPerFrame, MovePathQueriesPerFrame);
	AuthorityState->CommitBudget.Reset(MoveCommitMilliseconds, 0, 0, FMath::Max(25, MoveCommitMembersPerFrame));
	{
		FGuLiNavigationWorkBudget::FScope Scope(AuthorityState->PlanningBudget);
		int32 BootstrapCount = 0;
		while (AuthorityState->ReservationBootstrapCursor < AuthorityState->Soldiers.Num()
			&& BootstrapCount++ < 256 && AuthorityState->PlanningBudget.CanWork())
			RefreshSoldierNavigationState(AuthorityState->Soldiers[AuthorityState->ReservationBootstrapCursor++].SoldierId);
	}
	// Rotate priority, so a continuous stream of repairs cannot starve new orders.
	if ((GFrameCounter & 1u) == 0)
		TickNavigationRepairs(AuthorityState->PlanningBudget.Projections, AuthorityState->PlanningBudget.Paths);
	TickMovePlanning(AuthorityState->PlanningBudget.Projections, AuthorityState->PlanningBudget.Paths);
	if ((GFrameCounter & 1u) != 0)
		TickNavigationRepairs(AuthorityState->PlanningBudget.Projections, AuthorityState->PlanningBudget.Paths);
	TickLocalFlowFields();''')
replace('''		AuthorityState->FixedStepAccumulator -= GuLiCommanderMassPrivate::FixedStepSeconds;
	}
}''','''		AuthorityState->FixedStepAccumulator -= GuLiCommanderMassPrivate::FixedStepSeconds;
	}
	PublishMoveEndpointChanges();
}''')

# Keep request admission cheap: freeze identity/version, defer all navigation and destinations.
start=s.index('\tTUniquePtr<FMovePlanningJob> NewJob =',s.index('bool UGuLiBattleAuthoritySubsystem::BeginMovePlanning'))
end=s.index('\n\treturn true;\n}',start)
s=s[:start]+'''	TUniquePtr<FMovePlanningJob> NewJob = MakeUnique<FMovePlanningJob>();
	FMovePlanningJob& Job = *NewJob;
	Job.Handle = {AuthorityState->NextPlanId++, CurrentAuthorityEpoch};
	Job.Progress.Handle = Job.Handle;
	Job.PlayerState = &PlayerState;
	Job.OwningController = Cast<AController>(PlayerState.GetOwner());
	Job.bRequireOwningController = Job.OwningController.IsValid();
	Job.Team = PlayerState.GetTeam();
	Job.Request = Request;
	Job.FullSelection = Selection;
	Job.UpdatedSelection = Selection;
	Job.UpdatedSelection.Cohorts.Reset();
	Job.NavigationGeneration = AuthorityState->NavigationGeneration;
	Job.AuthorityEpoch = CurrentAuthorityEpoch;
	Job.PlanningStartedAt = Job.LastProgressAt = FPlatformTime::Seconds();
	Job.AggregateAck = OutImmediateAck;
	Job.AggregateAck.CommandKind = EGuLiCommandKind::Move;
	Job.Debug.ClientCommandId = Request.ClientCommandId;
	Job.Debug.RequestedTarget = FVector(Request.Target);
	Job.MaximumMemberRadiusCentimeters = MemberAgentRadiusCentimeters;
	Job.Reservations = &AuthorityState->MoveReservations;
	// A version must be frozen at admission, not when its later batch happens to run.
	for (const auto& Cohort : Selection.Cohorts) for (const auto Id : Cohort.MemberIds)
		if (const int32* Index = AuthorityState->SoldierIndexById.Find(Id.Value))
		{
			const auto& Soldier = AuthorityState->Soldiers[*Index];
			Job.FrozenTaskGenerations.Add(Id.Value, Soldier.TaskGeneration);
			Job.MaximumMemberRadiusCentimeters = FMath::Max(Job.MaximumMemberRadiusCentimeters, Soldier.AvoidanceRadiusCentimeters);
		}
	Job.MinimumSlotSpacingCentimeters = FMath::Max(DestinationMinimumSeparationCentimeters, Job.MaximumMemberRadiusCentimeters * 2);
	Job.DestinationBucketSizeCentimeters = FMath::Max(Job.MinimumSlotSpacingCentimeters,
		Job.MaximumMemberRadiusCentimeters + AuthorityState->MoveReservations.MaximumRadius);
	AuthorityState->MovePlanningJobs.Add(MoveTemp(NewJob));'''+s[end:]

# Global live reservations are queried without copying the entire army for every cohort.
needle='''		const FIntPoint CenterBucket = MakeMoveDestinationBucket(CandidateLocation, Job);
		for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)'''
replace(needle,'''		if (Job.Reservations)
		{
			const auto& Ledger = *Job.Reservations;
			const auto Cell = FMoveReservationLedger::Cell(CandidateLocation);
			const int32 Range = FMath::CeilToInt(FMath::Max(DestinationMinimumSeparationCentimeters,
				Job.MaximumMemberRadiusCentimeters + Ledger.MaximumRadius) / FMoveReservationLedger::CellSize);
			for (int32 X = -Range; X <= Range; ++X) for (int32 Y = -Range; Y <= Range; ++Y)
				if (const auto* Ids = Ledger.Cells.Find(Cell + FIntPoint(X,Y))) for (const uint32 Id : *Ids)
				{
					if (Job.ReleasedReservationIds.Contains(Id)) continue;
					const auto* E = Ledger.Entries.Find(Id);
					if (E && E->Team == Job.Team && FVector::DistSquared2D(CandidateLocation, E->Location)
						< FMath::Square(FMath::Max(DestinationMinimumSeparationCentimeters, Job.MaximumMemberRadiusCentimeters + E->RadiusCentimeters))) return true;
				}
		}
		const FIntPoint CenterBucket = MakeMoveDestinationBucket(CandidateLocation, Job);
		for (int32 OffsetX = -1; OffsetX <= 1; ++OffsetX)''')

# Failure is terminal for the remaining portion only; committed receipts survive.
a=s.index('\t\tJob.Ack.Result = Result;',s.index('void CompleteMovePlanningJobWithSystemFailure'))
b=s.index('\n\t}\n',a)
s=s[:a]+'''		for (auto CohortAck : Job.Ack.CohortResults)
		{
			CohortAck.AcceptedMemberMask = 0;
			CohortAck.Result = Result;
			Job.AggregateAck.CohortResults.Add(CohortAck);
		}
		Job.Ack = Job.AggregateAck;
		Job.Ack.BatchOrderId = Job.SharedBatchOrderId;
		Job.Ack.Result = Job.TotalAccepted ? EGuLiCommandAckResult::PartiallyAccepted : Result;
		Job.Ack.ServerSelectionRevision = Job.FullSelection.SelectionRevision;
		Job.Ack.Sanitize();
		Job.Progress.bComplete = true;
		Job.Progress.BatchOrderId = Job.SharedBatchOrderId;
		Job.Stage = EMovePlanningStage::Completed;'''+s[b:]

# A bounded batch initializer. Hungarian input is now <=25 members and one soft anchor.
insert=s.index('void UGuLiBattleAuthoritySubsystem::TickMovePlanning')
s=s[:insert]+'''void UGuLiBattleAuthoritySubsystem::PrepareNextMoveBatch(const uint64 PlanId)
{
	using namespace GuLiCommanderMassPrivate;
	using namespace GuLiCommanderDestinationPlanner;
	auto* Pointer = AuthorityState->MovePlanningJobs.FindByPredicate([PlanId](const auto& J) { return J && J->Handle.Value == PlanId; });
	if (!Pointer) return;
	auto& Job = **Pointer;
	if (Job.NextCohort >= Job.FullSelection.Cohorts.Num())
	{
		Job.Ack = Job.AggregateAck;
		Job.Ack.BatchOrderId = Job.SharedBatchOrderId;
		Job.Ack.Result = Job.TotalAccepted == 0 ? EGuLiCommandAckResult::PathFailed
			: Job.TotalAccepted == Job.TotalEligible ? EGuLiCommandAckResult::Accepted : EGuLiCommandAckResult::PartiallyAccepted;
		Job.bSelectionChanged = !AreSelectionsEqual(Job.FullSelection, Job.UpdatedSelection);
		Job.Ack.ServerSelectionRevision = Job.FullSelection.SelectionRevision;
		Job.Progress.bComplete = true; Job.Progress.BatchOrderId = Job.SharedBatchOrderId;
		Job.Debug.AcceptedMembers = Job.TotalAccepted;
		Job.Debug.FailedMembers = Job.FrozenTaskGenerations.Num() - Job.TotalAccepted;
		Job.Debug.BatchOrderId = Job.SharedBatchOrderId;
		Job.Debug.PlanningMilliseconds = (FPlatformTime::Seconds() - Job.PlanningStartedAt) * 1000;
		AuthorityState->LastMovePlanningDebug = Job.Debug; AuthorityState->bHasLastMovePlanningDebug = true;
		UE_LOG(LogGuLiCommanderMass, Display, TEXT("Incremental move plan=%llu batch=%u accepted=%d failed=%d firstBatchMs=%.2f totalMs=%.2f queryOverruns=%llu maxQueryMs=%.3f"),
			Job.Handle.Value, Job.SharedBatchOrderId, Job.TotalAccepted, Job.Debug.FailedMembers,
			Job.FirstCommitAt > 0 ? (Job.FirstCommitAt - Job.PlanningStartedAt) * 1000 : -1,
			Job.Debug.PlanningMilliseconds, AuthorityState->PlanningBudget.QueryOverruns, AuthorityState->PlanningBudget.MaximumQuerySeconds * 1000);
		Job.Stage = EMovePlanningStage::Completed;
		return;
	}
	Job.Members.Reset(); Job.Cohorts.Reset(); Job.MemberIndexById.Reset();
	Job.ReleasedReservationIds.Reset(); Job.PlannerRequest = FRequest{};
	Job.HardReservations.Reset(); Job.HardReservationBuckets.Reset();
	Job.LegalSlots.Reset(); Job.LegalSlotBuckets.Reset(); Job.LegalSlotIndexByCandidate.Reset();
	Job.ProjectedNavByCandidateIndex.Reset(); Job.CandidateOwnerMemberPlanIndex.Reset();
	Job.RejectedCandidatePairs.Reset(); Job.RouteTasks.Reset(); Job.PreparedFormations.Reset();
	Job.NextStartValidationIndex = Job.NextCandidateProjectionIndex = 0;
	Job.CandidateProjectionLimit = Job.DesiredLegalSlotCount = 0;
	Job.bEscalatedToFullCandidatePool = false;
	Job.FrozenSelection = FGuLiCommanderSelectionState{};
	Job.FrozenSelection.SelectionRevision = Job.FullSelection.SelectionRevision;
	const auto& Source = Job.FullSelection.Cohorts[Job.NextCohort++];
	Job.FrozenSelection.Cohorts.Add(Source);
	Job.Ack = FGuLiCommandAck{};
	auto& Receipt = Job.Ack.CohortResults.AddDefaulted_GetRef();
	Receipt.CohortId = Source.CohortId; Receipt.MemberCount = static_cast<uint8>(Source.MemberIds.Num());
	auto& Cohort = Job.Cohorts.AddDefaulted_GetRef(); Cohort.CohortId = Source.CohortId; Cohort.FrozenMemberIds = Source.MemberIds;
	for (int32 M = 0; M < Source.MemberIds.Num(); ++M)
	{
		auto& Member = Job.Members.AddDefaulted_GetRef();
		Member.SoldierId = Source.MemberIds[M]; Member.CohortId = Source.CohortId; Member.CohortMemberIndex = M;
		Cohort.MemberPlanIndices.Add(M); Job.MemberIndexById.Add(Member.SoldierId.Value, M);
		const auto* Index = AuthorityState->SoldierIndexById.Find(Member.SoldierId.Value);
		const auto* Version = Job.FrozenTaskGenerations.Find(Member.SoldierId.Value);
		if (!Index || !Version) { Member.FailureStage = EGuLiMovePlanFailureStage::MemberInvalid; continue; }
		const auto& Soldier = AuthorityState->Soldiers[*Index];
		Member.TaskGeneration = *Version;
		if (!Soldier.CanAct() || Soldier.TaskGeneration != *Version || Soldier.Team != Job.Team
			|| (Job.bAutomatic && !Soldier.bAutomaticAdvance)) { Member.FailureStage = EGuLiMovePlanFailureStage::MemberInvalid; continue; }
		Member.bEligible = true; Receipt.EligibleMemberMask |= 1u << M;
		Member.OldReservation = Soldier.bHasFinalDestination && Soldier.ActiveOrderId ? Soldier.FinalDestination.Location : Soldier.LastValidNavLocation.Location;
		Job.ReleasedReservationIds.Add(Member.SoldierId.Value);
	}
	Job.TotalEligible += FPlatformMath::CountBits(Receipt.EligibleMemberMask);
	if (Job.HexCandidates.IsEmpty())
	{
		FHexCandidateRequest CandidateRequest; CandidateRequest.TargetAnchor = FVector(Job.Request.Target);
		CandidateRequest.CandidatePitchCentimeters = DefaultFreeCandidatePitchCentimeters + Job.MinimumSlotSpacingCentimeters - DestinationMinimumSeparationCentimeters;
		CandidateRequest.MaximumRadiusCentimeters = FreeDestinationMaximumRadiusCentimeters * CandidateRequest.CandidatePitchCentimeters / DefaultFreeCandidatePitchCentimeters;
		Job.Debug.MaximumSearchRadiusCentimeters = CandidateRequest.MaximumRadiusCentimeters;
		BuildHexCandidates(CandidateRequest, Job.HexCandidates);
		Job.Debug.TheoreticalCandidates = Job.HexCandidates.Num();
	}
	Job.NavigationGeneration = AuthorityState->NavigationGeneration;
	Job.LastProgressAt = FPlatformTime::Seconds();
	Job.Stage = EMovePlanningStage::ValidateStarts;
}

'''+s[insert:]

# Limit every legacy planning loop with the world budget, and resume only the active cohort.
a=s.index('void UGuLiBattleAuthoritySubsystem::TickMovePlanning')
b=s.index('void UGuLiBattleAuthoritySubsystem::CommitReadyMovePlans',a)
t=s[a:b]
t=t.replace('''	// A destroyed owner''','''	FGuLiNavigationWorkBudget::FScope BudgetScope(AuthorityState->PlanningBudget);
	if (AuthorityState->ReservationBootstrapCursor < AuthorityState->Soldiers.Num()) return;
	// A destroyed owner''')
t=t.replace('return !Job || !Job->PlayerState.IsValid();','return !Job || (!Job->bAutomatic && !Job->PlayerState.IsValid());')
ra=t.index('\t\tfor (const FSoldierRuntime& Soldier : AuthorityState->Soldiers)',t.index('auto RebuildHardReservations'))
rb=t.index('\n\t\tJob.PlannerRequest = FRequest{};',ra)
t=t[:ra]+'''		Job.ReleasedReservationIds = ReleasedIds;
		for (auto& Member : Job.Members) if (const auto* Index = AuthorityState->SoldierIndexById.Find(Member.SoldierId.Value))
		{
			const auto& Soldier = AuthorityState->Soldiers[*Index];
			Member.OldReservation = Soldier.bHasFinalDestination && Soldier.ActiveOrderId ? Soldier.FinalDestination.Location : Soldier.LastValidNavLocation.Location;
		}'''+t[rb:]
t=t.replace('for (const TUniquePtr<FMovePlanningJob>& JobPointer : AuthorityState->MovePlanningJobs)', '''for (int32 Visit = 0, Count = AuthorityState->MovePlanningJobs.Num(); Visit < Count; ++Visit)''')
t=t.replace('''	{
		if (!JobPointer || JobPointer->Stage''','''	{
		const auto& JobPointer = AuthorityState->MovePlanningJobs[(AuthorityState->PlanningCursor + Visit) % AuthorityState->MovePlanningJobs.Num()];
		if (!JobPointer || JobPointer->Stage''',1)
t=t.replace('if (FPlatformTime::Seconds() - Job.PlanningStartedAt >= 5.0)', '''if (FPlatformTime::Seconds() - Job.PlanningStartedAt >= 30.0
			|| FPlatformTime::Seconds() - Job.LastProgressAt >= 5.0)''')
t=t.replace('''if (!PlayerState || !IsMovePlanningOwnerCurrent(Job, *PlayerState)
			|| !PlayerState->IsCommander() || PlayerState->GetTeam() != Job.Team)''', '''if (!Job.bAutomatic && (!PlayerState || !IsMovePlanningOwnerCurrent(Job, *PlayerState)
			|| !PlayerState->IsCommander() || PlayerState->GetTeam() != Job.Team))''')
old='''		++Job.Debug.PlanningWorldFrames;'''
t=t.replace(old,'''		if (!AuthorityState->PlanningBudget.CanWork()) continue;
		if (Job.Stage == EMovePlanningStage::PrepareBatch)
		{
			PrepareNextMoveBatch(Job.Handle.Value);
			if (Job.Stage == EMovePlanningStage::Completed || !AuthorityState->PlanningBudget.CanWork()) continue;
		}
		const auto PreviousStage = Job.Stage;
		const int32 PreviousProgress = Job.NextStartValidationIndex + Job.NextCandidateProjectionIndex + Job.Debug.PathQueries;
		++Job.Debug.PlanningWorldFrames;''')
t=t.replace('while (RemainingProjectionBudget > 0','while (AuthorityState->PlanningBudget.CanWork() && RemainingProjectionBudget > 0')
t=t.replace('while (RemainingPathBudget > 0 && !Job.RouteTasks.IsEmpty())','while (AuthorityState->PlanningBudget.CanWork() && !Job.RouteTasks.IsEmpty())')
t=t.replace('while (Job.Stage == EMovePlanningStage::ProjectCandidates)','while (AuthorityState->PlanningBudget.CanWork() && Job.Stage == EMovePlanningStage::ProjectCandidates)')
t=t.replace('if (Job.Stage == EMovePlanningStage::AssignDestinations)','if (AuthorityState->PlanningBudget.CanWork() && Job.Stage == EMovePlanningStage::AssignDestinations)')
t=t.replace('if (Job.Stage == EMovePlanningStage::ReconcileReservations)','if (AuthorityState->PlanningBudget.CanWork() && Job.Stage == EMovePlanningStage::ReconcileReservations)')
t=t.replace('if (!Soldier.CanAct() || Soldier.Team != Job.Team\n', 'if (!Soldier.CanAct() || Soldier.TaskGeneration != Member.TaskGeneration || Soldier.Team != Job.Team\n')
# Progress guards must be evaluated on all normal exits. The per-stage counters update query loops below.
t=t.replace('++Job.Debug.CandidateProjectionQueries;', '++Job.Debug.CandidateProjectionQueries; Job.LastProgressAt = FPlatformTime::Seconds();')
t=t.replace('++Job.Debug.PathQueries;', '++Job.Debug.PathQueries; Job.LastProgressAt = FPlatformTime::Seconds();')
pos=t.rfind('\n\t}\n}')
t=t[:pos]+'''
		if (Job.Stage != PreviousStage || PreviousProgress != Job.NextStartValidationIndex + Job.NextCandidateProjectionIndex + Job.Debug.PathQueries)
			Job.LastProgressAt = FPlatformTime::Seconds();'''+t[pos:]
t=t[:t.rfind('\n}')]+'''
	++AuthorityState->PlanningCursor;'''+t[t.rfind('\n}'):]
s=s[:a]+t+s[b:]

# Use existing, well-established Mass commit body but bound it to this single cohort.
a=s.index('void UGuLiBattleAuthoritySubsystem::CommitReadyMovePlans()')
b=s.index('EGuLiMovePlanningStatus UGuLiBattleAuthoritySubsystem::PollMovePlanning',a)
old=s[a:b]
ca=old.index('\n\t\tfor (FOrderFormationRuntime& Formation : Job.PreparedFormations)',old.index('const uint32 BatchOrderId = AcceptedMembers'))
cb=old.index('\n\t\tJob.Ack.BatchOrderId = BatchOrderId;',ca)
body=old[ca:cb]
body=body.replace('Soldier.bAutomaticAdvance = false;', 'Soldier.bAutomaticAdvance = Job.bAutomatic;')
body=body.replace('*CommandStart = Soldier.LastValidNavLocation;', '*CommandStart = Soldier.LastValidNavLocation;\n\t\t\t\tSoldier.CommandStartLocation = CommandStart->Location;')
body=body.replace('MoveTarget.DesiredSpeed = FMassInt16Real(MovementSpeedCentimetersPerSecond);', '''MoveTarget.DesiredSpeed = FMassInt16Real(MovementSpeedCentimetersPerSecond);
				RefreshSoldierNavigationState(SoldierId);
				Job.Progress.Committed.Add(SoldierId);''')
new='''void UGuLiBattleAuthoritySubsystem::CommitReadyMovePlans()
{
	using namespace GuLiCommanderMassPrivate;
	if (!AuthorityState || !AuthorityState->bPopulationSpawned || !GetWorld()) return;
	auto* World = GetWorld();
	auto* MassSubsystem = AuthorityState->MassEntitySubsystem.Get();
	if (!MassSubsystem) return;
	auto& EntityManager = MassSubsystem->GetMutableEntityManager();
	FGuLiNavigationWorkBudget::FScope Scope(AuthorityState->CommitBudget);
	for (const auto& Pointer : AuthorityState->MovePlanningJobs)
	{
		if (!Pointer || Pointer->Stage != EMovePlanningStage::ReadyToCommit) continue;
		auto& Job = *Pointer;
		if (!AuthorityState->CommitBudget.CanWork() || AuthorityState->CommitBudget.Members < Job.Members.Num()) break;
		if (FPlatformTime::Seconds() - Job.PlanningStartedAt >= 30 || FPlatformTime::Seconds() - Job.LastProgressAt >= 5)
		{ CompleteMovePlanningJobWithSystemFailure(Job, EGuLiCommandAckResult::TimedOut); continue; }
		const auto* Owner = Job.PlayerState.Get();
		if (!Job.bAutomatic && (!Owner || !IsMovePlanningOwnerCurrent(Job, *Owner) || !Owner->IsCommander() || Owner->GetTeam() != Job.Team))
		{ CompleteMovePlanningJobWithSystemFailure(Job, EGuLiCommandAckResult::Unauthorized); continue; }
		const auto* GameState = World->GetGameState<AGuLiBattleGameState>();
		if (!GameState || GameState->GetMatchEpoch() != Job.AuthorityEpoch)
		{ CompleteMovePlanningJobWithSystemFailure(Job, EGuLiCommandAckResult::Cancelled); continue; }
		if (Job.NavigationGeneration != AuthorityState->NavigationGeneration)
		{
			// TickMovePlanning owns the budgeted rebuild; no navigation work at the commit boundary.
			Job.Stage = EMovePlanningStage::ValidateStarts; continue;
		}
		bool bRetry = false;
		for (int32 I = 0; I < Job.Members.Num(); ++I)
		{
			auto& Member = Job.Members[I]; if (!Member.bAccepted) continue;
			const auto* Index = AuthorityState->SoldierIndexById.Find(Member.SoldierId.Value);
			if (!Index || !EntityManager.IsEntityValid(AuthorityState->Soldiers[*Index].Entity)
				|| !AuthorityState->Soldiers[*Index].CanAct() || AuthorityState->Soldiers[*Index].TaskGeneration != Member.TaskGeneration
				|| AuthorityState->Soldiers[*Index].Team != Job.Team
				|| (Job.bAutomatic && !AuthorityState->Soldiers[*Index].bAutomaticAdvance))
			{
				ReleaseMoveMemberDestination(Job, I, false); RemoveMoveMemberFromPreparedFormations(Job, Member.SoldierId);
				Job.ReleasedReservationIds.Remove(Member.SoldierId.Value); bRetry = true; continue;
			}
			const auto& Soldier = AuthorityState->Soldiers[*Index];
			if (!Soldier.LastValidNavLocation.Location.Equals(Member.CommandStart.Location, .1))
			{
				// The old order advanced after connector validation. Reconnect under the planning budget.
				Member.CommandStart = Soldier.LastValidNavLocation; Member.bAccepted = false;
				RemoveMoveMemberFromPreparedFormations(Job, Member.SoldierId);
				FMoveRouteTask Task; Task.CohortId = Member.CohortId; Task.MemberPlanIndices.Add(I);
				Job.RouteTasks.Add(MoveTemp(Task)); bRetry = true;
			}
			else if (IsMoveCandidateBlockedByHardReservation(Job, Member.Destination.WorldDestination))
			{ Member.bAccepted = false; RemoveMoveMemberFromPreparedFormations(Job, Member.SoldierId); ReleaseMoveMemberDestination(Job,I,true); bRetry = true; }
		}
		if (bRetry)
		{
			Job.Stage = Job.RouteTasks.IsEmpty() ? EMovePlanningStage::ReconcileReservations : EMovePlanningStage::Route;
			continue;
		}
		int32 AcceptedMembers = 0;
		for (const auto& Member : Job.Members) AcceptedMembers += Member.bAccepted ? 1 : 0;
		if (AcceptedMembers && !Job.SharedBatchOrderId) Job.SharedBatchOrderId = AllocateNonZero(AuthorityState->NextBatchOrderId);
		const uint32 BatchOrderId = Job.SharedBatchOrderId;
'''+body+'''
		AuthorityState->CommitBudget.Members -= Job.Members.Num();
		Job.TotalAccepted += AcceptedMembers;
		if (AcceptedMembers && Job.FirstCommitAt == 0) Job.FirstCommitAt = FPlatformTime::Seconds();
		for (auto Receipt : Job.Ack.CohortResults)
		{
			Receipt.AcceptedMemberMask = 0;
			FGuLiControlCohortDescriptor Updated; Updated.CohortId = Receipt.CohortId; Updated.ActiveOrderId = BatchOrderId;
			for (const auto& Member : Job.Members)
			{
				if (Member.bAccepted) { Receipt.AcceptedMemberMask |= 1u << Member.CohortMemberIndex; Updated.MemberIds.Add(Member.SoldierId); }
				else Job.Progress.Failed.Add(Member.SoldierId);
			}
			Receipt.Result = !Receipt.AcceptedMemberMask ? EGuLiCommandAckResult::PathFailed
				: Receipt.AcceptedMemberMask == Receipt.EligibleMemberMask ? EGuLiCommandAckResult::Accepted : EGuLiCommandAckResult::PartiallyAccepted;
			Job.AggregateAck.CohortResults.Add(Receipt);
			Updated.AliveCount = static_cast<uint8>(Updated.MemberIds.Num());
			if (Updated.AliveCount) Job.UpdatedSelection.Cohorts.Add(MoveTemp(Updated));
		}
		Job.Progress.BatchOrderId = BatchOrderId;
		Job.LastProgressAt = FPlatformTime::Seconds();
		Job.Stage = EMovePlanningStage::PrepareBatch;
	}
}

'''
s=s[:a]+new+s[b:]

# Automatic advance uses exactly the same budgeted pipeline, without a fake player owner.
a=s.index('bool UGuLiBattleAuthoritySubsystem::IssueAttackMove(')
b=s.index('\nvoid UGuLiBattleAuthoritySubsystem::ExecuteSelectedUnitSkills',a)
s=s[:a]+'''FGuLiMovePlanHandle UGuLiBattleAuthoritySubsystem::BeginAutomaticMovePlanning(EGuLiTeam Team,
	TConstArrayView<FGuLiSoldierId> Soldiers, const FVector& Destination)
{
	using namespace GuLiCommanderMassPrivate;
	if (!IsAuthorityWorld() || !AuthorityState || !AuthorityState->bPopulationSpawned || Destination.ContainsNaN()
		|| Soldiers.IsEmpty() || Soldiers.Num() > SoldierCountPerFormation) return {};
	for (const auto& Existing : AuthorityState->MovePlanningJobs)
		if (Existing && Existing->Team == Team && Existing->Stage != EMovePlanningStage::Completed) return {};
	const auto* GameState = GetWorld()->GetGameState<AGuLiBattleGameState>();
	if (!GameState || !GameState->GetMatchEpoch()) return {};
	auto Pointer = MakeUnique<FMovePlanningJob>(); auto& Job = *Pointer;
	Job.Handle = {AuthorityState->NextPlanId++, GameState->GetMatchEpoch()}; Job.Progress.Handle = Job.Handle;
	Job.bAutomatic = true; Job.Team = Team; Job.Request.Target = Destination;
	Job.PlanningStartedAt = Job.LastProgressAt = FPlatformTime::Seconds();
	Job.AuthorityEpoch = Job.Handle.Epoch; Job.NavigationGeneration = AuthorityState->NavigationGeneration;
	Job.Reservations = &AuthorityState->MoveReservations;
	auto& Cohort = Job.FullSelection.Cohorts.AddDefaulted_GetRef();
	Cohort.CohortId = FGuLiControlCohortId(AllocateNonZero(AuthorityState->NextControlCohortId));
	Cohort.MemberIds.Append(Soldiers.GetData(), Soldiers.Num());
	for (auto Id : Soldiers) if (const auto* Index = AuthorityState->SoldierIndexById.Find(Id.Value))
	{
		const auto& Soldier = AuthorityState->Soldiers[*Index];
		Job.FrozenTaskGenerations.Add(Id.Value, Soldier.TaskGeneration);
		Job.MaximumMemberRadiusCentimeters = FMath::Max(Job.MaximumMemberRadiusCentimeters, Soldier.AvoidanceRadiusCentimeters);
	}
	Job.MinimumSlotSpacingCentimeters = FMath::Max(DestinationMinimumSeparationCentimeters, Job.MaximumMemberRadiusCentimeters * 2);
	Job.DestinationBucketSizeCentimeters = FMath::Max(Job.MinimumSlotSpacingCentimeters, Job.MaximumMemberRadiusCentimeters + AuthorityState->MoveReservations.MaximumRadius);
	const auto Handle = Job.Handle; AuthorityState->MovePlanningJobs.Add(MoveTemp(Pointer)); return Handle;
}

bool UGuLiBattleAuthoritySubsystem::IssueAttackMove(EGuLiTeam Team, TConstArrayView<FGuLiSoldierId> Soldiers,
	const FVector& Destination)
{
	return BeginAutomaticMovePlanning(Team, Soldiers, Destination).IsValid();
}

'''+s[b:]

# Incremental read models are maintained where authority already touches a Soldier.
needle='\t\tFTransformFragment& Transform = EntityManager\n\t\t\t.GetFragmentDataChecked<FTransformFragment>(Soldier.Entity);\n\t\tTransform.SetTransform(FTransform('
replace(needle,'\t\tRefreshSoldierNavigationState(Soldier.SoldierId);\n'+needle)

p.write_text(s,encoding='utf-8')
print('Updated incremental planning and bounded cohort commit')
