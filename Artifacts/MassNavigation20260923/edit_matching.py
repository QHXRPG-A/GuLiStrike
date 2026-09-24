from pathlib import Path
p=Path('Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp');s=p.read_text(encoding='utf-8-sig')
s=s.replace('#include "Containers/Queue.h"','#include "Containers/Queue.h"\n#include "Commander/Mass/Navigation/GuLiIncrementalAssignment.h"')
s=s.replace('bool bPathValid = false;\n\t};\n\n\tenum class EMovePlanningStage','bool bPathValid = false;\n\t\tbool bNeedsCandidate=false;\n\t\tint32 NextFallbackSlot=0;\n\t};\n\n\tenum class EMovePlanningStage',1)
s=s.replace('FGuLiMovePlanHandle Handle;','FGuLiIncrementalAssignment Matching;\n\t\tTArray<int32> MatchingMembers;\n\t\tbool bMatchingStarted=false;\n\t\tFGuLiMovePlanHandle Handle;',1)
s=s.replace('Job.RouteTasks.Reset();','Job.bMatchingStarted=false; Job.MatchingMembers.Reset();\n\t\tJob.RouteTasks.Reset();')
a=s.index('\t\t\tFFreeAssignmentPlan Assignment;'); b=s.index('\n\t\tif (Job.Stage == EMovePlanningStage::Route)',a)
s=s[:a]+'''\t\t\tif (!Job.bMatchingStarted)
\t\t\t{
\t\t\t\t// A single <=25 member cohort has one soft anchor. Its center-first free slots
\t\t\t\t// retain the former minimum travel objective without a whole-selection matrix.
\t\t\t\tJob.MatchingMembers.Reset();
\t\t\t\tfor (int32 I=0; I<Job.Members.Num(); ++I) if (Job.Members[I].bStartValid) Job.MatchingMembers.Add(I);
\t\t\t\tJob.MatchingMembers.Sort([&](int32 A,int32 B){return Job.Members[A].SoldierId<Job.Members[B].SoldierId;});
\t\t\t\tTArray<FVector> Rows,Columns;
\t\t\t\tfor (int32 I=0; I<FMath::Min(Job.LegalSlots.Num(),Job.MatchingMembers.Num()); ++I) Rows.Add(Job.LegalSlots[I].WorldDestination);
\t\t\t\tfor (int32 I : Job.MatchingMembers) Columns.Add(Job.Members[I].CommandStart.Location);
\t\t\t\tJob.Matching.Begin(MoveTemp(Rows),MoveTemp(Columns)); Job.bMatchingStarted=true;
\t\t\t}
\t\t\twhile (!Job.Matching.bComplete && AuthorityState->PlanningBudget.CanWork())
\t\t\t{ Job.Matching.Step(); Job.LastProgressAt=FPlatformTime::Seconds(); }
\t\t\tif (!Job.Matching.bComplete || !AuthorityState->PlanningBudget.CanWork()) continue;
\t\t\tFMoveRouteTask RouteTask; RouteTask.CohortId=Job.Cohorts[0].CohortId;
\t\t\tfor (int32 R=0; R<Job.Matching.ColumnByRow.Num(); ++R)
\t\t\t{
\t\t\t\tconst int32 M=Job.MatchingMembers[Job.Matching.ColumnByRow[R]]; const auto& Slot=Job.LegalSlots[R];
\t\t\t\tif (!IsMoveCandidateAvailableForMember(Job,Job.Members[M],Slot.CandidateIndex)) continue;
\t\t\t\tClaimMoveCandidate(Job,M,Slot,Job.ProjectedNavByCandidateIndex.FindChecked(Slot.CandidateIndex));
\t\t\t\tRouteTask.MemberPlanIndices.Add(M);
\t\t\t}
\t\t\tif (!RouteTask.MemberPlanIndices.IsEmpty()) Job.RouteTasks.Add(MoveTemp(RouteTask));
\t\t\tfor (auto& Member : Job.Members) if (Member.bStartValid && !Member.bHasDestination) Member.FailureStage=EGuLiMovePlanFailureStage::CandidatesExhausted;
\t\t\tJob.Stage=EMovePlanningStage::Route;
\t\t}
'''+s[b:]
s=s.replace('return !Member.bHasDestination;', 'return !Member.bHasDestination && !Task.bNeedsCandidate;',1)
needle='\t\t\t\tFVector StartCentroid = FVector::ZeroVector;'
s=s.replace(needle,'''\t\t\t\tif (Task.bNeedsCandidate)
\t\t\t\t{
\t\t\t\t\tconst int32 M=Task.MemberPlanIndices[0]; auto& Member=Job.Members[M];
\t\t\t\t\twhile (Task.NextFallbackSlot<Job.LegalSlots.Num() && AuthorityState->PlanningBudget.CanWork())
\t\t\t\t\t{
\t\t\t\t\t\tconst auto& Slot=Job.LegalSlots[Task.NextFallbackSlot++]; Job.LastProgressAt=FPlatformTime::Seconds();
\t\t\t\t\t\tif (Slot.CandidateIndex<=Member.LastCandidateIndex || !IsMoveCandidateAvailableForMember(Job,Member,Slot.CandidateIndex)
\t\t\t\t\t\t\t|| IsMoveCandidateBlockedByHardReservation(Job,Slot.WorldDestination)) continue;
\t\t\t\t\t\tClaimMoveCandidate(Job,M,Slot,Job.ProjectedNavByCandidateIndex.FindChecked(Slot.CandidateIndex));
\t\t\t\t\t\tTask.bNeedsCandidate=false; break;
\t\t\t\t\t}
\t\t\t\t\tif (Task.bNeedsCandidate)
\t\t\t\t\t{
\t\t\t\t\t\tif (Task.NextFallbackSlot<Job.LegalSlots.Num()) { Job.RouteTasks.Insert(MoveTemp(Task),0); break; }
\t\t\t\t\t\tif (RestartMovePlanningWithCompleteCandidatePool(Job)) break;
\t\t\t\t\t\tMember.FailureStage=EGuLiMovePlanFailureStage::CandidatesExhausted; continue;
\t\t\t\t\t}
\t\t\t\t}
'''+needle,1)
a=s.index('\t\t\t\t\tconst FFreeDestinationSlot* Fallback =',s.index('if (!bRouteValid)')); b=s.index('\n\t\t\t\t\tcontinue;',a)
s=s[:a]+'''\t\t\t\t\tTask.bNeedsCandidate=true; Task.bPathQueried=false; Task.NextConnector=0; Task.NextFallbackSlot=0; Task.CachedPath.Reset();
\t\t\t\t\tJob.RouteTasks.Add(MoveTemp(Task));'''+s[b:]
# Reconciliation only queues a personal fallback; its candidate scan resumes above.
a=s.index('\t\t\t\tconst FFreeDestinationSlot* Fallback =',s.index('EMovePlanningStage::ReconcileReservations)')); b=s.index('\t\t\t\tbQueuedRepair = true;',a)
s=s[:a]+'''\t\t\t\tJob.RejectedCandidatePairs.Add(MakeMoveCandidatePairKey(Member.SoldierId,ConflictingCandidateIndex));
\t\t\t\tFMoveRouteTask Retry; Retry.CohortId=Member.CohortId; Retry.MemberPlanIndices.Add(MemberPlanIndex); Retry.bNeedsCandidate=true;
\t\t\t\tJob.RouteTasks.Add(MoveTemp(Retry));
'''+s[b:]
# Hard-block failed members' live old reservations for fallback scans too.
s=s.replace('RestoredReservations.Add(Member.OldReservation);', '''RestoredReservations.Add(Member.OldReservation);
\t\t\t\t\tFMoveDestinationReservation Restored; Restored.OwnerSoldierId=Member.SoldierId.Value; Restored.Location=Member.OldReservation;
\t\t\t\t\tRestored.RadiusCentimeters=AuthorityState->Soldiers[*SoldierIndex].AvoidanceRadiusCentimeters; AddHardReservationToMoveJob(Job,Restored);''',1)
# Spatially direct connection: navigation raycast cannot slide around a wall.
a=s.index('\t\tFNavLocation Reached;',s.index('bool HasDirectSurfaceConnection')); b=s.index('\n\t}',a)
s=s[:a]+'''\t\tif (!Start.NodeRef || Start.Location.ContainsNaN() || Target.ContainsNaN()) return false;
\t\tFVector Hit;
\t\treturn !NavigationData.Raycast(Start.Location,Target,Hit,NavigationData.GetDefaultQueryFilter())
\t\t\t&& FVector::DistSquared2D(Hit,Target)<=FMath::Square(static_cast<double>(ToleranceCentimeters));'''+s[b:]
s=s.replace('OutImmediateAck = Existing->Ack;', 'OutImmediateAck = Existing->Stage==EMovePlanningStage::Completed ? Existing->Ack : Existing->AggregateAck;',1)
s=s.replace('Job.Ack.ServerSelectionRevision = Job.FullSelection.SelectionRevision;\n\t\tJob.Progress.bComplete', 'Job.UpdatedSelection.SelectionRevision=Job.FullSelection.SelectionRevision+(Job.bSelectionChanged ? 1u : 0u);\n\t\tif (!Job.UpdatedSelection.SelectionRevision) Job.UpdatedSelection.SelectionRevision=1;\n\t\tJob.Ack.ServerSelectionRevision = Job.UpdatedSelection.SelectionRevision;\n\t\tJob.Ack.Sanitize();\n\t\tJob.Progress.bComplete',1)
s=s.replace('if (!CachedProjection) { AuthorityState->PlanningBudget.RecordQuery(ProjectionStarted); Job.ProjectionCache.Add', 'if (!CachedProjection) { Job.ProjectionCache.Add',1)
s=s.replace('const double ProjectionStarted = FPlatformTime::Seconds();','FGuLiNavigationWorkBudget::FQueryScope ProjectionTimer(AuthorityState->PlanningBudget);',1)
s=s.replace('--RemainingProjectionBudget;\n\t\t\t\t++AuthorityState->MoveCandidateProjectionQueries;', '--RemainingProjectionBudget;\n\t\t\t\tFGuLiNavigationWorkBudget::FQueryScope StartTimer(AuthorityState->PlanningBudget);\n\t\t\t\t++AuthorityState->MoveCandidateProjectionQueries;',1)
p.write_text(s,encoding='utf-8')
