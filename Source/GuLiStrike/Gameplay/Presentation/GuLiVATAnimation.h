#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Gameplay/Presentation/GuLiMechanicalAnimation.h"
#include "GuLiVATAnimation.generated.h"

class UTexture2D;
class UInstancedStaticMeshComponent;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiVATClip
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) FName Name;
	UPROPERTY(EditAnywhere) int32 FirstFrame = 0;
	UPROPERTY(EditAnywhere) int32 FrameCount = 1;
	UPROPERTY(EditAnywhere) float DurationSeconds = 1;
	UPROPERTY(EditAnywhere) float StrideCentimeters = 0;
	UPROPERTY(EditAnywhere) bool bLoop = true;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiVATBone
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) FName Name;
	UPROPERTY(EditAnywhere) int32 ParentIndex = INDEX_NONE;
	UPROPERTY(EditAnywhere) bool bUpperAim = false;
	UPROPERTY(EditAnywhere) uint8 GunPitch = 0;
};

USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiVATMuzzle
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) int32 BoneIndex = INDEX_NONE;
	UPROPERTY(EditAnywhere) FVector ReferencePosition = FVector::ZeroVector;
	UPROPERTY(EditAnywhere) FVector ReferenceDirection = FVector::ForwardVector;
	UPROPERTY(EditAnywhere) FVector PitchPivot = FVector::ZeroVector;
};

/** Static vertices sample one shared animation atlas per LOD. No runtime skeleton. */
USTRUCT(BlueprintType)
struct GULISTRIKE_API FGuLiVertexVATLOD
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) TObjectPtr<UTexture2D> Position;
	UPROPERTY(EditAnywhere) TObjectPtr<UTexture2D> Rotation;
	UPROPERTY(EditAnywhere) int32 VertexCount = 0;
	UPROPERTY(EditAnywhere) int32 TextureWidth = 4096;
	UPROPERTY(EditAnywhere) int32 RowsPerFrame = 0;
	UPROPERTY(EditAnywhere) int32 FramesPerClip = 32;
	UPROPERTY(EditAnywhere) int32 TextureFrames = 0;
};

/** Legacy rigid VAT remains supported; vertex VAT carries no Bones/BoneDeltas. */
UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiVATDefinition : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere) TObjectPtr<UTexture2D> BonePosition;
	UPROPERTY(EditAnywhere) TObjectPtr<UTexture2D> BoneRotation;
	UPROPERTY(EditAnywhere) bool bVertexAnimation = false;
	UPROPERTY(EditAnywhere) TArray<FGuLiVertexVATLOD> VertexLODs;
	UPROPERTY(EditAnywhere) bool bDirectionalBlend = false;
	/** Names, reference pivots and rates are asset data; old Pioneer defaults remain unchanged. */
	UPROPERTY(EditAnywhere) FName UpperBoneName = TEXT("Top_M");
	UPROPERTY(EditAnywhere) FName PitchBoneName;
	UPROPERTY(EditAnywhere) FVector PitchPivot = FVector::ZeroVector;
	UPROPERTY(EditAnywhere) FVector PitchAxis = FVector(0,-1,0);
	UPROPERTY(EditAnywhere) float UpperTurnRateDegreesPerSecond = 180;
	UPROPERTY(EditAnywhere) float PitchTurnRateDegreesPerSecond = 90;
	UPROPERTY(EditAnywhere) float MinimumPitchDegrees = -80;
	UPROPERTY(EditAnywhere) float MaximumPitchDegrees = 80;
	UPROPERTY(EditAnywhere) int32 FramesPerSecond = 30;
	UPROPERTY(EditAnywhere) TArray<FGuLiVATBone> Bones;
	UPROPERTY(EditAnywhere) TArray<FGuLiVATClip> Clips;
	/** Frame-major, bone-minor. Cooked CPU copy is used only for muzzle and pivot sampling. */
	UPROPERTY(EditAnywhere) TArray<FTransform> BoneDeltas;
	UPROPERTY(EditAnywhere) TArray<FGuLiVATMuzzle> Muzzles;
	UPROPERTY(EditAnywhere) FVector UpperPivot = FVector::ZeroVector;
	UPROPERTY(EditAnywhere) FBox GameplayBounds = FBox(ForceInit);
	UPROPERTY(EditAnywhere) FBox RuntimeRenderBounds = FBox(ForceInit);
	UPROPERTY(EditAnywhere) TObjectPtr<UMaterialInterface> HitMaterial;
	UPROPERTY(EditAnywhere) TObjectPtr<UMaterialInterface> WreckMaterial;
	UPROPERTY(EditAnywhere) TObjectPtr<UMaterialInterface> PhaseMaterial;
	UFUNCTION(BlueprintPure, Category="VAT") bool IsValidDefinition() const;
	/** Source-pixel check after import; does not play the game or measure performance. */
	UFUNCTION(CallInEditor, Category="VAT") TArray<FString> ValidateImportedTextures() const;
	int32 FindClip(FName Name) const;
	float TextureFrame(uint8 Clip, float Phase) const;
	FTransform SampleBone(int32 Bone, float Frame) const;
};

struct GULISTRIKE_API FGuLiVATPlayback
{
	uint8 Clip = 0;
	float Phase = 0;
	/** Cycles/second; death clamps at the final frame. */
	float Rate = 1;
	bool bInitialized = false;
};

namespace GuLiVATAnimation
{
	// Keep all 51 legacy mechanical slots (including Hit at 0) unchanged.
	inline constexpr int32 FirstCustomData = 51;
	inline constexpr int32 CustomDataFloatCount = 63;
	GULISTRIKE_API void Step(const UGuLiVATDefinition& Definition, const FVector& Velocity,
		float BodyYaw, bool bAlive, float Dt, FGuLiVATPlayback& Playback);
	GULISTRIKE_API void StepAim(const UGuLiVATDefinition& Definition, const FGuLiVATPlayback& Playback,
		const FTransform& Root, const FVector* Target, float Dt, FGuLiMechanicalAnimationState& Aim, float TurnRateScale = 1.0f);
	GULISTRIKE_API bool ResolveMuzzle(const UGuLiVATDefinition& Definition, const FGuLiVATPlayback& Playback,
		const FGuLiMechanicalAnimationState& Aim, const FTransform& Root, int32 Side, FTransform& Out);
	GULISTRIKE_API void WriteInstance(UInstancedStaticMeshComponent& Component, int32 Index,
		const UGuLiVATDefinition& Definition, const FGuLiVATPlayback& Current, const FGuLiVATPlayback& Previous,
		const FGuLiMechanicalAnimationState& Aim, float BodyYaw, bool bReset,
		const FVector& Velocity = FVector::ZeroVector);
}
