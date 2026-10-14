#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UObject/Interface.h"
#include "GuLiExternalUnitControlComponent.generated.h"

class UMeshComponent;
class UMaterialInterface;

/** Movement implementations own prediction invalidation; callers only supply an authoritative pose. */
UINTERFACE(MinimalAPI)
class UGuLiExternalDisplacementTarget : public UInterface { GENERATED_BODY() };
class GULISTRIKE_API IGuLiExternalDisplacementTarget
{
	GENERATED_BODY()
public:
	virtual void ApplyExternalDisplacement(const FTransform& Transform) = 0;
};

USTRUCT()
struct FGuLiExternalUnitControlState
{
	GENERATED_BODY()
	UPROPERTY() FGuid OwnerToken;
	UPROPERTY() bool bPhased = false;
	UPROPERTY() bool bActionsLocked = false;
	UPROPERTY() FTransform Baseline;
	UPROPERTY() uint32 Revision = 0;
	UPROPERTY() uint32 DisplacementRevision = 0;
	UPROPERTY() uint8 RestoreMovementMode = 1;
	UPROPERTY() TObjectPtr<UMaterialInterface> PhaseMaterial;
};

USTRUCT()
struct FGuLiSavedMeshMaterials
{
	GENERATED_BODY()
	UPROPERTY() TWeakObjectPtr<UMeshComponent> Mesh;
	UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> Materials;
};

struct FGuLiExternalControlRestoreSettings
{
    bool Captured=false,Collision=true,CanBeDamaged=true;
};

/** Generic externally controlled state. Does not know about spells, tiers, Ship, Mass or targeting rules. */
UCLASS()
class GULISTRIKE_API UGuLiExternalUnitControlComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiExternalUnitControlComponent();
    virtual void BeginPlay() override;
    UPROPERTY(EditDefaultsOnly,Category="External Control") bool bPlayerVehicle=false;
    UPROPERTY(EditDefaultsOnly,Category="External Control") bool bAirborne=false;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	bool ApplyOwnedState(FGuid OwnerToken, bool bPhased, bool bLocked, const FTransform& Baseline,
		bool bDisplace, UMaterialInterface* PhaseMaterial = nullptr);
	bool IsPhased() const { return State.bPhased; }
	bool AreActionsLocked() const { return State.bActionsLocked; }
	FGuid GetOwnerToken() const { return State.OwnerToken; }
	const FGuLiExternalUnitControlState& GetState() const { return State; }
    /** Validated World adapter state application; this never emits an RPC. */
    void ApplyHybridState(const FGuLiExternalUnitControlState& InState);
    FGuLiExternalControlRestoreSettings CaptureRestoreSettings() const {return {bCapturedSettings,bPreviousCollision,bPreviousCanBeDamaged};}
    void RestoreHybridState(const FGuLiExternalUnitControlState& InState,const FGuLiExternalControlRestoreSettings& Settings);
	/** Presentation-only Pawns can share material restoration without creating a network authority. */
	void ApplyLocalPhaseAppearance(bool bPhased, UMaterialInterface* Material);
	DECLARE_MULTICAST_DELEGATE(FStateApplied);
	FStateApplied OnStateApplied;
	static bool IsActorPhased(const AActor* Actor);
	static bool AreActorActionsLocked(const AActor* Actor);
private:
	UPROPERTY() FGuLiExternalUnitControlState State;
	UPROPERTY(Transient) TArray<FGuLiSavedMeshMaterials> SavedMaterials;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> AppliedPhaseMaterial;

	void ApplyState();
	void ApplyMaterials();
	void RestoreMaterials();
	uint32 AppliedDisplacementRevision = 0;
	bool bCapturedSettings = false;
	bool bPreviousCollision = true;
	bool bPreviousCanBeDamaged = true;
};
