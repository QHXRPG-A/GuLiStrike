#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/AssetUserData.h"
#include "GuLiRecoilVAT.generated.h"

class UStaticMesh;
class UTexture2D;
class UMaterialInstanceDynamic;
class UInstancedStaticMeshComponent;
struct FGuLiMechanicalAnimationState;

/** One rigid recoil sample, in unscaled mesh centimeters. Identical CPU/GPU data. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiRecoilVATSample
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) FVector UpperTranslation = FVector::ZeroVector;
	UPROPERTY(EditAnywhere) FQuat UpperRotation = FQuat::Identity;
	UPROPERTY(EditAnywhere) FVector LeftGunTranslation = FVector::ZeroVector;
	UPROPERTY(EditAnywhere) FVector RightGunTranslation = FVector::ZeroVector;
};

/** Two closed, independent rigid-part VAT loops, composed after locomotion. */
UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiRecoilVATDefinition final : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere) TSoftObjectPtr<UTexture2D> PositionTexture;
	UPROPERTY(EditAnywhere) TSoftObjectPtr<UTexture2D> RotationTexture;
	UPROPERTY(EditAnywhere) int32 FramesPerSide = 33;
	UPROPERTY(EditAnywhere) float CycleSeconds = .5f;
	UPROPERTY(EditAnywhere) int32 MissileFrames = 0;
	UPROPERTY(EditAnywhere) float MissileEnterSeconds = .1f;
	UPROPERTY(EditAnywhere) float MissileHoldSeconds = 1.2f;
	UPROPERTY(EditAnywhere) float MissileReturnSeconds = .5f;
	UPROPERTY(EditAnywhere) float MissilePeakPitchDegrees = 8.f;
	UPROPERTY(EditAnywhere) FVector UpperPivot = FVector::ZeroVector;
	UPROPERTY(EditAnywhere) TArray<FVector> GunPivots;
	UPROPERTY(EditAnywhere) TArray<FVector> ReferenceMuzzles;
	UPROPERTY(EditAnywhere) bool bMirrorGunPivotY = false;
	UPROPERTY(EditAnywhere) FBox RenderBounds = FBox(ForceInit);
	/** Left, right, then optional held missile clip. Each clip starts/ends neutral. */
	UPROPERTY(EditAnywhere) TArray<FGuLiRecoilVATSample> Samples;
	/** UV payload IDs: one upper part, left/right housing and left/right barrel. */
	UPROPERTY(EditAnywhere) TArray<int32> UpperPartIds;
	UPROPERTY(EditAnywhere) TArray<int32> LeftGunPartIds;
	UPROPERTY(EditAnywhere) TArray<int32> RightGunPartIds;
	UPROPERTY(EditAnywhere) FString SourceSHA256;
	UFUNCTION(BlueprintPure, Category="Animation|Recoil") bool IsValidDefinition() const;
	UFUNCTION(BlueprintPure, Category="Animation|Recoil") TArray<FString> ValidateImportedTextures() const;
	UFUNCTION(BlueprintPure, Category="Animation|Recoil") static UGuLiRecoilVATDefinition* GetMeshDefinition(UStaticMesh* Mesh);
	UFUNCTION(BlueprintCallable, Category="Animation|Recoil|Authoring") bool BindMesh(UStaticMesh* Mesh);
	float Frame(int32 Side, double Age) const;
	float MissileFrame(const FGuLiMechanicalAnimationState& State, double Time) const;
	void NotifyMissileShot(FGuLiMechanicalAnimationState& State, double Time) const;
	FGuLiRecoilVATSample Sample(float Frame) const;
	FGuLiRecoilVATSample Evaluate(const FGuLiMechanicalAnimationState& State, double Time) const;
	void ConfigureMaterial(UMaterialInstanceDynamic& Material) const;
};

/** Strong ownership on the existing mesh; no per-unit UObject or runtime skeleton. */
UCLASS()
class GULISTRIKE_API UGuLiRecoilVATBinding final : public UAssetUserData
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere) TObjectPtr<UGuLiRecoilVATDefinition> Definition;
};

struct FGuLiRecoilVATPlayback
{
	float Current[2] = {}, Previous[2] = {};
	float MissileCurrent = 0, MissilePrevious = 0;
};

namespace GuLiRecoilVAT
{
	// Existing mechanical 0..50 and locomotion VAT 51..62 remain unchanged.
	inline constexpr int32 FirstCustomData = 63;
	inline constexpr int32 CustomDataFloatCount = 69;
	GULISTRIKE_API const UGuLiRecoilVATDefinition* Find(const UStaticMesh* Mesh);
	GULISTRIKE_API void ConfigureBounds(UInstancedStaticMeshComponent& Component);
	GULISTRIKE_API void Update(const UGuLiRecoilVATDefinition& D, const FGuLiMechanicalAnimationState& State,
		double Time, bool Reset, FGuLiRecoilVATPlayback& Playback);
	GULISTRIKE_API void Write(UInstancedStaticMeshComponent& Component, int32 Index, const FGuLiRecoilVATPlayback& Playback, bool Reset=false);
}
