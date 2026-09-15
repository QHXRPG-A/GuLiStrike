#pragma once
#include "CoreMinimal.h"
#include "Gameplay/Resources/GuLiResourceTypes.h"
#include "GuLiEngineeringCommandTypes.generated.h"

UENUM(BlueprintType)
enum class EGuLiTransitOrderResult : uint8
{
	Accepted, DeferredUntilFactoryExit, SameTerritory, InvalidRequest, Unauthorized,
	InvalidVehicle, ActionsLocked, InvalidSource, SourceEncircled, InvalidTarget,
	TargetEncircled, NoRoute, StaleRequest
};

/** Explicit relocation; a ground move never constructs this command. */
USTRUCT(BlueprintType)
struct FGuLiStrongholdTransitOrder
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 RequestId = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 SelectionRevision = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FName TerritoryId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector ClickLocation = FVector::ZeroVector;
	bool IsWellFormed() const { return RequestId != 0 && SelectionRevision != 0 && !TerritoryId.IsNone() && !ClickLocation.ContainsNaN(); }
};

namespace GuLiEngineeringCommands
{
	GULISTRIKE_API bool IsAccepted(EGuLiTransitOrderResult Result);
	GULISTRIKE_API FString Describe(EGuLiTransitOrderResult Result);
}
