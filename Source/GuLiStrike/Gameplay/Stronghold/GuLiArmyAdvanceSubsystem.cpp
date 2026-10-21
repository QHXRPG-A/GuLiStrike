#include "Gameplay/Stronghold/GuLiArmyAdvanceSubsystem.h"
#include "Commander/Orders/GuLiUnitTaskSubsystem.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Gameplay/Performance/GuLiHotspotOptions.h"

void RegisterGuLiHybridAdvanceState(UGuLiArmyAdvanceSubsystem& Self);
namespace
{
double AdvanceTime(const UWorld& World)
{
    if (UGuLiNetworkSettings::IsHybrid(&World))
        if (const auto* H=World.GetSubsystem<UGuLiHybridWorldSubsystem>()) return H->GetLogicalSeconds();
    return World.GetTimeSeconds();
}
}
void UGuLiArmyAdvanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (UGuLiNetworkSettings::IsHybrid(GetWorld()))
    {
        Collection.InitializeDependency<UGuLiHybridWorldSubsystem>();
        RegisterGuLiHybridAdvanceState(*this);
    }
}

bool UGuLiArmyAdvanceSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const auto* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode() != NM_Client;
}
TStatId UGuLiArmyAdvanceSubsystem::GetStatId() const { RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiArmyAdvanceSubsystem, STATGROUP_Tickables); }
void UGuLiArmyAdvanceSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	if (auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(&InWorld))
		Nav->OnNavigationGenerationFinishedDelegate.AddDynamic(this,&UGuLiArmyAdvanceSubsystem::OnNavigationGenerated);
}
void UGuLiArmyAdvanceSubsystem::Deinitialize()
{
	if (auto* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
		Nav->OnNavigationGenerationFinishedDelegate.RemoveDynamic(this,&UGuLiArmyAdvanceSubsystem::OnNavigationGenerated);
	GroupIndex.Reset();bGroupIndexDirty=true;
	Super::Deinitialize();
}
void UGuLiArmyAdvanceSubsystem::OnNavigationGenerated(ANavigationData* Data)
{
	for (auto& Group : Groups) { Group.Unreachable.Reset(); Group.NextDecisionTime = 0; }
}
void UGuLiArmyAdvanceSubsystem::RegisterBatch(EGuLiTeam Team, TConstArrayView<FGuLiSoldierId> Soldiers, int32 SourceTerritory)
{
	check(GetWorld()->GetNetMode() != NM_Client);
	GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>()->RegisterSoldiers(Team, Soldiers, SourceTerritory);
}
void UGuLiArmyAdvanceSubsystem::ActivateTaskMember(EGuLiTeam Team, FGuLiSoldierId Soldier, int32 SourceTerritory)
{
	if (!GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>()->MayAdvance(Soldier)) return;
	auto& Authority = *GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	Authority.RegisterAutomaticAdvance(MakeArrayView(&Soldier, 1));
	for (auto& Group : Groups)
	{
		if (Group.Soldiers.Contains(Soldier)) return;
		if (Group.Team == Team && Group.Source == SourceTerritory && Group.Target == INDEX_NONE && Group.NextDecisionTime == 0 && Group.Soldiers.Num() < 25)
		{ Group.Soldiers.Add(Soldier); bGroupIndexDirty=true; return; }
	}
	auto& Group = Groups.AddDefaulted_GetRef(); Group.Team = Team; Group.Source = SourceTerritory; Group.Soldiers.Add(Soldier);
	bGroupIndexDirty=true;
}

UGuLiArmyAdvanceSubsystem::FAdvanceGroup* UGuLiArmyAdvanceSubsystem::FindGroup(FGuLiSoldierId Soldier)
{ return const_cast<FAdvanceGroup*>(static_cast<const UGuLiArmyAdvanceSubsystem*>(this)->FindGroup(Soldier)); }
const UGuLiArmyAdvanceSubsystem::FAdvanceGroup* UGuLiArmyAdvanceSubsystem::FindGroup(FGuLiSoldierId Soldier) const
{
    if (bGroupIndexDirty)
    {
        GroupIndex.Reset();
        for (int32 Index=0;Index<Groups.Num();++Index)
            for (const auto Id:Groups[Index].Soldiers)
                if (!GroupIndex.Contains(Id)) GroupIndex.Add(Id,Index); // Preserve first-match semantics.
        bGroupIndexDirty=false;
    }
    const auto* Index=GroupIndex.Find(Soldier);
    return Index?&Groups[*Index]:nullptr;
}
EGuLiCommanderWorkPhase UGuLiArmyAdvanceSubsystem::GetBehaviorPhase(FGuLiSoldierId Soldier) const
{ const auto* Group = FindGroup(Soldier); return Group ? Group->Phase : EGuLiCommanderWorkPhase::Any; }
EGuLiCommanderWorkResult UGuLiArmyAdvanceSubsystem::GetBehaviorResult(FGuLiSoldierId Soldier) const
{
    const auto* Group = FindGroup(Soldier);
    const auto* Resources = GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
    if (!Group || !Resources || !Resources->IsResourceWorldActive() || !Resources->IsRuntimeReady()) return EGuLiCommanderWorkResult::Running;
    return Group->bLocked ? EGuLiCommanderWorkResult::Running : Group->Result;
}

void UGuLiArmyAdvanceSubsystem::ObserveGroup(FAdvanceGroup& Group)
{
    auto& Resources = *GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
    auto& Authority = *GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
    if (Group.PendingPlan)
    {
        FGuLiMovePlanProgress Progress;
        const bool bFound = Authority.ConsumeMovePlanProgress({Group.PendingPlan,Group.PendingPlanEpoch},Progress);
        Group.bPlanCommitted |= !Progress.Committed.IsEmpty();
        if (bFound && !Progress.bComplete) { Group.Result=EGuLiCommanderWorkResult::Running; return; }
        Group.PendingPlan=0;
        if (!Group.bPlanCommitted) { Group.Result=EGuLiCommanderWorkResult::Failed; return; }
    }
    Group.Center = FVector::ZeroVector; Group.bMoving = Group.bLocked = false;
    bool bAtTarget = false, bFailed = false;
    for (auto Id : Group.Soldiers)
    {
        FGuLiSoldierNavigationDebug Nav;
        Authority.TryGetSoldierNavigationDebug(Id, Nav);
        Group.Center += Nav.Location;
        bAtTarget |= Group.Target != INDEX_NONE && Resources.FindTerritoryIndex(Nav.Location) == Group.Target;
        Group.bMoving |= Nav.ActiveOrderId != 0;
        bFailed |= Nav.State == EGuLiSoldierNavigationState::Blocked;
        Group.bLocked |= Authority.IsSoldierExternallyLocked(Id);
    }
    Group.Center /= FMath::Max(1, Group.Soldiers.Num());
    using Phase = EGuLiCommanderWorkPhase;
    using Result = EGuLiCommanderWorkResult;
    if (Group.Phase == Phase::AdvanceMoving || Group.Phase == Phase::AdvanceCapturing)
    {
        const auto& WorldState = *Resources.GetResourceWorldState();
        Group.Result = Group.Target != INDEX_NONE && WorldState.GetTerritoryOwner(Group.Target) == Group.Team ? Result::Complete
            : bAtTarget ? Result::Arrived : (!bFailed && Group.bMoving) ? Result::Running : Result::Failed;
    }
    else if (Group.Phase == Phase::AdvanceWaiting) Group.Result = Result::NoTarget;
}

void UGuLiArmyAdvanceSubsystem::Tick(float)
{
    if (UGuLiNetworkSettings::IsHybrid(GetWorld()))
    {
        auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        if (!H || !H->CanPublishFacts() || LastHybridTick==H->GetLogicalTick()) return;
        LastHybridTick=H->GetLogicalTick();
    }
    RemainingMoveQueries = 4; // Admission throttle only; all navigation uses the authority world budget.
    auto& Resources = *GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
    if (Groups.IsEmpty() || !Resources.IsResourceWorldActive() || !Resources.IsRuntimeReady()) return;
    auto& Authority = *GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
    auto& Tasks = *GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>();
    const auto& WorldState = *Resources.GetResourceWorldState();
    TArray<EGuLiTeam> Owners;
    for (const auto& State : WorldState.GetTerritories()) Owners.Add(State.Owner);
    if (Owners != ObservedOwners)
    {
        ObservedOwners = MoveTemp(Owners);
        for (auto& Group : Groups) { Group.Unreachable.Reset(); Group.NextDecisionTime = 0; }
    }
    for (auto& Group : Groups)
        bGroupIndexDirty |= Group.Soldiers.RemoveAll([&](auto Id) { return !Authority.IsAutomaticallyAdvancing(Id) || !Tasks.MayAdvance(Id); })>0;
    bGroupIndexDirty |= Groups.RemoveAll([](const auto& Group) { return Group.Soldiers.IsEmpty(); })>0;
    const double Now = AdvanceTime(*GetWorld());
    // Observe and pace shared groups only. Target choice, orders, stage completion and retries are StateTree operations.
    for (int32 Visit = 0; Visit < Groups.Num(); ++Visit)
    {
        auto& Group = Groups[DecisionCursor++ % Groups.Num()];
        if (Now < Group.NextDecisionTime) continue;
        Group.NextDecisionTime = Now + .5;
        ObserveGroup(Group);
    }
}

void UGuLiArmyAdvanceSubsystem::SelectBehaviorTarget(FGuLiSoldierId Soldier)
{
    auto* Group = FindGroup(Soldier);
    if (!Group || Group->bLocked || (Group->Phase != EGuLiCommanderWorkPhase::AdvanceSelecting && Group->Phase != EGuLiCommanderWorkPhase::AdvanceWaiting)
        || Group->Result == EGuLiCommanderWorkResult::TargetReady) return;
    auto& Resources = *GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
    if (!Resources.IsRuntimeReady() || !Resources.IsResourceWorldActive()) return;
    auto& Tasks = *GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>();
    bGroupIndexDirty |= Group->Soldiers.RemoveAll([&](auto Id) { return !Tasks.MayAdvance(Id); })>0;
    if (Group->Soldiers.IsEmpty()) return;
    ObserveGroup(*Group);
    if (Group->bLocked) return;
    const auto& WorldState = *Resources.GetResourceWorldState();
    const int32 Current = Resources.FindTerritoryIndex(Group->Center);
    const int32 Origin = Current != INDEX_NONE ? Current : Group->Source;
    const TArray<int32> Candidates = Resources.GetStrongholdTopology().FindAttackCandidates(Origin, Group->Team,
        [&](int32 Index) { return WorldState.GetTerritoryOwner(Index); });
    Group->Target = INDEX_NONE;
    for (int32 Candidate : Candidates) if (!Group->Unreachable.Contains(Candidate)) { Group->Target = Candidate; break; }
    Group->Phase = EGuLiCommanderWorkPhase::AdvanceSelecting;
    if (Group->Target == INDEX_NONE)
    {
        for (auto Id : Group->Soldiers) { Tasks.UpdateAdvanceTarget(Id, INDEX_NONE, FVector::ZeroVector); Tasks.NotifyAdvanceStage(Id); }
        Group->Result = EGuLiCommanderWorkResult::NoTarget;
        return;
    }
    const FVector Ground = Resources.GetTerritoryGroundLocation(Group->Target);
    Group->Approach = Ground + (Group->Center - Ground).GetSafeNormal2D() * 1200;
    for (auto Id : Group->Soldiers) Tasks.UpdateAdvanceTarget(Id, Group->Target, Group->Approach);
    Group->Result = EGuLiCommanderWorkResult::TargetReady;
}

void UGuLiArmyAdvanceSubsystem::MoveToBehaviorTarget(FGuLiSoldierId Soldier)
{
    auto* Group = FindGroup(Soldier);
    if (!Group || Group->bLocked || RemainingMoveQueries <= 0 || Group->Phase != EGuLiCommanderWorkPhase::AdvanceSelecting
        || Group->Result != EGuLiCommanderWorkResult::TargetReady) return;
    auto& Authority = *GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
    auto& Tasks = *GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>();
    bGroupIndexDirty |= Group->Soldiers.RemoveAll([&](auto Id) { return !Tasks.MayAdvance(Id); })>0;
    if (Group->Soldiers.IsEmpty()) return;
    --RemainingMoveQueries;
    const auto Handle = Authority.BeginAutomaticMovePlanning(Group->Team, Group->Soldiers, Group->Approach);
    if (!Handle.IsValid()) return; // Busy team: retry the same target, never report a false path failure.
    Group->PendingPlan=Handle.Value; Group->PendingPlanEpoch=Handle.Epoch; Group->bPlanCommitted=false;
    Group->Phase = EGuLiCommanderWorkPhase::AdvanceMoving;
    Group->Result = EGuLiCommanderWorkResult::Running;
    Group->NextDecisionTime = AdvanceTime(*GetWorld()) + .5;
}

void UGuLiArmyAdvanceSubsystem::WaitForBehaviorCapture(FGuLiSoldierId Soldier)
{
    auto* Group = FindGroup(Soldier);
    if (!Group || Group->Phase != EGuLiCommanderWorkPhase::AdvanceMoving || Group->Result != EGuLiCommanderWorkResult::Arrived) return;
    Group->Phase = EGuLiCommanderWorkPhase::AdvanceCapturing;
    Group->Result = EGuLiCommanderWorkResult::Running;
}

void UGuLiArmyAdvanceSubsystem::CompleteBehaviorStage(FGuLiSoldierId Soldier)
{
    auto* Group = FindGroup(Soldier);
    if (!Group || Group->Result != EGuLiCommanderWorkResult::Complete) return;
    auto& Tasks = *GetWorld()->GetSubsystem<UGuLiUnitTaskSubsystem>();
    for (auto Id : Group->Soldiers) Tasks.NotifyAdvanceStage(Id);
    bGroupIndexDirty |= Group->Soldiers.RemoveAll([&](auto Id) { return !Tasks.MayAdvance(Id); })>0;
    Group->Phase = EGuLiCommanderWorkPhase::AdvanceSelecting;
    Group->Result = EGuLiCommanderWorkResult::None; Group->NextDecisionTime = 0;
}

void UGuLiArmyAdvanceSubsystem::RejectBehaviorTarget(FGuLiSoldierId Soldier)
{
    auto* Group = FindGroup(Soldier);
    if (!Group || Group->Result != EGuLiCommanderWorkResult::Failed) return;
    if (Group->Target != INDEX_NONE) Group->Unreachable.Add(Group->Target);
    Group->Target = INDEX_NONE; Group->NextDecisionTime = 0;
    Group->Phase = EGuLiCommanderWorkPhase::AdvanceSelecting; Group->Result = EGuLiCommanderWorkResult::None;
    if (Group->bMoving) GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->StopAutomaticMove(Group->Soldiers);
}

void UGuLiArmyAdvanceSubsystem::WaitForBehaviorTarget(FGuLiSoldierId Soldier)
{
    auto* Group = FindGroup(Soldier);
    if (!Group || Group->Phase != EGuLiCommanderWorkPhase::AdvanceSelecting || Group->Result != EGuLiCommanderWorkResult::NoTarget) return;
    if (Group->bMoving) GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()->StopAutomaticMove(Group->Soldiers);
    Group->Phase = EGuLiCommanderWorkPhase::AdvanceWaiting;
    Group->Result = EGuLiCommanderWorkResult::Running;
    Group->NextDecisionTime = AdvanceTime(*GetWorld()) + .5;
}
