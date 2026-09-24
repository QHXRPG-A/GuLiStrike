from pathlib import Path
p=Path('Source/GuLiStrike/Commander/Mass/GuLiBattleAuthoritySubsystem.cpp'); s=p.read_text(encoding='utf-8-sig')
a=s.index('\n\tenum class ENavigationRepairMemberStage'); b=s.index('\n\t',s.index('\t\tbool bReadyToCommit = false;',a)+len('\t\tbool bReadyToCommit = false;'))
# Include the closing struct delimiter, but retain helpers following it.
b=s.index('\n\t};',s.index('struct FNavigationRepairJob',a))+len('\n\t};')
s=s[:a]+'''
\tstruct FNavigationRecoveryWork
\t{
\t\tFGuLiSoldierId Id;
\t\tuint64 TaskVersion=0;
\t\tuint32 Order=0, Nav=0, Epoch=0;
\t\tFNavLocation Start, Final;
\t\tFVector RequestedFinal=FVector::ZeroVector;
\t\tTArray<FVector> Path;
\t\tint32 Stage=0, Candidate=0;
\t\tdouble Started=0, Progress=0;
\t\tbool bReady=false, bValid=false, bHadFinal=false, bArrived=false;
\t};
'''+s[b:]
s=s.replace('TUniquePtr<GuLiCommanderMassPrivate::FNavigationRepairJob> NavigationRepairJob;', '''TMap<uint32,GuLiCommanderMassPrivate::FNavigationRecoveryWork> RecoveryWork;
\tTQueue<uint32> RecoveryQueue, ReadyRecoveryQueue;
\tuint32 RecoveryScanCursor=0;
\tuint64 RecoveryQueries=0, RecoveryFailures=0;''')
s=s.replace('AuthorityState->NavigationRepairJob.Reset();','AuthorityState->RecoveryWork.Reset(); AuthorityState->RecoveryQueue.Empty(); AuthorityState->ReadyRecoveryQueue.Empty(); AuthorityState->RecoveryScanCursor=0;')
# Off by default flow-field code no longer depends on the retired bulk-repair job.
a=s.index('\t\tif (AuthorityState->NavigationRepairJob',s.index('void UGuLiBattleAuthoritySubsystem::TickLocalFlowFields'))
b=s.index('\t\tif (Formation.bFlowBuildInFlight',a)
s=s[:a]+s[b:]
# Drop old per-formation global stop while repair was building. Members keep their current order until their own result is ready.
a=s.index('\t\tconst bool bFormationPendingNavigationRepair'); b=s.index('\t\tif (!NavigationSystem || !CommanderNavigationData)',a)
s=s[:a]+s[b:]
a=s.index('void UGuLiBattleAuthoritySubsystem::TickNavigationRepairs('); b=s.index('// 固定步：',a)
s=s[:a]+'''void UGuLiBattleAuthoritySubsystem::TickNavigationRepairs(int32& RemainingProjectionBudget,int32& RemainingPathBudget)
{
\tusing namespace GuLiCommanderMassPrivate;
\tif (!AuthorityState || AuthorityState->Soldiers.IsEmpty()) return;
\tFGuLiNavigationWorkBudget::FScope Scope(AuthorityState->PlanningBudget);
\tauto* World=GetWorld(); auto* State=World->GetGameState<AGuLiBattleGameState>();
\tauto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
\tauto* Data=Nav ? GetCommanderNavigationData(*Nav) : nullptr;
\tauto* Registry=World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
\tif (!State || !Data || !Registry) return;
\tconst auto Obstacles=Registry->GetSnapshot();
\t// Discovery is cursor based too. No whole-population scan/sort on each personal retry.
\tfor (int32 Visit=0; Visit<FMath::Min(128,AuthorityState->Soldiers.Num()) && AuthorityState->PlanningBudget.CanWork(); ++Visit)
\t{
\t\tauto& Soldier=AuthorityState->Soldiers[AuthorityState->RecoveryScanCursor++ % AuthorityState->Soldiers.Num()];
\t\tif (!Soldier.CanAct() || AuthorityState->RecoveryWork.Contains(Soldier.SoldierId.Value)) continue;
\t\tconst bool Stale=Soldier.FinalDestinationNavigationGeneration!=AuthorityState->NavigationGeneration;
\t\tconst bool Retry=Soldier.ActiveOrderId && Soldier.NavigationState==EGuLiSoldierNavigationState::PersonalPathRecovery
\t\t\t&& (Soldier.PersonalPathRetries==0 || (Soldier.PersonalPathRetries==1 && Soldier.NoProgressSeconds>=PersonalRecoveryRetrySeconds));
\t\tif (!Stale && !Retry) continue;
\t\tFNavigationRecoveryWork Work; Work.Id=Soldier.SoldierId; Work.Order=Soldier.ActiveOrderId;
\t\tWork.Nav=AuthorityState->NavigationGeneration; Work.Epoch=State->GetMatchEpoch(); Work.TaskVersion=Soldier.TaskGeneration;
\t\tWork.bHadFinal=Soldier.bHasFinalDestination; Work.bArrived=Soldier.NavigationState==EGuLiSoldierNavigationState::Arrived;
\t\tWork.RequestedFinal=Soldier.FinalDestination.Location; Work.Started=Work.Progress=FPlatformTime::Seconds();
\t\tAuthorityState->RecoveryWork.Add(Work.Id.Value,MoveTemp(Work)); AuthorityState->RecoveryQueue.Enqueue(Soldier.SoldierId.Value);
\t}
\tuint32 Id=0;
\tfor (int32 Visit=0; Visit<128 && AuthorityState->PlanningBudget.CanWork() && AuthorityState->RecoveryQueue.Dequeue(Id); ++Visit)
\t{
\t\tauto* W=AuthorityState->RecoveryWork.Find(Id); const auto* Index=AuthorityState->SoldierIndexById.Find(Id);
\t\tif (!W || !Index) { AuthorityState->RecoveryWork.Remove(Id); continue; }
\t\tconst auto& Soldier=AuthorityState->Soldiers[*Index];
\t\tif (!Soldier.CanAct() || W->TaskVersion!=Soldier.TaskGeneration || W->Order!=Soldier.ActiveOrderId
\t\t\t|| W->Epoch!=State->GetMatchEpoch() || W->Nav!=AuthorityState->NavigationGeneration)
\t\t{ AuthorityState->RecoveryWork.Remove(Id); continue; }
\t\tconst double Now=FPlatformTime::Seconds();
\t\tif (Now-W->Started>=30. || Now-W->Progress>=5.) { W->bReady=true; W->bValid=false; }
\t\tif (!W->bReady && W->Stage<2 && AuthorityState->PlanningBudget.TakeProjection())
\t\t{
\t\t\tFGuLiNavigationWorkBudget::FQueryScope Query(AuthorityState->PlanningBudget); W->Progress=Now;
\t\t\tif (W->Stage==0)
\t\t\t{
\t\t\t\tW->bValid=ProjectPointToCommanderNavigation(*Nav,*Data,Soldier.Location,FVector(10,10,5000),W->Start)
\t\t\t\t\t&& FVector::DistSquared2D(Soldier.Location,W->Start.Location)<=100.;
\t\t\t\tW->bReady=!W->bValid || !W->bHadFinal; W->Stage=1;
\t\t\t}
\t\t\telse
\t\t\t{
\t\t\t\tFVector Target=W->RequestedFinal;
\t\t\t\tif (W->Candidate) { const int32 Ring=W->Candidate-1; const double Angle=UE_TWO_PI*(Ring%8)/8.;
\t\t\t\t\tTarget+=FVector(FMath::Cos(Angle),FMath::Sin(Angle),0)*50.f*(Ring/8+1); }
\t\t\t\tFMovePlanningJob Reservation; Reservation.Reservations=&AuthorityState->MoveReservations; Reservation.Team=Soldier.Team;
\t\t\t\tReservation.MaximumMemberRadiusCentimeters=Soldier.AvoidanceRadiusCentimeters; Reservation.ReleasedReservationIds.Add(Id);
\t\t\t\tW->bValid=ProjectPointToCommanderNavigation(*Nav,*Data,Target,FVector(10,10,5000),W->Final)
\t\t\t\t\t&& FVector::DistSquared2D(Target,W->Final.Location)<=100.
\t\t\t\t\t&& !IsMoveCandidateBlockedByHardReservation(Reservation,W->Final.Location)
\t\t\t\t\t&& Obstacles->IsSegmentClear(W->Final.Location,W->Final.Location,Soldier.AvoidanceRadiusCentimeters);
\t\t\t\tif (W->bValid) { W->Stage=2; W->bReady=!W->Order; }
\t\t\t\telse if (++W->Candidate>=33) W->bReady=true;
\t\t\t}
\t\t}
\t\telse if (!W->bReady && W->Stage==2 && AuthorityState->PlanningBudget.TakePath())
\t\t{
\t\t\tFGuLiNavigationWorkBudget::FQueryScope Query(AuthorityState->PlanningBudget); W->Progress=Now;
\t\t\t++AuthorityState->PathQueries; ++AuthorityState->PersonalPathQueries; ++AuthorityState->RecoveryQueries;
\t\t\tW->bValid=BuildCompletePathQuiet(*Nav,*Data,W->Start,W->Final.Location,W->Path);
\t\t\tif (W->bValid || ++W->Candidate>=33) W->bReady=true; else W->Stage=1;
\t\t}
\t\tif (W->bReady) AuthorityState->ReadyRecoveryQueue.Enqueue(Id); else AuthorityState->RecoveryQueue.Enqueue(Id);
\t}
}

void UGuLiBattleAuthoritySubsystem::CommitReadyNavigationRepairs()
{
\tusing namespace GuLiCommanderMassPrivate;
\tif (!AuthorityState || !AuthorityState->MassEntitySubsystem.IsValid()) return;
\tFGuLiNavigationWorkBudget::FScope Scope(AuthorityState->CommitBudget);
\tauto* World=GetWorld(); const auto* State=World->GetGameState<AGuLiBattleGameState>(); if (!State) return;
\tauto& Manager=AuthorityState->MassEntitySubsystem->GetMutableEntityManager(); uint32 Id=0;
\twhile (AuthorityState->CommitBudget.CanWork() && AuthorityState->CommitBudget.Members>0 && AuthorityState->ReadyRecoveryQueue.Dequeue(Id))
\t{
\t\tauto* W=AuthorityState->RecoveryWork.Find(Id); const auto* Index=AuthorityState->SoldierIndexById.Find(Id);
\t\tif (!W || !Index) { AuthorityState->RecoveryWork.Remove(Id); continue; }
\t\tauto& Soldier=AuthorityState->Soldiers[*Index];
\t\tif (!Soldier.CanAct() || !Manager.IsEntityValid(Soldier.Entity) || W->TaskVersion!=Soldier.TaskGeneration || W->Order!=Soldier.ActiveOrderId
\t\t\t|| W->Nav!=AuthorityState->NavigationGeneration || W->Epoch!=State->GetMatchEpoch())
\t\t{ AuthorityState->RecoveryWork.Remove(Id); continue; }
\t\tif (W->bValid && W->Order && Soldier.LastValidNavLocation.NodeRef!=W->Start.NodeRef)
\t\t{ W->Stage=0; W->bReady=false; AuthorityState->RecoveryQueue.Enqueue(Id); continue; }
\t\t--AuthorityState->CommitBudget.Members;
\t\tif (W->bValid && W->bHadFinal)
\t\t{
\t\t\tFMovePlanningJob Reservation; Reservation.Reservations=&AuthorityState->MoveReservations; Reservation.Team=Soldier.Team;
\t\t\tReservation.MaximumMemberRadiusCentimeters=Soldier.AvoidanceRadiusCentimeters; Reservation.ReleasedReservationIds.Add(Id);
\t\t\tif (IsMoveCandidateBlockedByHardReservation(Reservation,W->Final.Location))
\t\t\t{ W->Stage=1; W->bReady=false; AuthorityState->RecoveryQueue.Enqueue(Id); continue; }
\t\t}
\t\tSoldier.FinalDestinationNavigationGeneration=AuthorityState->NavigationGeneration;
\t\tif (W->bValid)
\t\t{
\t\t\t// A changed endpoint is committed together with its new personal path. CommandStart stays static.
\t\t\tif (W->bHadFinal) Soldier.FinalDestination=W->Final;
\t\t\tSoldier.LastValidNavLocation=FNavLocation(Soldier.Location,W->Start.NodeRef);
\t\t\tif (W->Order)
\t\t\t{
\t\t\t\tSoldier.PersonalPathPoints=MoveTemp(W->Path); Soldier.PersonalPathPointIndex=Soldier.PersonalPathPoints.Num()>1 ? 1 : 0;
\t\t\t\tSoldier.NavigationState=EGuLiSoldierNavigationState::PersonalPathRecovery; ++Soldier.PersonalPathRetries;
\t\t\t\tSoldier.NoProgressSeconds=0; Soldier.BestWaypointDistanceCentimeters=TNumericLimits<float>::Max();
\t\t\t\tSoldier.ConsecutiveSurfaceFailures=0; Soldier.TotalSurfaceFailures=0; Soldier.bForceMovementUpdate=true;
\t\t\t\tfor (auto& Formation : AuthorityState->OrderFormations) if (Formation.BatchOrderId==W->Order && Formation.FinalDestinationBySoldierId.Contains(Id))
\t\t\t\t{ Formation.FinalDestinationBySoldierId.Add(Id,W->Final); break; }
\t\t\t}
\t\t}
\t\telse
\t\t{
\t\t\t++AuthorityState->RecoveryFailures;
\t\t\tSoldier.LastFailedOrderId=Soldier.ActiveOrderId; Soldier.ActiveOrderId=0; Soldier.Velocity=FVector::ZeroVector;
\t\t\tSoldier.Location=Soldier.LastValidNavLocation.Location; Soldier.NavigationState=EGuLiSoldierNavigationState::Blocked;
\t\t\tSoldier.NavigationFailure=EGuLiSoldierNavigationFailure::PersonalPathFailed;
\t\t\tSoldier.FailureSimulationSeconds=AuthorityState->SimulationSeconds;
\t\t}
\t\t++Soldier.StateRevision;
\t\tauto& Order=Manager.GetFragmentDataChecked<FGuLiMassOrderFragment>(Soldier.Entity);
\t\tOrder.ActiveOrderId=Soldier.ActiveOrderId; Order.OrderRevision=Soldier.StateRevision; Order.bHasMoveTarget=Soldier.ActiveOrderId!=0;
\t\tOrder.FormationTarget=Soldier.FinalDestination.Location;
\t\tauto& Move=Manager.GetFragmentDataChecked<FMassMoveTargetFragment>(Soldier.Entity);
\t\tMove.CreateNewAction(Soldier.ActiveOrderId ? EMassMovementAction::Move : EMassMovementAction::Stand,*World);
\t\tMove.Center=Soldier.ActiveOrderId ? Soldier.FinalDestination.Location : Soldier.Location;
\t\tMove.DesiredSpeed=FMassInt16Real(Soldier.ActiveOrderId ? MovementSpeedCentimetersPerSecond : 0.f);
\t\tRefreshSoldierNavigationState(Soldier.SoldierId); AuthorityState->RecoveryWork.Remove(Id);
\t}
}
'''+s[b:]
# Keep terminal no-progress logic in the simulation; all queries now execute above once per world frame.
a=s.index('\tTArray<int32> PersonalPathQueryCandidates;'); b=s.index('\n\tfor (int32 SoldierIndex = 0;',a+10)
s=s[:a]+s[b:]
a=s.index('\t\tconst bool bNeedsInitialQuery ='); b=s.index('\n\tfor (int32 SoldierIndex = 0;',a)
s=s[:a]+'\t}\n'+s[b:]
# Do not declare starvation-induced failure while queued planning is actually progressing.
s=s.replace('if (GuLiCommanderNavigationPolicy::ShouldBlockPersonalPathRecovery(', 'if (!AuthorityState->RecoveryWork.Contains(Soldier.SoldierId.Value) && GuLiCommanderNavigationPolicy::ShouldBlockPersonalPathRecovery(',1)
s=s.replace('\tCommitReadyMovePlans();\n\tCommitReadyNavigationRepairs();', '''\tif (AuthorityState->ServerSimTick % GuLiCommanderNavigationPolicy::MovementUpdateIntervalTicks == 0)
\t{
\t\tCommitReadyMovePlans(); CommitReadyNavigationRepairs();
\t}''',1)
# Add helper namespace required by steering validation.
s=s.replace('void UGuLiBattleAuthoritySubsystem::TickSteeringValidation()\n{', 'void UGuLiBattleAuthoritySubsystem::TickSteeringValidation()\n{\n\tusing namespace GuLiCommanderMassPrivate;')
# Initial snapshots should not enqueue 10,000 unnecessary repair queries.
s=s.replace('Soldier.LastValidNavLocation = Projected;', 'Soldier.LastValidNavLocation = Projected;\n\tSoldier.FinalDestinationNavigationGeneration=AuthorityState->NavigationGeneration;')
# Time/counter bounded optional flow-field queries too, even if enabled later.
s=s.replace('int32 RemainingSampleBudget = FMath::Max(1, FlowFieldWalkabilitySamplesPerTick);', 'FGuLiNavigationWorkBudget::FScope FlowScope(AuthorityState->PlanningBudget);\n\tint32 RemainingSampleBudget = FMath::Min(AuthorityState->PlanningBudget.Projections,FMath::Max(1, FlowFieldWalkabilitySamplesPerTick));')
needle='\t\t\tif (ProjectPointToCommanderNavigation('
idx=s.index(needle,s.index('void UGuLiBattleAuthoritySubsystem::TickLocalFlowFields'))
s=s[:idx]+'\t\t\tif (!AuthorityState->PlanningBudget.TakeProjection()) break;\n\t\t\tFGuLiNavigationWorkBudget::FQueryScope Query(AuthorityState->PlanningBudget);\n'+s[idx:]
# Width probing is a bounded work unit; never issue its 5..1 queries outside the shared budget.
s=s.replace('const int32 ColumnCount)\n\t{\n\t\tconst FRotator FacingRotation', 'const int32 ColumnCount, FGuLiNavigationWorkBudget& Budget)\n\t{\n\t\tconst FRotator FacingRotation',1)
s=s.replace('FNavLocation ProjectedPoint;\n\t\t\tconst bool bProbeWalkable', 'FNavLocation ProjectedPoint;\n\t\t\tif (!Budget.TakeProjection()) return false;\n\t\t\tFGuLiNavigationWorkBudget::FQueryScope Query(Budget);\n\t\t\tconst bool bProbeWalkable',1)
s=s.replace('const float Spacing)\n\t{\n\t\tif (!NavigationSystem', 'const float Spacing, FGuLiNavigationWorkBudget& Budget, const int32 PreviousColumns)\n\t{\n\t\tFGuLiNavigationWorkBudget::FScope Scope(Budget);\n\t\tif (Budget.Projections<15 || !Budget.CanWork()) return PreviousColumns;\n\t\tif (!NavigationSystem',1)
s=s.replace('Spacing,\n\t\t\t\tColumnCount))', 'Spacing,\n\t\t\t\tColumnCount,Budget))',1)
s=s.replace('Formation.TravelFacingYawDegrees,\n\t\t\t\tFormation.MemberSpacingCentimeters);', 'Formation.TravelFacingYawDegrees,\n\t\t\t\tFormation.MemberSpacingCentimeters,AuthorityState->PlanningBudget,Formation.TransitColumnCount);',1)
p.write_text(s,encoding='utf-8')
