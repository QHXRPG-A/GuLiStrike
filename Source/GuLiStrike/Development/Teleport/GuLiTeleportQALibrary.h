#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Gameplay/Teleport/GuLiTeleportTypes.h"
#include "GuLiTeleportQALibrary.generated.h"
class ASceneCapture2D;
class UGuLiTeleportInputComponent;
UCLASS()
class GULISTRIKE_API UGuLiTeleportQALibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
#if WITH_EDITOR
	/** Temporary PIE settings only; does not save config or assets. Modes: 0 standalone, 1 listen, 2 dedicated clients. */
	UFUNCTION(BlueprintCallable,Category="Development|Teleport") static bool ConfigurePIE(int32 Mode, int32 Clients);
	UFUNCTION(BlueprintCallable,Category="Development|Teleport") static void RequestLateJoin();
	UFUNCTION(BlueprintCallable,Category="Development|Teleport") static bool PreferAirForNextJoin(UObject* WorldContext);
	UFUNCTION(BlueprintCallable,Category="Development|Teleport") static bool RevokeCommanderRole(APlayerController* Controller);
	/** Exercise the real RPC callspace: editor Python otherwise forces RPCs to execute locally. */
	UFUNCTION(BlueprintCallable,Category="Development|Teleport") static bool SubmitIntent(UGuLiTeleportInputComponent* Input, EGuLiTeleportCommand Command, FGuid CastId, FVector Point);
	UFUNCTION(BlueprintCallable,Category="Development|Teleport") static ASceneCapture2D* CreateCapture(UObject* WorldContext);
	UFUNCTION(BlueprintCallable,Category="Development|Teleport") static AActor* CreateBlocker(UObject* WorldContext, FVector Center, FVector Extent);
	UFUNCTION(BlueprintPure,Category="Development|Teleport") static FString Snapshot(UObject* WorldContext);
	UFUNCTION(BlueprintCallable,Category="Development|Teleport") static bool DamageMass(UObject* WorldContext, int64 Id, float Amount);
#endif
};
