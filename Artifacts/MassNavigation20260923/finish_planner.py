from pathlib import Path
p=Path('Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp')
s=p.read_text(encoding='utf-8-sig')
def rep(a,b,n=1):
 global s
 assert a in s,a[:100]
 s=s.replace(a,b,n)
rep('int32 NextCohort = 0;', 'int32 NextCohort = 0;\n\t\tint32 CandidateRow = INDEX_NONE;\n\t\tbool bCandidatesGenerated = false;\n\t\tTSet<uint32> FinishedIds;')
# Terminal progress never re-emits a previously committed cohort.
a=s.index('\t\tfor (auto CohortAck : Job.Ack.CohortResults)',s.index('void CompleteMovePlanningJobWithSystemFailure'))
b=s.index('\t\tJob.Ack = Job.AggregateAck;',a)
s=s[:a]+'''\t\tfor (const auto& Cohort : Job.FullSelection.Cohorts)
\t\t{
\t\t\tif (Job.AggregateAck.CohortResults.ContainsByPredicate([&](const auto& R) { return R.CohortId == Cohort.CohortId; })) continue;
\t\t\tFGuLiCohortMoveResult Receipt;
\t\t\tReceipt.CohortId = Cohort.CohortId; Receipt.MemberCount = static_cast<uint8>(Cohort.MemberIds.Num()); Receipt.Result = Result;
\t\t\tfor (auto Id : Cohort.MemberIds) if (!Job.FinishedIds.Contains(Id.Value))
\t\t\t{ Job.Progress.Failed.Add(Id); Job.FinishedIds.Add(Id.Value); }
\t\t\tJob.AggregateAck.CohortResults.Add(Receipt);
\t\t}
'''+s[b:]
# Use actual receipt element type (native name varies with current protocol source).
s=s.replace('FGuLiCohortMoveResult Receipt;', 'auto Receipt = decltype(Job.Ack.CohortResults)::ElementType{};')
a=s.index('\tif (Job.HexCandidates.IsEmpty())',s.index('void UGuLiBattleAuthoritySubsystem::PrepareNextMoveBatch'))
b=s.index('\tJob.LastProgressAt',a)
s=s[:a]+'''\tif (Job.NavigationGeneration != AuthorityState->NavigationGeneration)
\t{ Job.ProjectionCache.Reset(); Job.InvalidProjectionCache.Reset(); }
\tJob.NavigationGeneration = AuthorityState->NavigationGeneration;
'''+s[b:]
for result in ['Unauthorized','InvalidRequest']:
 a=s.index(f'\t\t\tJob.Ack.Result = EGuLiCommandAckResult::{result};',s.index('void UGuLiBattleAuthoritySubsystem::TickMovePlanning'))
 b=s.index('\t\t\tcontinue;',a)
 s=s[:a]+f'\t\t\tCompleteMovePlanningJobWithSystemFailure(Job, EGuLiCommandAckResult::{result});\n'+s[b:]
rep('if (Job.Stage == EMovePlanningStage::PrepareBatch)\n\t\t{', '''if (!Job.bCandidatesGenerated)
\t\t{
\t\t\t// Generate one lattice row per work unit. The final bounded (2263-point) sort
\t\t\t// preserves the existing distance/q/r ordering and capacity exactly.
\t\t\tconst double Pitch = DefaultFreeCandidatePitchCentimeters + Job.MinimumSlotSpacingCentimeters - DestinationMinimumSeparationCentimeters;
\t\t\tconst double Radius = FreeDestinationMaximumRadiusCentimeters * Pitch / DefaultFreeCandidatePitchCentimeters;
\t\t\tconst int32 Extent = FMath::CeilToInt(2 * Radius / (FMath::Sqrt(3.0) * Pitch)) + 1;
\t\t\tif (Job.CandidateRow == INDEX_NONE) Job.CandidateRow = 0;
\t\t\twhile (Job.CandidateRow <= Extent * 2 && AuthorityState->PlanningBudget.CanWork())
\t\t\t{
\t\t\t\tconst int32 Q = Job.CandidateRow++ - Extent;
\t\t\t\tfor (int32 R = -Extent; R <= Extent; ++R)
\t\t\t\t{
\t\t\t\t\tconst double D = double(Q*Q + Q*R + R*R) * Pitch * Pitch;
\t\t\t\t\tif (D > Radius*Radius + FMath::Max(1.e-6, Radius*Radius*1.e-12)) continue;
\t\t\t\t\tauto& C = Job.HexCandidates.AddDefaulted_GetRef(); C.AxialQ=Q; C.AxialR=R;
\t\t\t\t\tC.AnchorLocalOffset=FVector(Pitch*(Q+R*.5), Pitch*FMath::Sqrt(3.0)*.5*R,0);
\t\t\t\t\tC.WorldCandidate=FVector(Job.Request.Target)+C.AnchorLocalOffset; C.DistanceSquaredFromAnchor=D;
\t\t\t\t}
\t\t\t\tJob.LastProgressAt = FPlatformTime::Seconds();
\t\t\t}
\t\t\tif (Job.CandidateRow <= Extent*2 || !AuthorityState->PlanningBudget.CanWork()) continue;
\t\t\tJob.HexCandidates.Sort([](const auto& A,const auto& B) { return A.DistanceSquaredFromAnchor != B.DistanceSquaredFromAnchor
\t\t\t\t? A.DistanceSquaredFromAnchor < B.DistanceSquaredFromAnchor : A.AxialQ != B.AxialQ ? A.AxialQ < B.AxialQ : A.AxialR < B.AxialR; });
\t\t\tfor (int32 I=0; I<Job.HexCandidates.Num(); ++I) Job.HexCandidates[I].CandidateIndex=I;
\t\t\tJob.Debug.MaximumSearchRadiusCentimeters=Radius; Job.Debug.TheoreticalCandidates=Job.HexCandidates.Num();
\t\t\tJob.bCandidatesGenerated=true;
\t\t}
\t\tif (Job.Stage == EMovePlanningStage::PrepareBatch)
\t\t{''')
rep('Job.LegalSlotBuckets.Reset();\n\t\tJob.ProjectedNavByCandidateIndex.Reset();', 'Job.LegalSlotBuckets.Reset();\n\t\tJob.LegalSlotIndexByCandidate.Reset();\n\t\tJob.ProjectedNavByCandidateIndex.Reset();')
rep('if (!CachedProjection) --RemainingProjectionBudget;\n\t\t\t\t\t++AuthorityState->MoveCandidateProjectionQueries;\n\t\t\t\t\t++Job.Debug.CandidateProjectionQueries;', 'if (!CachedProjection) { --RemainingProjectionBudget; ++AuthorityState->MoveCandidateProjectionQueries; ++Job.Debug.CandidateProjectionQueries; }')
rep('Seed.Z = LandscapeHeight;', 'Seed.Z = CachedProjection ? CachedProjection->Location.Z : LandscapeHeight;')
rep('Job.ProjectedNavByCandidateIndex.Add(Candidate.CandidateIndex, Projected);', 'Job.LegalSlotIndexByCandidate.Add(Candidate.CandidateIndex, Job.LegalSlots.Num()-1);\n\t\t\t\t\tJob.ProjectedNavByCandidateIndex.Add(Candidate.CandidateIndex, Projected);')
a=s.index('\t\t\t\t\tFMoveMemberPlan* Member = Job.Members.FindByPredicate(',s.index('TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommander_MovePlanning_AssignDestinations)'))
b=s.index('\t\t\t\t\tconst FNavLocation* DestinationNav',a)
s=s[:a]+'''\t\t\t\t\tconst int32* MemberIndex = Job.MemberIndexById.Find(Assigned.SoldierId.Value);
\t\t\t\t\tFMoveMemberPlan* Member = MemberIndex ? &Job.Members[*MemberIndex] : nullptr;
\t\t\t\t\tconst int32* SlotIndex = Job.LegalSlotIndexByCandidate.Find(Assigned.CandidateIndex);
\t\t\t\t\tconst FFreeDestinationSlot* Slot = SlotIndex ? &Job.LegalSlots[*SlotIndex] : nullptr;
'''+s[b:]
rep('Task.MemberPlanIndices.RemoveAll([this, &Job, &Task]', 'const int32 PreviousMemberCount = Task.MemberPlanIndices.Num();\n\t\t\t\tTask.MemberPlanIndices.RemoveAll([this, &Job, &Task]')
rep('if (Task.MemberPlanIndices.IsEmpty())\n\t\t\t\t{', 'if (PreviousMemberCount != Task.MemberPlanIndices.Num())\n\t\t\t\t{ Task.bPathQueried = false; Task.NextConnector = 0; Task.CachedPath.Reset(); }\n\t\t\t\tif (Task.MemberPlanIndices.IsEmpty())\n\t\t\t\t{')
rep('if (!Soldier.LastValidNavLocation.Location.Equals(Member.CommandStart.Location, .1))', 'if (Soldier.LastValidNavLocation.NodeRef != Member.CommandStart.NodeRef)')
# Preserve acceptance until reconciliation can find another available candidate.
rep('{ Member.bAccepted = false; RemoveMoveMemberFromPreparedFormations(Job, Member.SoldierId); ReleaseMoveMemberDestination(Job,I,true); bRetry = true; }','{ bRetry = true; }')
rep('const bool bConflicts = RestoredReservations.ContainsByPredicate(', 'const bool bConflicts = IsMoveCandidateBlockedByHardReservation(Job, Member.Destination.WorldDestination) || RestoredReservations.ContainsByPredicate(')
rep('&& !RestoredReservations.ContainsByPredicate(', '&& !IsMoveCandidateBlockedByHardReservation(Job, Candidate.WorldDestination)\n\t\t\t\t\t\t\t&& !RestoredReservations.ContainsByPredicate(')
rep('else Job.Progress.Failed.Add(Member.SoldierId);', 'else Job.Progress.Failed.Add(Member.SoldierId);\n\t\t\t\tJob.FinishedIds.Add(Member.SoldierId.Value);')
rep('Job.Progress.BatchOrderId = BatchOrderId;\n\t\tJob.LastProgressAt', 'Job.Ack.CohortResults.Reset();\n\t\tJob.Progress.BatchOrderId = BatchOrderId;\n\t\tJob.LastProgressAt')
# Invalidation covers future cohorts through frozen task generations. Do not cancel
# an entire plan just because the current cohort is empty/invalid.
a=s.index('\tfor (auto& Job : AuthorityState->MovePlanningJobs)\n\t\tif (Job && Job->Stage !=',s.index('void UGuLiBattleAuthoritySubsystem::InvalidateTaskSoldierPlans'))
b=s.index('\n}',a)
s=s[:a]+s[b:]
rep('Member.FailureStage = EGuLiMovePlanFailureStage::MemberInvalid;\n\t\t\t\tMember.OldReservation', 'Member.FailureStage = EGuLiMovePlanFailureStage::MemberInvalid;\n\t\t\t\tJob->ReleasedReservationIds.Remove(Id.Value);\n\t\t\t\tMember.OldReservation')
rep('++Soldier.StateRevision;\n\t\tif (!Manager.IsEntityValid(Soldier.Entity)) continue;', '++Soldier.StateRevision;\n\t\tRefreshSoldierNavigationState(Id);\n\t\tif (!Manager.IsEntityValid(Soldier.Entity)) continue;')
rep('void UGuLiBattleAuthoritySubsystem::StopAutomaticMove(TConstArrayView<FGuLiSoldierId> Soldiers)\n{\n\tcheck(IsAuthorityWorld());', 'void UGuLiBattleAuthoritySubsystem::StopAutomaticMove(TConstArrayView<FGuLiSoldierId> Soldiers)\n{\n\tcheck(IsAuthorityWorld());\n\tInvalidateTaskSoldierPlans(Soldiers);')
rep('Soldier.Velocity = FVector::ZeroVector; ++Soldier.StateRevision;', 'Soldier.Velocity = FVector::ZeroVector; ++Soldier.StateRevision;\n\t\tRefreshSoldierNavigationState(Id);')
rep('AuthorityState->Soldiers.Reset();','AuthorityState->ActiveEndpoints.Reset(); AuthorityState->DirtyEndpointIds.Reset(); AuthorityState->RemovedEndpointIds.Reset();\n\tAuthorityState->MoveReservations = {}; AuthorityState->ReservationBootstrapCursor = 0;\n\tFGuLiMoveEndpointDelta ResetEndpoints; ResetEndpoints.bReset = true; OnMoveEndpointsChanged.Broadcast(ResetEndpoints);\n\tAuthorityState->Soldiers.Reset();')
rep('OnSoldierRetiring.Broadcast(FinalState);','AuthorityState->MoveReservations.Remove(Soldier.SoldierId.Value);\n\t\tif (AuthorityState->ActiveEndpoints.Remove(Soldier.SoldierId.Value)) AuthorityState->RemovedEndpointIds.Add(Soldier.SoldierId.Value);\n\t\tAuthorityState->DirtyEndpointIds.Remove(Soldier.SoldierId.Value);\n\t\tOnSoldierRetiring.Broadcast(FinalState);')
p.write_text(s,encoding='utf-8')
# Incremental task notifications happen before legacy final poll removes the job.
p=Path('Source/GuLiStrike/Commander/Orders/GuLiUnitTaskSubsystem.cpp'); s=p.read_text(encoding='utf-8-sig')
a='\t\tconst auto Result = Batch.Owner.IsValid() ? Authority->PollMovePlanning'
assert a in s
s=s.replace(a,'''\t\tFGuLiMovePlanProgress Progress;
\t\tif (Batch.Owner.IsValid() && Authority->ConsumeMovePlanProgress(Authority->FindMovePlan(*Batch.Owner, Batch.Id), Progress))
\t\t{
\t\t\tauto Apply = [&](FGuLiSoldierId Id, bool bCommitted)
\t\t\t{
\t\t\t\tconst auto Key = FGuLiTaskUnitId::Soldier(Id);
\t\t\t\tconst uint64* Version = Batch.Versions.Find(Key);
\t\t\t\tauto* State = States.Find(Key);
\t\t\t\tif (Version && State && !State->bCancelPending)
\t\t\t\t{
\t\t\t\t\tconst bool bReplacement = Batch.Replacements.Contains(Key);
\t\t\t\t\tauto& Execution = bReplacement ? State->PendingMove : State->Active;
\t\t\t\t\tif (Execution.IsSet() && Execution->Version == *Version)
\t\t\t\t\t{
\t\t\t\t\t\tif (bCommitted) { Execution->bPlanning=false; Execution->Status=EGuLiTaskStatus::Running;
\t\t\t\t\t\t\tif (bReplacement) { State->Active=MoveTemp(State->PendingMove); State->PendingMove.Reset(); } }
\t\t\t\t\t\telse { LogMoveFailure(*State,*Execution,TEXT("IncrementalPlanningFailure")); Execution.Reset(); }
\t\t\t\t\t}
\t\t\t\t}
\t\t\t\tBatch.Versions.Remove(Key); Batch.Replacements.Remove(Key);
\t\t\t};
\t\t\tfor (auto Id : Progress.Committed) Apply(Id,true);
\t\t\tfor (auto Id : Progress.Failed) Apply(Id,false);
\t\t}
\t\tconst auto Result = Batch.Owner.IsValid() ? Authority->PollMovePlanning''',1)
s=s.replace('const uint64* Version = Batch.Versions.Find(Key);','const auto* Version = Batch.Versions.Find(Key);')
p.write_text(s,encoding='utf-8')
p=Path('Source/GuLiStrike/Gameplay/Stronghold/GuLiArmyAdvanceSubsystem.h'); s=p.read_text(encoding='utf-8-sig').replace('bool bMoving = false;', 'uint64 PendingPlan = 0;\n\t\tuint32 PendingPlanEpoch = 0;\n\t\tbool bPlanCommitted = false;\n\t\tbool bMoving = false;'); p.write_text(s,encoding='utf-8')
p=Path('Source/GuLiStrike/Gameplay/Stronghold/GuLiArmyAdvanceSubsystem.cpp'); s=p.read_text(encoding='utf-8-sig')
s=s.replace('    Group.Center = FVector::ZeroVector;', '''    if (Group.PendingPlan)
    {
        FGuLiMovePlanProgress Progress;
        const bool bFound = Authority.ConsumeMovePlanProgress({Group.PendingPlan,Group.PendingPlanEpoch},Progress);
        Group.bPlanCommitted |= !Progress.Committed.IsEmpty();
        if (bFound && !Progress.bComplete) { Group.Result=EGuLiCommanderWorkResult::Running; return; }
        Group.PendingPlan=0;
        if (!Group.bPlanCommitted) { Group.Result=EGuLiCommanderWorkResult::Failed; return; }
    }
    Group.Center = FVector::ZeroVector;''',1)
s=s.replace('RemainingMoveQueries = 4; // Same per-world-frame path query budget as before.', 'RemainingMoveQueries = 4; // Admission throttle only; all navigation uses the authority world budget.')
s=s.replace('    const bool bAccepted = Authority.IssueAttackMove(Group->Team, Group->Soldiers, Group->Approach);','''    const auto Handle = Authority.BeginAutomaticMovePlanning(Group->Team, Group->Soldiers, Group->Approach);
    if (!Handle.IsValid()) return; // Busy team: retry the same target, never report a false path failure.
    Group->PendingPlan=Handle.Value; Group->PendingPlanEpoch=Handle.Epoch; Group->bPlanCommitted=false;''')
s=s.replace('Group->Result = bAccepted ? EGuLiCommanderWorkResult::Running : EGuLiCommanderWorkResult::Failed;', 'Group->Result = EGuLiCommanderWorkResult::Running;')
p.write_text(s,encoding='utf-8')
p=Path('Source/GuLiStrike/Commander/Framework/GuLiCommanderNetSyncComponent.cpp'); s=p.read_text(encoding='utf-8-sig').replace('PendingMoveDeadline = NowSeconds + 8.0;', 'PendingMoveDeadline = NowSeconds + 35.0;'); p.write_text(s,encoding='utf-8')
