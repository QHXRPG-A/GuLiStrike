#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GuLiSkillAuthoringLibrary.generated.h"

class UGuLiShipBuildCatalog;
class UGuLiCommanderSkillCatalog;
class AGuLiStrikeShip;
class UBlueprint;

/** Asset authoring checks stay in the editor module; runtime compilation remains usable by authority. */
UCLASS()
class GULISTRIKEEDITOR_API UGuLiSkillAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Skills")
	static bool CompileSkillBlueprint(UBlueprint* Blueprint);
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Skills")
	static TArray<FString> ValidateShipCatalog(UGuLiShipBuildCatalog* Catalog, TSubclassOf<AGuLiStrikeShip> ShipClass);
	UFUNCTION(BlueprintCallable, Category="GuLiStrike|Editor|Skills")
	static TArray<FString> ValidateCommanderCatalog(UGuLiCommanderSkillCatalog* Catalog);
};
