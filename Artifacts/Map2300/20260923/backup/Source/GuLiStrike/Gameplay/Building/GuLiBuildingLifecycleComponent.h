#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/Interface.h"
#include "Gameplay/Building/GuLiBuildingTypes.h"
#include "Gameplay/Building/GuLiConstructionSlots.h"
#include "GuLiBuildingLifecycleComponent.generated.h"

UINTERFACE(MinimalAPI)
class UGuLiBuildingOwner : public UInterface { GENERATED_BODY() };
class GULISTRIKE_API IGuLiBuildingOwner
{
	GENERATED_BODY()
public:
	virtual EGuLiTeam GetBuildingTeam() const = 0;
	virtual void SetBuildingTeamAuthority(EGuLiTeam Team) = 0;
	virtual FVector GetBuildingGroundLocation() const = 0;
};
UENUM(BlueprintType)
enum class EGuLiBuildingPhase : uint8 { UnderConstruction, Completed, Destroyed };
UENUM()
enum class EGuLiBuildingOrigin : uint8 { Manual, Map, Gift };

USTRUCT(BlueprintType)
struct FGuLiBuildingLifecycleState
{
	GENERATED_BODY()
	UPROPERTY() uint32 InstanceId = 0;
	UPROPERTY(BlueprintReadOnly) int32 DefinitionId = 0;
	UPROPERTY(BlueprintReadOnly) int32 TerritoryIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly) EGuLiBuildingPhase Phase = EGuLiBuildingPhase::UnderConstruction;
	UPROPERTY() EGuLiBuildingOrigin Origin = EGuLiBuildingOrigin::Manual;
	UPROPERTY() FGuid BuilderGuid;
	UPROPERTY(BlueprintReadOnly) float WorkDone = 0;
};

UCLASS(ClassGroup=(GuLiStrike), meta=(BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiBuildingLifecycleComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiBuildingLifecycleComponent();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	void InitializeBuilding(int32 DefinitionId, int32 TerritoryIndex, EGuLiBuildingOrigin Origin, bool bCompleted, const FGuid& Builder = FGuid());
	void AddConstructionWork(float Work);
	void AccumulateConstructionWork(float Work);
	virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* Function) override;
	void PrepareConstructionSlots(ACharacter& Prototype);
	bool BuildNextConstructionSlot();
	void InvalidateConstructionSlots(const FBox& Bounds);
	EGuLiWorkPositionAvailability GetConstructionAvailability() const;
	uint32 GetConstructionSlotGeneration() const { return SlotGeneration; }
	UFUNCTION(BlueprintPure, Category="Building") TArray<FTransform> GetConstructionSlotPoses() const;
	UFUNCTION(BlueprintPure, Category="Building") int32 GetReservedConstructionSlots() const;
	UFUNCTION(BlueprintPure, Category="Building") bool AreConstructionSlotsReady() const { return !bSlotsPending; }
	EGuLiWorkPositionAvailability TryReserveConstructionSlot(ACharacter& Vehicle, uint32 Task,
		TConstArrayView<int32> ExcludedSlots, FGuLiConstructionSlotReservation& Out, FVector& Position);
	bool ValidateConstructionSlot(const ACharacter& Vehicle, const FGuLiConstructionSlotReservation& Reservation) const;
	void ReleaseConstructionSlot(const ACharacter& Vehicle, const FGuLiConstructionSlotReservation& Reservation);
	void RefreshTeam();
	UFUNCTION(BlueprintPure, Category="Building") const FGuLiBuildingLifecycleState& GetState() const { return State; }
	const FGuLiBuildingDefinition& GetDefinition() const;
	EGuLiTeam GetTeam() const;
	FVector GetGroundLocation() const;
	bool IsCompleted() const { return State.Phase == EGuLiBuildingPhase::Completed; }
	bool CountsForManualLimit() const { return State.Origin == EGuLiBuildingOrigin::Manual; }
	UFUNCTION(BlueprintPure, Category="Building") float GetConstructionProgress() const;
	/** Local notification on authority and replicas; observers read the current snapshot. */
	FSimpleMulticastDelegate OnConstructionStateChanged;
private:
	UPROPERTY(ReplicatedUsing=OnRep_State) FGuLiBuildingLifecycleState State;
	UFUNCTION() void OnRep_State();
	UFUNCTION() void HandleDeath();
	void RegisterInstance();
	TArray<FGuLiConstructionSlot> ConstructionSlots;
	TWeakObjectPtr<ACharacter> ConstructionPrototype;
	uint32 SlotGeneration = 1;
	int32 NextSlotSample = 0;
	bool bSlotsPending = true;
	float PendingConstructionWork = 0;
};
