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

/** One replicated ownership snapshot; cosmetic transforms are reconstructed locally. */
USTRUCT()
struct FGuLiOutpostOwnerState
{
	GENERATED_BODY()
	UPROPERTY() EGuLiTeam Team = EGuLiTeam::Unassigned;
	UPROPERTY() double FloatEpochServerTime = 0.0;
	UPROPERTY() double ChangedServerTime = 0.0;
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
	UPROPERTY(Config, EditAnywhere, Category="Outpost") float ModelHeightCm = 1000.0f;
	UPROPERTY(Config, EditAnywhere, Category="Outpost") float FloatAmplitudeCm = 5000.0f;
	UPROPERTY(Config, EditAnywhere, Category="Outpost") float FloatPeriodSeconds = 5.0f;
	UPROPERTY(Config, EditAnywhere, Category="Outpost") float LandingSeconds = 0.5f;
	UPROPERTY(Config, EditAnywhere, Category="Outpost") float GlowIntensity = 5.0f;
	UPROPERTY(Config, EditAnywhere, Category="Outpost") float HaloOpacity = 0.22f;
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
	FVector RestMeshLocation = FVector::ZeroVector;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> VisualMesh;
	UPROPERTY(Transient) TObjectPtr<UMaterialBillboardComponent> Halo;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> BodyMaterial;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> HaloMaterial;
	FLinearColor LastGlowColor = FLinearColor::Transparent;
};
