#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/Interface.h"
#include "Gameplay/Building/GuLiBuildingTypes.h"
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
	void RefreshTeam();
	UFUNCTION(BlueprintPure, Category="Building") const FGuLiBuildingLifecycleState& GetState() const { return State; }
	const FGuLiBuildingDefinition& GetDefinition() const;
	EGuLiTeam GetTeam() const;
	FVector GetGroundLocation() const;
	bool IsCompleted() const { return State.Phase == EGuLiBuildingPhase::Completed; }
	bool CountsForManualLimit() const { return State.Origin == EGuLiBuildingOrigin::Manual; }
private:
	UPROPERTY(ReplicatedUsing=OnRep_State) FGuLiBuildingLifecycleState State;
	UFUNCTION() void OnRep_State();
	UFUNCTION() void HandleDeath();
	void RegisterInstance();
};
