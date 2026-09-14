#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Blueprint/UserWidget.h"
#include "Gameplay/Teleport/GuLiTeleportAbility.h"
#include "Gameplay/Teleport/GuLiTeleportTypes.h"
#include "GuLiTeleportInputComponent.generated.h"
class AGuLiTeleportFieldActor;
class AGuLiCommanderPlayerController;

UCLASS()
class GULISTRIKE_API UGuLiTeleportHUDWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
};

UCLASS()
class GULISTRIKE_API UGuLiTeleportInputComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiTeleportInputComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	UFUNCTION(BlueprintCallable, Category="Commander|Teleport") void ActivateAiming();
	UFUNCTION(BlueprintCallable, Category="Commander|Teleport") void CancelTeleport();
	UFUNCTION(BlueprintPure, Category="Commander|Teleport") bool IsAiming() const { return bArmed; }
	UFUNCTION(BlueprintPure, Category="Commander|Teleport") FGuLiTeleportCastState QueryState() const;
	/** Authority-only API; the ordinary client cannot change its own tier. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Commander|Teleport") bool SetServerLevel(int32 NewLevel);
	bool HandlePrimaryAction();
	bool IsHUDHovered() const;
	UFUNCTION(BlueprintPure, Category="Commander|Teleport") FText GetStatusText() const;
	int32 GetLevel() const { return Level; }
	UFUNCTION(Server, Reliable, BlueprintCallable, Category="Commander|Teleport") void ServerSubmit(EGuLiTeleportCommand Command, FGuid CastId, FVector Point);
private:
	UFUNCTION(Client, Reliable) void ClientResult(EGuLiTeleportCommand Command, bool bSucceeded, FGuid CastId, const FString& Error);
	void ClearLocalAim();
	AGuLiCommanderPlayerController* GetCommander() const;
	UPROPERTY(Replicated) int32 Level = 1;
	UPROPERTY(Transient) TObjectPtr<AGuLiTeleportFieldActor> Preview;
	UPROPERTY(Transient) TObjectPtr<UGuLiTeleportHUDWidget> HUD;
	FGuid CurrentCastId;
	FString Feedback;
	bool bArmed = false;
	bool bSourcePending = false;
	bool bCancelPending = false;
	double NextServerPointTime = 0;
};
