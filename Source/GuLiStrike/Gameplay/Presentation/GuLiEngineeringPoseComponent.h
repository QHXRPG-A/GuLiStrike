#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "GuLiEngineeringPoseComponent.generated.h"

class UChildActorComponent;
class UGuLiExternalUnitControlComponent;

/** Cosmetic interpolation for engineering Actor carriers. Never moves the collision root. */
UCLASS()
class GULISTRIKE_API UGuLiEngineeringPoseComponent final : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiEngineeringPoseComponent();
	void InitializeVisual(UChildActorComponent& Component, const FBox& Bounds);
	void ReleaseVisual();
	void ResetToAuthority();
	void SetPresentationVisible(bool bVisible);
	const FTransform& GetPresentedRoot() const { return DisplayedRoot; }
	UFUNCTION(BlueprintPure, Category="Engineering|Presentation") FString GetPoseDiagnosticsJson() const;
	DECLARE_MULTICAST_DELEGATE(FDisplayedPoseChanged);
	FDisplayedPoseChanged OnDisplayedPoseChanged;
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* Function) override;
private:
	void RootChanged(USceneComponent* Component, EUpdateTransformFlags Flags, ETeleportType Teleport);
	void ExternalStateApplied();
	void WriteVisual(const FTransform& Pose);
	TWeakObjectPtr<UChildActorComponent> Visual;
	TWeakObjectPtr<USceneComponent> CollisionRoot;
	TWeakObjectPtr<UGuLiExternalUnitControlComponent> ExternalControl;
	FDelegateHandle RootHandle, ExternalHandle;
	FTransform BaseRelative = FTransform::Identity;
	FTransform DisplayedRoot = FTransform::Identity;
	FTransform BlendFrom = FTransform::Identity, BlendTarget = FTransform::Identity;
	FBox LocalBounds = FBox(ForceInit);
	double LastReceipt = -1, BlendStarted = 0, BlendDuration = 0, LastTickTime = 0;
	uint32 DisplacementRevision = 0;
	bool bPending = false, bBlending = false, bVisible = true, bOffscreen = false, bPhased = false;
	bool bCanPresent = false, bHasVisualPose = false;
};
