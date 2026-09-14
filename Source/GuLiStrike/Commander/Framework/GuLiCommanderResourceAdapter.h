// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commander/Network/GuLiCommanderTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiCommanderResourceAdapter.generated.h"

class AGuLiBattlePlayerState;
class AGuLiMiningVehiclePawn;
class APawn;
class UGuLiResourceWorldSubsystem;

/**
 * Commander-facing integration seam for Actor-based resource units.
 * Resource runtime code owns mining facts; this adapter owns Commander selection and order semantics.
 */
UCLASS()
class GULISTRIKE_API UGuLiCommanderResourceAdapter final : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	bool IsCommandRuntimeReady() const;
	bool ResolveActorSelection(
		const FGuLiSelectionRequest& Request,
		EGuLiTeam Team,
		TConstArrayView<FGuLiControllableActorId> ExistingIds,
		TArray<FGuLiControllableActorId>& OutIds) const;
	bool IssueMiningCommand(
		const AGuLiBattlePlayerState& PlayerState,
		TConstArrayView<FGuLiControllableActorId> SelectedIds,
		const FGuLiMiningCommand& Command) const;
	void HandleCommanderDisconnected(EGuLiTeam Team) const;
	bool GetControllableActorCenter(
		TConstArrayView<FGuLiControllableActorId> ActorIds,
		FVector& OutCenter) const;
	FGuLiControllableActorId FindControllableActorAlongRay(
		EGuLiTeam Team,
		const FVector& RayOrigin,
		const FVector& RayDirection,
		float PickHalfAngleRadians) const;
	bool FindClusterAlongRay(
		const FVector& RayOrigin,
		const FVector& RayDirection,
		uint16& OutClusterId,
		FVector& OutClusterCenter) const;
	bool IsFactoryAlongRay(
		EGuLiTeam Team,
		const FVector& RayOrigin,
		const FVector& RayDirection) const;

private:
	UGuLiResourceWorldSubsystem& Resources() const;
	void SynchronizeAuthorityState() const;
	void SynchronizeTeamPrivateState() const;
	AGuLiMiningVehiclePawn* FindMiningVehicle(FGuLiControllableActorId Id) const;
	APawn* FindEngineeringVehicle(FGuLiControllableActorId Id) const;
	static bool RayPassesSphere(
		const FVector& Origin,
		const FVector& Direction,
		const FVector& Center,
		float Radius,
		double& OutAlongRay);
};
