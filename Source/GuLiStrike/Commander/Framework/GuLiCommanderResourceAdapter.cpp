// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Framework/GuLiCommanderResourceAdapter.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Building/GuLiConstructionVehiclePawn.h"
#include "Gameplay/Building/GuLiBuildingRegistrySubsystem.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"

#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/Economy/GuLiTeamEconomySubsystem.h"
#include "Gameplay/Resources/GuLiResourceActors.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "Subsystems/SubsystemCollection.h"

namespace
{
	constexpr double MaximumSelectionRayDistance = 600000.0;

	bool IsPointInsideSelectionBox(const FGuLiSelectionRequest& Request, const FVector& Location)
	{
		const FVector Rays[] = {
			Request.BoxTopLeftRay, Request.BoxTopRightRay,
			Request.BoxBottomRightRay, Request.BoxBottomLeftRay
		};
		const FVector CenterRay = (Rays[0] + Rays[1] + Rays[2] + Rays[3]).GetSafeNormal();
		const FVector Offset = Location - FVector(Request.RayOrigin);
		if (FVector::DotProduct(Offset, CenterRay) <= 0.0
			|| Offset.SizeSquared() > FMath::Square(MaximumSelectionRayDistance))
		{
			return false;
		}
		for (int32 Index = 0; Index < 4; ++Index)
		{
			FVector Inward = FVector::CrossProduct(Rays[Index], Rays[(Index + 1) % 4]).GetSafeNormal();
			if (FVector::DotProduct(Inward, CenterRay) < 0.0) Inward *= -1.0;
			if (FVector::DotProduct(Inward, Offset) < -1.0) return false;
		}
		return true;
	}
}

bool UGuLiCommanderResourceAdapter::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

void UGuLiCommanderResourceAdapter::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGuLiTeamEconomySubsystem>();
	Collection.InitializeDependency<UGuLiResourceWorldSubsystem>();
}

void UGuLiCommanderResourceAdapter::Tick(const float DeltaTime)
{
	(void)DeltaTime;
	check(GetWorld());
	if (GetWorld()->GetNetMode() == NM_Client || !Resources().IsResourceWorldActive()) return;
	SynchronizeAuthorityState();
	SynchronizeTeamPrivateState();
}

TStatId UGuLiCommanderResourceAdapter::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UGuLiCommanderResourceAdapter, STATGROUP_Tickables);
}

bool UGuLiCommanderResourceAdapter::IsCommandRuntimeReady() const
{
	const UGuLiResourceWorldSubsystem& ResourceWorld = Resources();
	return !ResourceWorld.IsResourceWorldActive() || ResourceWorld.IsRuntimeReady();
}

bool UGuLiCommanderResourceAdapter::ResolveActorSelection(
	const FGuLiSelectionRequest& Request,
	const EGuLiTeam Team,
	const TConstArrayView<FGuLiControllableActorId> ExistingIds,
	TArray<FGuLiControllableActorId>& OutIds) const
{
	check(GetWorld()->GetNetMode() != NM_Client);
	OutIds.Reset();
	if (!Request.IsWellFormed() || !GuLiResources::IsPlayableTeam(Team)) return false;

	TArray<FGuLiControllableActorId> Hits;
	if (Request.Modifier != EGuLiSelectionModifier::Clear)
	{
		for (TActorIterator<APawn> It(GetWorld()); It; ++It)
		{
			const APawn& Pawn = **It;
			const auto* Interface = Cast<IGuLiEngineeringVehicle>(&Pawn);
			if (!Interface || UGuLiExternalUnitControlComponent::IsActorPhased(&Pawn)) continue;
			const auto& Vehicle = *Interface;
			if (Vehicle.GetTeam() != Team || !Vehicle.GetStableActorId().IsValid()) continue;
			bool bHit = false;
			if (Request.Kind == EGuLiSelectionKind::Point)
			{
				double Along = 0.0;
				bHit = Vehicle.GetStableActorId() == Request.SeedActorId
					&& RayPassesSphere(Request.RayOrigin, Request.RayDirection,
						Pawn.GetActorLocation(), 1200.0f, Along);
			}
			else if (Request.Kind == EGuLiSelectionKind::SameType)
			{
				const APawn* Seed = FindEngineeringVehicle(Request.SeedActorId);
				bHit = Seed && Seed->GetClass() == Pawn.GetClass();
			}
			else if (Request.Kind == EGuLiSelectionKind::Box)
			{
				bHit = IsPointInsideSelectionBox(Request, Pawn.GetActorLocation());
			}
			else
			{
				bHit = FVector::DistSquared2D(Pawn.GetActorLocation(), Request.Center)
					<= FMath::Square(GuLiCommanderProtocol::GetSelectionRadiusCentimeters(Request.RadiusPreset));
			}
			if (bHit) Hits.Add(Vehicle.GetStableActorId());
		}
		if ((Request.Kind == EGuLiSelectionKind::Point || Request.Kind == EGuLiSelectionKind::SameType)
			&& Request.SeedActorId.IsValid() && Hits.IsEmpty()) return false;
	}

	TSet<FGuLiControllableActorId> Combined;
	if (Request.Modifier == EGuLiSelectionModifier::Add || Request.Modifier == EGuLiSelectionModifier::Toggle)
		for (const FGuLiControllableActorId Id : ExistingIds) if (Id.IsValid()) Combined.Add(Id);
	for (const FGuLiControllableActorId Id : Hits)
	{
		if (Request.Modifier == EGuLiSelectionModifier::Toggle && Combined.Contains(Id)) Combined.Remove(Id);
		else Combined.Add(Id);
	}
	OutIds = Combined.Array();
	OutIds.Sort();
	return true;
}

bool UGuLiCommanderResourceAdapter::IssueMiningCommand(
	const AGuLiBattlePlayerState& PlayerState,
	const TConstArrayView<FGuLiControllableActorId> SelectedIds,
	const FGuLiMiningCommand& Command) const
{
	check(GetWorld()->GetNetMode() != NM_Client);
	if (!PlayerState.IsCommander() || !Command.IsWellFormed()) return false;
	bool bAcceptedAny = false;
	for (const FGuLiControllableActorId Id : SelectedIds)
	{
		APawn* Pawn = FindEngineeringVehicle(Id);
		if (!Pawn || CastChecked<IGuLiEngineeringVehicle>(Pawn)->GetTeam() != PlayerState.GetTeam()) continue;
		if (auto* Miner = Cast<AGuLiMiningVehiclePawn>(Pawn))
			bAcceptedAny |= Miner->IssuePlayerCommand(Command, PlayerState.GetTeam());
		else if (auto* Builder = Cast<AGuLiConstructionVehiclePawn>(Pawn))
		{
			if (Command.Type == EGuLiMiningOrderType::Cancel) { bAcceptedAny |= Builder->IssueMove(Builder->GetActorLocation()); continue; }
			if (Command.Type != EGuLiMiningOrderType::Move) continue;
			TArray<UGuLiBuildingLifecycleComponent*> Buildings;
			GetWorld()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->Query(Buildings);
			UGuLiBuildingLifecycleComponent* Site = nullptr;
			for (auto* Building : Buildings)
			{
				if (Building->GetTeam() != PlayerState.GetTeam() || Building->GetState().Phase != EGuLiBuildingPhase::UnderConstruction) continue;
				const FVector Local = Building->GetOwner()->GetActorTransform().InverseTransformPosition(Command.Target);
				const FVector Extent = Building->GetDefinition().CollisionExtent;
				if (FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y) { Site = Building; break; }
			}
			bAcceptedAny |= Site ? Builder->IssueConstruction(Site) : Builder->IssueMove(Command.Target);
		}
	}
	return bAcceptedAny;
}

void UGuLiCommanderResourceAdapter::IssueStrongholdTransit(
	const AGuLiBattlePlayerState& PlayerState, TConstArrayView<FGuLiControllableActorId> SelectedIds,
	const FGuLiStrongholdTransitOrder& Order, FGuLiCommandAck& Ack) const
{
	check(GetWorld()->GetNetMode() != NM_Client && PlayerState.IsCommander());
	int32 Accepted = 0;
	for (const auto Id : SelectedIds)
	{
		APawn* Pawn = FindEngineeringVehicle(Id);
		const auto Result = Pawn ? CastChecked<IGuLiEngineeringVehicle>(Pawn)->IssueStrongholdTransit(Order,PlayerState.GetTeam())
			: EGuLiTransitOrderResult::InvalidVehicle;
		Ack.EngineeringResults.Add({Id,Result});
		Accepted += GuLiEngineeringCommands::IsAccepted(Result) ? 1 : 0;
		UE_LOG(LogTemp,Display,TEXT("StrongholdTransit request=%u vehicle=%u target=%s result=%s"),
			uint32(Order.RequestId),Id.Value,*Order.TerritoryId.ToString(),*GuLiEngineeringCommands::Describe(Result));
	}
	Ack.Result = SelectedIds.IsEmpty() ? EGuLiCommandAckResult::NoSelection
		: Accepted == SelectedIds.Num() ? EGuLiCommandAckResult::Accepted
		: Accepted > 0 ? EGuLiCommandAckResult::PartiallyAccepted : EGuLiCommandAckResult::InvalidTarget;
}

void UGuLiCommanderResourceAdapter::HandleCommanderDisconnected(const EGuLiTeam Team) const
{
	check(GetWorld()->GetNetMode() != NM_Client);
	if (!GuLiResources::IsPlayableTeam(Team)) return;
	for (TActorIterator<AGuLiMiningVehiclePawn> It(GetWorld()); It; ++It)
	{
		AGuLiMiningVehiclePawn& Vehicle = **It;
		if (Vehicle.GetTeam() == Team)
		{
			Vehicle.ForceAutomaticControl();
		}
	}
}

bool UGuLiCommanderResourceAdapter::GetControllableActorCenter(
	const TConstArrayView<FGuLiControllableActorId> ActorIds,
	FVector& OutCenter) const
{
	OutCenter = FVector::ZeroVector;
	int32 Count = 0;
	for (const FGuLiControllableActorId Id : ActorIds)
	{
		if (const APawn* Vehicle = FindEngineeringVehicle(Id))
		{
			OutCenter += Vehicle->GetActorLocation();
			++Count;
		}
	}
	if (Count == 0) return false;
	OutCenter /= static_cast<double>(Count);
	return true;
}

FGuLiControllableActorId UGuLiCommanderResourceAdapter::FindControllableActorAlongRay(
	const EGuLiTeam Team,
	const FVector& RayOrigin,
	const FVector& RayDirection,
	const float PickHalfAngleRadians) const
{
	FGuLiControllableActorId Result;
	double BestAlong = TNumericLimits<double>::Max();
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
	{
		const APawn& Pawn = **It;
		const auto* Interface = Cast<IGuLiEngineeringVehicle>(&Pawn);
		if (!Interface || UGuLiExternalUnitControlComponent::IsActorPhased(&Pawn)) continue;
		const auto& Vehicle = *Interface;
		if (Vehicle.GetTeam() != Team) continue;
		double Along = 0.0;
		const float Radius = 1200.0f
			+ static_cast<float>(FMath::Tan(PickHalfAngleRadians) * 10000.0);
		if (RayPassesSphere(RayOrigin, RayDirection, Pawn.GetActorLocation(), Radius, Along)
			&& Along < BestAlong)
		{
			BestAlong = Along;
			Result = Vehicle.GetStableActorId();
		}
	}
	return Result;
}

bool UGuLiCommanderResourceAdapter::FindClusterAlongRay(
	const FVector& RayOrigin,
	const FVector& RayDirection,
	uint16& OutClusterId,
	FVector& OutClusterCenter) const
{
	OutClusterId = 0u;
	OutClusterCenter = FVector::ZeroVector;
	const UGuLiResourceWorldSubsystem& ResourceWorld = Resources();
	const UGuLiResourceMapDefinition* Definition = ResourceWorld.GetMapDefinition();
	if (!ResourceWorld.IsRuntimeReady() || !Definition) return false;

	double BestAlong = TNumericLimits<double>::Max();
	for (const FGuLiResourceClusterDefinition& Cluster : Definition->Clusters)
	{
		if (ResourceWorld.IsClusterEmpty(Cluster.ClusterId)) continue;
		double Along = 0.0;
		if (RayPassesSphere(RayOrigin, RayDirection, Cluster.Center,
			Cluster.ObstacleRadiusCentimeters, Along) && Along < BestAlong)
		{
			BestAlong = Along;
			OutClusterId = Cluster.ClusterId;
			OutClusterCenter = Cluster.Center;
		}
	}
	return OutClusterId != 0u;
}

bool UGuLiCommanderResourceAdapter::IsFactoryAlongRay(
	const EGuLiTeam Team,
	const FVector& RayOrigin,
	const FVector& RayDirection) const
{
	for (TActorIterator<AGuLiResourceFactoryActor> It(GetWorld()); It; ++It)
	{
		if (It->GetTeam() != Team) continue;
		double Along = 0.0;
		return RayPassesSphere(RayOrigin, RayDirection, It->GetActorLocation(), 5000.0f, Along);
	}
	return false;
}

UGuLiResourceWorldSubsystem& UGuLiCommanderResourceAdapter::Resources() const
{
	UGuLiResourceWorldSubsystem* ResourceWorld =
		GetWorld()->GetSubsystem<UGuLiResourceWorldSubsystem>();
	check(ResourceWorld);
	return *ResourceWorld;
}

void UGuLiCommanderResourceAdapter::SynchronizeAuthorityState() const
{
	bool bHasRedCommander = false;
	bool bHasBlueCommander = false;
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* Controller = It->Get();
		check(Controller);
		const AGuLiBattlePlayerState* PlayerState = Controller->GetPlayerState<AGuLiBattlePlayerState>();
		if (!PlayerState || !PlayerState->IsCommander()) continue;
		if (PlayerState->GetTeam() == EGuLiTeam::Red) bHasRedCommander = true;
		else if (PlayerState->GetTeam() == EGuLiTeam::Blue) bHasBlueCommander = true;
	}
	for (TActorIterator<AGuLiMiningVehiclePawn> It(GetWorld()); It; ++It)
	{
		check(GuLiResources::IsPlayableTeam(It->GetTeam()));
		const bool bHasCommander = It->GetTeam() == EGuLiTeam::Red
			? bHasRedCommander
			: bHasBlueCommander;
		if (!bHasCommander && It->GetControlMode() != EGuLiMiningControlMode::Auto)
			It->ForceAutomaticControl();
	}
}

void UGuLiCommanderResourceAdapter::SynchronizeTeamPrivateState() const
{
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	if (!GameState) return;
	const UGuLiTeamEconomySubsystem* Economy =
		GetWorld()->GetSubsystem<UGuLiTeamEconomySubsystem>();
	check(Economy);

	FGuLiTeamResourcePrivateState RedState;
	FGuLiTeamResourcePrivateState BlueState;
	RedState.Inventory = Economy->GetTeamBalance(EGuLiTeam::Red);
	BlueState.Inventory = Economy->GetTeamBalance(EGuLiTeam::Blue);
	for (TActorIterator<AGuLiResourceFactoryActor> It(GetWorld()); It; ++It)
	{
		// A neutralized outpost retains its facilities, but no team owns their private status.
		if (It->GetTeam() == EGuLiTeam::Unassigned) continue;
		check(GuLiResources::IsPlayableTeam(It->GetTeam()));
		FGuLiTeamResourcePrivateState& State = It->GetTeam() == EGuLiTeam::Red
			? RedState
			: BlueState;
		State.Factories.Add(It->MakePrivateState());
	}
	for (TActorIterator<AGuLiMiningVehiclePawn> It(GetWorld()); It; ++It)
	{
		check(GuLiResources::IsPlayableTeam(It->GetTeam()));
		FGuLiTeamResourcePrivateState& State = It->GetTeam() == EGuLiTeam::Red
			? RedState
			: BlueState;
		State.MiningVehicles.Add(It->MakePrivateState());
	}
	RedState.SortByStableId();
	BlueState.SortByStableId();

	for (APlayerState* BasePlayerState : GameState->PlayerArray)
	{
		AGuLiBattlePlayerState* PlayerState = Cast<AGuLiBattlePlayerState>(BasePlayerState);
		if (!PlayerState || !GuLiResources::IsPlayableTeam(PlayerState->GetTeam())) continue;
		PlayerState->SetServerResourcePrivateState(
			PlayerState->GetTeam() == EGuLiTeam::Red ? RedState : BlueState);
	}
}

APawn* UGuLiCommanderResourceAdapter::FindEngineeringVehicle(FGuLiControllableActorId Id) const
{
	for (TActorIterator<APawn> It(GetWorld()); It; ++It)
		if (const auto* Vehicle = Cast<IGuLiEngineeringVehicle>(*It))
			if (Vehicle->GetStableActorId() == Id) return *It;
	return nullptr;
}

AGuLiMiningVehiclePawn* UGuLiCommanderResourceAdapter::FindMiningVehicle(
	const FGuLiControllableActorId Id) const
{
	for (TActorIterator<AGuLiMiningVehiclePawn> It(GetWorld()); It; ++It)
		if (It->GetStableActorId() == Id) return *It;
	return nullptr;
}

bool UGuLiCommanderResourceAdapter::RayPassesSphere(
	const FVector& Origin,
	const FVector& Direction,
	const FVector& Center,
	const float Radius,
	double& OutAlongRay)
{
	const FVector UnitDirection = Direction.GetSafeNormal();
	if (UnitDirection.IsNearlyZero() || Origin.ContainsNaN() || Center.ContainsNaN()) return false;
	const FVector Offset = Center - Origin;
	OutAlongRay = FVector::DotProduct(Offset, UnitDirection);
	return OutAlongRay > 0.0 && OutAlongRay <= MaximumSelectionRayDistance
		&& (Offset - UnitDirection * OutAlongRay).SizeSquared() <= FMath::Square(Radius);
}
