#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Stronghold/GuLiStrongholdGateConfig.h"
#include "GuLiStrongholdGateComponent.generated.h"
class APawn;
class UDecalComponent;
class UStaticMeshComponent;

/** Permanent spell field composed onto the indestructible outpost. */
UCLASS()
class GULISTRIKE_API UGuLiStrongholdGateComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiStrongholdGateComponent();
	void InitializeGate(int32 InFieldId, int32 InTerritoryIndex, const FVector& Ground);
	bool CanEnter(const APawn& Vehicle) const;
	bool FindEntry(const APawn& Vehicle, FVector& Entry) const;
	const FGuLiStrongholdGateConfig& GetConfig() const;
	FVector GetGroundLocation() const { return GroundLocation; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
private:
	UPROPERTY(ReplicatedUsing=OnRep_Gate) int32 FieldId = 0;
	UPROPERTY(ReplicatedUsing=OnRep_Gate) int32 TerritoryIndex = INDEX_NONE;
	UPROPERTY(ReplicatedUsing=OnRep_Gate) FVector GroundLocation = FVector::ZeroVector;
	UPROPERTY(Transient) TObjectPtr<UActorComponent> Presentation;
	UFUNCTION() void OnRep_Gate();
};
