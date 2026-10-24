#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "GuLiStrongholdFacilitiesComponent.generated.h"

USTRUCT()
struct FGuLiStrongholdGiftSlot
{
	GENERATED_BODY()
	UPROPERTY() int32 DefinitionId = 0;
	UPROPERTY() bool bDelivered = false;
	UPROPERTY() TObjectPtr<AActor> Building;
};

struct FGuLiHybridGiftJob
{
    FGuLiHybridObjectKey Target;
    FGuid Event;
    FTransform Placement;
    uint32 Attempt=0;
    bool Submitted=false;
    bool Prepared=false;
};

UCLASS()
class GULISTRIKE_API UGuLiStrongholdFacilitiesComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiStrongholdFacilitiesComponent();
	void InitializeFacilities();
    void RecoverGridGifts();
    void ReleaseGridGifts();
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	const TArray<FGuLiStrongholdGiftSlot>& GetSlots() const { return Slots; }
private:
    friend struct FGuLiHybridFacilities;
    TArray<FGuLiHybridGiftJob> HybridJobs;
    uint64 HybridNextAttempt=0;
	UPROPERTY() bool bFirstCaptured = false;
	UPROPERTY() TArray<FGuLiStrongholdGiftSlot> Slots;
	// Authority-only bounded search state; failed placement never consumes a gift.
	TMap<int32, int32> PlacementSearchCursor;
	TMap<int32, FString> LastPlacementFailure;

};
