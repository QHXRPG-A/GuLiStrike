#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GuLiShipCapabilityComponent.generated.h"

class UDataAsset;
class UGuLiShipCapabilityComponent;

UENUM(BlueprintType)
enum class EGuLiShipCapabilityState : uint8 { Prepared, Enabled, Suspended, Released };

USTRUCT()
struct FGuLiShipCapabilityTimerView
{
	GENERATED_BODY()
	UPROPERTY() FName TimerId;
	UPROPERTY() double EndsAtServerSeconds = 0;
};

/** Dynamic UI state travels through the fixed assembly component, independently of its manifest. */
USTRUCT()
struct FGuLiShipCapabilityRuntimeView
{
	GENERATED_BODY()
	UPROPERTY() FName GroupId;
	UPROPERTY() FName CapabilityId;
	UPROPERTY() EGuLiShipCapabilityState State = EGuLiShipCapabilityState::Prepared;
	UPROPERTY() TArray<FGuLiShipCapabilityTimerView> Timers;
};

DECLARE_MULTICAST_DELEGATE(FGuLiShipCapabilityRuntimeChanged);
DECLARE_MULTICAST_DELEGATE_TwoParams(FGuLiShipCapabilityStateChanged, UGuLiShipCapabilityComponent*, EGuLiShipCapabilityState);

/** Ship-owned behavior. Registration never grants gameplay; assembly commits do. */
UCLASS(Abstract, BlueprintType, ClassGroup=(Ship))
class GULISTRIKE_API UGuLiShipCapabilityComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiShipCapabilityComponent();
	virtual bool ValidateConfiguration(const UDataAsset* Configuration, FString& Error) const;
	/** Empty permits repeated instances. A named runtime domain is exclusive within one Ship. */
	virtual FName GetExclusiveRuntimeDomain() const { return NAME_None; }
	void Prepare(FName InGroupId, FName InCapabilityId, UDataAsset* Configuration);
	void SetCapabilityEnabled(bool bEnabled);
	void ReleaseCapability();
	UFUNCTION(BlueprintPure, Category="Ship|Capability") FName GetSourceGroupId() const { return SourceGroupId; }
	UFUNCTION(BlueprintPure, Category="Ship|Capability") FName GetCapabilityId() const { return CapabilityId; }
	UFUNCTION(BlueprintPure, Category="Ship|Capability") EGuLiShipCapabilityState GetCapabilityState() const { return State; }
	UFUNCTION(BlueprintPure, Category="Ship|Capability") bool IsCapabilityEnabled() const { return State == EGuLiShipCapabilityState::Enabled; }
	/** Returning false means no gameplay was committed. Implementations own activation rules. */
	virtual bool RequestActivation(FName ActionId, FString& Error);
	virtual void BuildRuntimeView(FGuLiShipCapabilityRuntimeView& View) const;
	virtual void ApplyRuntimeView(const FGuLiShipCapabilityRuntimeView& View) {}
	FGuLiShipCapabilityRuntimeChanged RuntimeChanged;
	FGuLiShipCapabilityStateChanged StateChanged;
protected:
	virtual void OnPrepared() {}
	virtual void OnEnabled() {}
	virtual void OnSuspended() {}
	virtual void OnReleased() {}
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	UPROPERTY(Transient) TObjectPtr<UDataAsset> CapabilityConfiguration;
private:
	UPROPERTY(Transient) FName SourceGroupId;
	UPROPERTY(Transient) FName CapabilityId;
	UPROPERTY(Transient) EGuLiShipCapabilityState State = EGuLiShipCapabilityState::Prepared;
};
