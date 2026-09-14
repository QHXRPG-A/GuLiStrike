#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceWorldState.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Resources/GuLiResourceActors.h"
#include "Gameplay/Stronghold/GuLiStrongholdCaptureComponent.h"
#include "Gameplay/Stronghold/GuLiStrongholdGateComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Economy/GuLiTeamEconomySubsystem.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

namespace
{
	TAutoConsoleVariable<float> CVarStrongholdSample(TEXT("guli.stronghold.SampleSeconds"), .25f, TEXT("Authority ground dominance sample interval."));
	TAutoConsoleVariable<float> CVarStrongholdCapture(TEXT("guli.stronghold.CaptureSeconds"), 20.f, TEXT("Neutral to endpoint seconds; full reversal takes twice this."));
	TAutoConsoleVariable<float> CVarStrongholdUpkeep(TEXT("guli.stronghold.UpkeepSeconds"), 30.f, TEXT("Periodic upkeep, unchanged by ownership transfers."));
	TAutoConsoleVariable<int32> CVarStrongholdBlueCost(TEXT("guli.stronghold.UpkeepBlue"), 1, TEXT("Blue cost per owned Territory per upkeep cycle."));
	TAutoConsoleVariable<int32> CVarStrongholdRedCost(TEXT("guli.stronghold.UpkeepRed"), 1, TEXT("Red cost per owned Territory per upkeep cycle."));
}

FVector UGuLiResourceWorldSubsystem::GetTerritoryGroundLocation(int32 Index) const
{
	return WorldState->GetTerritories()[Index].GroundLocation;
}
UGuLiStrongholdGateComponent* UGuLiResourceWorldSubsystem::GetStrongholdGate(int32 Index) const
{
	check(GetWorld()->GetNetMode() != NM_Client && Outposts.IsValidIndex(Index));
	return Outposts[Index]->FindComponentByClass<UGuLiStrongholdGateComponent>();
}
FVector UGuLiResourceWorldSubsystem::GetInitialBaseExit(EGuLiTeam Team) const
{
	check(GuLiResources::IsPlayableTeam(Team));
	return ProjectAnchorToGround(Team == EGuLiTeam::Red ? MapDefinition->SpawnAnchors.RedAssembly : MapDefinition->SpawnAnchors.BlueAssembly);
}
bool UGuLiResourceWorldSubsystem::IsTerritorySupplied(int32 Index) const
{
	return Index == INDEX_NONE || (WorldState && WorldState->GetTerritories().IsValidIndex(Index)
		&& WorldState->GetTerritories()[Index].bSupplied);
}
bool UGuLiResourceWorldSubsystem::CanUseStrongholdTransit(int32 Index, EGuLiTeam Team) const
{
	if (!WorldState || !WorldState->GetTerritories().IsValidIndex(Index)) return false;
	const auto& State = WorldState->GetTerritories()[Index];
	return GuLiResources::IsPlayableTeam(Team) && State.Owner == Team && !State.bEncircled;
}
void UGuLiResourceWorldSubsystem::RefreshEncirclement()
{
	auto Owner = [this](int32 Index) { return WorldState->GetTerritoryOwner(Index); };
	const TArray<bool> Red = StrongholdTopology.FindEncircled(EGuLiTeam::Red, Owner);
	const TArray<bool> Blue = StrongholdTopology.FindEncircled(EGuLiTeam::Blue, Owner);
	for (int32 Index = 0; Index < Red.Num(); ++Index)
		WorldState->SetTerritoryEncircledAuthority(Index, Red[Index] || Blue[Index]);
}
void UGuLiResourceWorldSubsystem::TickStrongholds(float DeltaTime)
{
	const float Interval = CVarStrongholdSample.GetValueOnGameThread();
	const float CaptureSeconds = CVarStrongholdCapture.GetValueOnGameThread();
	const float UpkeepSeconds = CVarStrongholdUpkeep.GetValueOnGameThread();
	checkf(Interval > 0 && CaptureSeconds > 0 && UpkeepSeconds > 0, TEXT("Stronghold timing must be positive."));
	CaptureAccumulator += DeltaTime;
	MaintenanceAccumulator += DeltaTime;
	if (ActiveMaintenancePeriod == 0) ActiveMaintenancePeriod = UpkeepSeconds;
	if (CaptureAccumulator >= Interval)
	{
		TArray<FIntPoint> Counts; Counts.Init(FIntPoint::ZeroValue, Outposts.Num());
		TArray<FGuLiCombatTargetSnapshot> Targets;
		GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>()->GetTargetSnapshots(Targets);
		for (const auto& Target : Targets)
		{
			if (!Target.bAlive || (Target.Handle.Kind != EGuLiTargetKind::CommanderSoldier
				&& Target.Handle.Kind != EGuLiTargetKind::GroundActor)) continue;
			if (AActor* Actor = Target.CollisionActor.Get())
			{
				if (Actor->FindComponentByClass<UGuLiBuildingLifecycleComponent>()) continue;
				const auto* Control = Actor->FindComponentByClass<UGuLiExternalUnitControlComponent>();
				if (Control && Control->IsPhased()) continue;
			}
			const int32 Index = FindTerritoryIndex(Target.Location);
			if (Index == INDEX_NONE) continue;
			if (Target.Team == EGuLiTeam::Red) ++Counts[Index].X;
			else if (Target.Team == EGuLiTeam::Blue) ++Counts[Index].Y;
		}
		for (int32 Index = 0; Index < Outposts.Num(); ++Index)
			Outposts[Index]->FindComponentByClass<UGuLiStrongholdCaptureComponent>()->AdvanceCapture(
				Counts[Index].X, Counts[Index].Y, CaptureAccumulator, CaptureSeconds);
		CaptureAccumulator = 0;
	}
	while (MaintenanceAccumulator >= ActiveMaintenancePeriod)
	{
		MaintenanceAccumulator -= ActiveMaintenancePeriod;
		ActiveMaintenancePeriod = UpkeepSeconds;
		FGuLiResourceAmounts Cost;
		Cost.Blue = CVarStrongholdBlueCost.GetValueOnGameThread(); Cost.Red = CVarStrongholdRedCost.GetValueOnGameThread();
		checkf(Cost.Blue >= 0 && Cost.Red >= 0, TEXT("Stronghold upkeep costs must be nonnegative."));
		auto& Economy = *GetWorld()->GetSubsystem<UGuLiTeamEconomySubsystem>();
		TArray<int32> Order;
		for (int32 Index = 0; Index < Outposts.Num(); ++Index) Order.Add(Index);
		Order.Sort([this](int32 A, int32 B) { return MapDefinition->Territories[A].TerritoryId.LexicalLess(MapDefinition->Territories[B].TerritoryId); });
		for (int32 Index : Order)
		{
			const EGuLiTeam Team = WorldState->GetTerritoryOwner(Index);
			if (!GuLiResources::IsPlayableTeam(Team)) continue;
			FGuLiEconomyReservation Reservation;
			const bool bPaid = !Cost.HasPositiveAmount() || Economy.Reserve(Team, MaintenanceAccount, ++MaintenanceRequestId, Cost, Reservation);
			if (Reservation.IsValid()) Economy.Commit(Reservation);
			WorldState->SetTerritorySupplyAuthority(Index, bPaid);
		}
	}
}
