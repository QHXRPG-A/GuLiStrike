#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "Components/ActorComponent.h"
#include "GuLiOutpostPresentationComponent.generated.h"

class UMaterialBillboardComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/** Ownership display plus a lifetime phase origin; team changes never move the landmark. */
USTRUCT()
struct FGuLiOutpostOwnerState
{
	GENERATED_BODY()
	UPROPERTY() EGuLiTeam Team = EGuLiTeam::Unassigned;
	UPROPERTY() double FloatEpochServerTime = 0.0;
};

UCLASS(Config=Game, DefaultConfig)
class GULISTRIKE_API UGuLiOutpostPresentationSettings : public UObject
{
	GENERATED_BODY()
public:
	UGuLiOutpostPresentationSettings();
	UPROPERTY(Config, meta=(DeprecatedProperty, DeprecationMessage="Author Models.ResourcePath through ModelId.")) TSoftObjectPtr<UStaticMesh> Mesh;
	UPROPERTY(Config, EditAnywhere, Category="Models") int32 ModelId = 2003;
	UPROPERTY(Config, EditAnywhere, Category="Outpost") TSoftObjectPtr<UMaterialInterface> BodyMaterial;
	UPROPERTY(Config, EditAnywhere, Category="Outpost") TSoftObjectPtr<UMaterialInterface> HaloMaterial;
	UPROPERTY(Config, EditAnywhere, Category="Outpost") TSoftObjectPtr<UMaterialInterface> OutlineMaterial;
	UPROPERTY(Config, EditAnywhere, Category="Outpost|Size", meta=(ClampMin="1")) int32 ReferenceBuildingId = 5;
	UPROPERTY(Config, EditAnywhere, Category="Outpost|Size", meta=(ClampMin="0.01")) float ReferenceSizeMultiplier = 3.0f;
	/** Legacy height-only fallback when the reference building cannot be resolved. */
	UPROPERTY(Config, EditAnywhere, AdvancedDisplay, Category="Outpost|Size", meta=(ClampMin="1")) float ModelHeightCm = 1000.0f;
	UPROPERTY(Config, EditAnywhere, Category="Outpost", meta=(ClampMin="1")) float HoverHeightCm = 500.0f;
	UPROPERTY(Config, EditAnywhere, Category="Outpost", meta=(ClampMin="0")) float FloatAmplitudeCm = 50.0f;
	UPROPERTY(Config, EditAnywhere, Category="Outpost", meta=(ClampMin="0.01")) float FloatPeriodSeconds = 8.0f;
	UPROPERTY(Config, EditAnywhere, Category="Outpost|Motion", meta=(ClampMin="1")) float SelfRotationPeriodSeconds = 60.0f;
	UPROPERTY(Config, EditAnywhere, Category="Outpost") float GlowIntensity = 5.0f;
	UPROPERTY(Config, EditAnywhere, Category="Outpost") float HaloOpacity = 0.22f;
};

/** Resolved once from the production catalog; editor references use this same calculation. */
struct GULISTRIKE_API FGuLiOutpostVisualLayout
{
	FVector MeshScale = FVector::OneVector;
	FVector GroundOffset = FVector::ZeroVector;
	FVector SizeCm = FVector::ZeroVector;
	FVector ReferenceSizeCm = FVector::ZeroVector;
	FVector ReferenceMeshScale = FVector::OneVector;
	FVector ReferenceGroundOffset = FVector::ZeroVector;
	UStaticMesh* ReferenceMesh = nullptr;
	bool bUsesReferenceSize = false;
};

/** Owns only the visible landmark and its glow. Ground collision and capture never move. */
UCLASS(ClassGroup=(GuLiStrike))
class GULISTRIKE_API UGuLiOutpostPresentationComponent final : public UActorComponent
{
	GENERATED_BODY()
public:
	UGuLiOutpostPresentationComponent();
	void ApplyOwnerState(const FGuLiOutpostOwnerState& InState);
	static double SampleFloatHeight(double ServerTime, double Epoch, double Amplitude, double Period);
	static double SamplePresentationHeight(double ServerTime, const FGuLiOutpostOwnerState& OwnerState,
		const UGuLiOutpostPresentationSettings& Settings);
	static double SamplePresentationYaw(double ServerTime, const FGuLiOutpostOwnerState& OwnerState,
		const UGuLiOutpostPresentationSettings& Settings);
	/** A usable legacy fallback returns true with a diagnostic in Warning. */
	static bool ResolveVisualLayout(UStaticMesh* Mesh, FGuLiOutpostVisualLayout& Layout, FString& Warning);
	static FLinearColor OwnerColor(EGuLiTeam Team);
	bool GetSceneUIHalo(FVector& Center, EGuLiTeam& Team, FVector2D& RadiiCm) const;

protected:
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction) override;

private:
	void CreatePresentation();
	void RefreshColor();
	void UpdatePose();
	double ServerTime() const;
	FGuLiOutpostOwnerState State;
	FVector RestMeshCenterLocation = FVector::ZeroVector;
	FVector ScaledMeshBoundsOrigin = FVector::ZeroVector;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> VisualMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialBillboardComponent> Halo;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> HaloMaterial;
	FLinearColor LastGlowColor = FLinearColor::Transparent;
};
