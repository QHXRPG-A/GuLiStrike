#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiScaleMigrationLibrary.generated.h"

class AGuLiMiningVehiclePawn;
class AGuLiResourceFactoryActor;

/** Narrow editor-only support for the reviewed 0.2 migration; never modifies vendor systems. */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiScaleMigrationLibrary final : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	/** Resolve the existing authoritative assignment for a PIE probe; no reassignment. */
	UFUNCTION(BlueprintCallable, Category="GuLi|Migration")
	static AGuLiResourceFactoryActor* ReadMiningFactory(AGuLiMiningVehiclePawn* Miner);
	/** Read-only static switches on the production mining system's size modules. */
	UFUNCTION(BlueprintCallable, Category="GuLi|Migration")
	static TMap<FString, FString> ReadMiningSizeSwitches();
	/** Conflict-checked assignment for Vec2 sprite dimensions that the existing RI tool cannot write. */
	UFUNCTION(BlueprintCallable, Category="GuLi|Migration")
	static bool SetMiningSpriteSize020(const FString& Emitter, const FString& Stage,
		const FString& Parameter, FVector2D ExpectedBefore, FVector2D Target);
};
