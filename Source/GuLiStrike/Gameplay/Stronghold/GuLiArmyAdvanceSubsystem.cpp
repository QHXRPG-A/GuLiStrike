#include "Gameplay/Stronghold/GuLiArmyAdvanceSubsystem.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Engine/World.h"
#include "NavigationSystem.h"

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
	Super::Deinitialize();
}
void UGuLiArmyAdvanceSubsystem::OnNavigationGenerated(ANavigationData* Data)
{
	for (auto& Group : Groups) { Group.Unreachable.Reset(); Group.NextDecisionTime = 0; }
}
void UGuLiArmyAdvanceSubsystem::RegisterBatch(EGuLiTeam Team, TConstArrayView<FGuLiSoldierId> Soldiers, int32 SourceTerritory)
{
	check(GetWorld()->GetNetMode() != NM_Client);
	auto& Authority = *GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	Authority.RegisterAutomaticAdvance(Soldiers);
	for (int32 Start = 0; Start < Soldiers.Num(); Start += 25)
	{
		auto& Group = Groups.AddDefaulted_GetRef();
		Group.Team = Team; Group.Source = SourceTerritory;
		Group.Soldiers.Append(Soldiers.GetData() + Start, FMath::Min(25, Soldiers.Num() - Start));
	}
}
void UGuLiArmyAdvanceSubsystem::Tick(float Dt)
{
	auto& Resources = *GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
	if (Groups.IsEmpty() || !Resources.IsResourceWorldActive() || !Resources.IsRuntimeReady()) return;
	auto& Authority = *GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>();
	const auto& WorldState = *Resources.GetResourceWorldState();
	TArray<EGuLiTeam> Owners;
	for (const auto& State : WorldState.GetTerritories()) Owners.Add(State.Owner);
	if (Owners != ObservedOwners)
	{
		ObservedOwners = MoveTemp(Owners);
		for (auto& Group : Groups) { Group.Unreachable.Reset(); Group.NextDecisionTime = 0; }
	}
	for (auto& Group : Groups)
		Group.Soldiers.RemoveAll([&](auto Id) { return !Authority.IsAutomaticallyAdvancing(Id); });
	Groups.RemoveAll([](const auto& Group) { return Group.Soldiers.IsEmpty(); });
	const double Now = GetWorld()->GetTimeSeconds();
	int32 Queries = 0;
	for (int32 Visit = 0; Visit < Groups.Num() && Queries < 4; ++Visit)
	{
		auto& Group = Groups[DecisionCursor++ % Groups.Num()];
		if (Now < Group.NextDecisionTime) continue;
		Group.NextDecisionTime = Now + .5;
		FVector Center = FVector::ZeroVector;
		bool bMoving = false, bFailed = false, bLocked = false, bAtTarget = false;
		for (auto Id : Group.Soldiers)
		{
			FGuLiSoldierNavigationDebug Navigation;
			Authority.TryGetSoldierNavigationDebug(Id, Navigation);
			Center += Navigation.Location;
			bAtTarget |= Group.Target != INDEX_NONE && Resources.FindTerritoryIndex(Navigation.Location) == Group.Target;
			bMoving |= Navigation.ActiveOrderId != 0;
			bFailed |= Navigation.State == EGuLiSoldierNavigationState::Blocked;
			bLocked |= Authority.IsSoldierExternallyLocked(Id);
		}
		Center /= Group.Soldiers.Num();
		if (bLocked) continue;
		if (Group.Target != INDEX_NONE && WorldState.GetTerritoryOwner(Group.Target) != Group.Team)
		{
			if (bAtTarget || (!bFailed && bMoving)) continue;
			Group.Unreachable.Add(Group.Target);
		}
		const int32 Current = Resources.FindTerritoryIndex(Center);
		const int32 Origin = Current != INDEX_NONE ? Current : Group.Source;
		const TArray<int32> Candidates = Resources.GetStrongholdTopology().FindAttackCandidates(Origin, Group.Team,
			[&](int32 Index) { return WorldState.GetTerritoryOwner(Index); });
		Group.Target = INDEX_NONE;
		for (int32 Candidate : Candidates) if (!Group.Unreachable.Contains(Candidate)) { Group.Target = Candidate; break; }
		if (Group.Target == INDEX_NONE) { if (bMoving) Authority.StopAutomaticMove(Group.Soldiers); continue; }
		const FVector Ground = Resources.GetTerritoryGroundLocation(Group.Target);
		const FVector Approach = Ground + (Center - Ground).GetSafeNormal2D() * 6000;
		++Queries;
		if (!Authority.IssueAttackMove(Group.Team, Group.Soldiers, Approach))
		{
			Group.Unreachable.Add(Group.Target); Group.Target = INDEX_NONE; Group.NextDecisionTime = 0;
			if (bMoving) Authority.StopAutomaticMove(Group.Soldiers);
		}
	}
}
