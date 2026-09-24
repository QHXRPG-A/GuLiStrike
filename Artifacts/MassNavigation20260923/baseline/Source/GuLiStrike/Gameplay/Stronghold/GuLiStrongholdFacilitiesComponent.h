#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "GuLiStrongholdFacilitiesComponent.generated.h"

USTRUCT()
struct FGuLiStrongholdGiftSlot
{
	GENERATED_BODY()
	UPROPERTY() int32 DefinitionId = 0;
	UPROPERTY() bool bDelivered = false;
	UPROPERTY() TObjectPtr<AActor> Building;
};

UCLASS()
class GULISTRIKE_API UGuLiStrongholdFacilitiesComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiStrongholdFacilitiesComponent();
	void InitializeFacilities();
	void HandleOwnerChanged(EGuLiTeam Team);
	virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	const TArray<FGuLiStrongholdGiftSlot>& GetSlots() const { return Slots; }
private:
	UPROPERTY(Replicated) bool bFirstCaptured = false;
	UPROPERTY(Replicated) TArray<FGuLiStrongholdGiftSlot> Slots;
	// Authority-only bounded search state; failed placement never consumes a gift.
	TMap<int32, int32> PlacementSearchCursor;
	TMap<int32, FString> LastPlacementFailure;
	void DeliverPendingSlots();
};
