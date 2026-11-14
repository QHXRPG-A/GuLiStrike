#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "GuLiQuadrupedMinerPresentation.generated.h"

class UTexture2D;
class UNiagaraComponent;

USTRUCT(BlueprintType)
struct FGuLiMinerVATLOD
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere) TObjectPtr<UTexture2D> Position;
	UPROPERTY(EditAnywhere) TObjectPtr<UTexture2D> Normal;
	UPROPERTY(EditAnywhere) int32 VertexCount = 0;
	UPROPERTY(EditAnywhere) int32 NormalCount = 0;
	UPROPERTY(EditAnywhere) int32 PositionRows = 0;
	UPROPERTY(EditAnywhere) int32 NormalRows = 0;
};

/** Dense absolute vertex clips, independent of the five-direction combat VAT layout. */
UCLASS(BlueprintType)
class GULISTRIKE_API UGuLiQuadrupedMinerVATData final : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere) FString SourceSHA256;
	UPROPERTY(EditAnywhere) TArray<FGuLiMinerVATLOD> LODs;
	UPROPERTY(EditAnywhere) int32 TextureWidth = 2048;
	UPROPERTY(EditAnywhere) int32 TotalFrames = 103;
	UPROPERTY(EditAnywhere) int32 WalkFirstFrame = 1;
	UPROPERTY(EditAnywhere) int32 WalkFrameCount = 29;
	UPROPERTY(EditAnywhere) int32 MiningFirstFrame = 30;
	UPROPERTY(EditAnywhere) int32 MiningFrameCount = 73;
	UPROPERTY(EditAnywhere) float MiningDurationSeconds = 2.46f;
	/** Fixed presentation cadence, independent of travel distance and instance scale. */
	UPROPERTY(EditAnywhere) float WalkDurationSeconds = 28.f / 24.f;
	/** Rest-space travel per full cycle, including the 0.62 stance fraction. */
	UPROPERTY(EditAnywhere) float WalkCycleDistanceCm = 120.f / .62f;
	UPROPERTY(EditAnywhere) FVector ReferenceMuzzle = FVector::ZeroVector;
	UPROPERTY(EditAnywhere) TArray<FVector> MuzzleSamples;
	UPROPERTY(EditAnywhere) FBox RenderBounds = FBox(ForceInit);
	UPROPERTY(EditAnywhere) TObjectPtr<UMaterialInterface> HitMaterial;
	UPROPERTY(EditAnywhere) TObjectPtr<UMaterialInterface> WreckMaterial;
	UFUNCTION(BlueprintPure, Category="Mining|VAT") bool IsValidData() const;
	/** Scoped authoring entry; Python MeshDescription cannot write colors/normals. */
	UFUNCTION(BlueprintCallable, Category="Mining|Authoring")
	static FString BuildApprovedMesh(UStaticMesh* Mesh, const FString& SourceDirectory);
	FVector SampleMuzzle(float Frame) const;
};

/** Identifies the mesh's VAT-compatible feedback without changing other models. */
UCLASS()
class GULISTRIKE_API UGuLiMiningVATMeshComponent final : public UStaticMeshComponent
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, Category="Mining|VAT") TObjectPtr<UGuLiQuadrupedMinerVATData> Animation;
};

UCLASS(Blueprintable)
class GULISTRIKE_API AGuLiQuadrupedMinerPresentation final : public AActor
{
	GENERATED_BODY()
public:
	AGuLiQuadrupedMinerPresentation();
	virtual void BeginPlay() override;
	virtual void EndPlay(EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UGuLiMiningVATMeshComponent> BodyVAT;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly) TObjectPtr<UNiagaraComponent> MiningLaser;
	bool IsReady() const;
	FVector GetReferenceMuzzleWorld() const;
	FVector GetDisplayedMuzzleWorld() const;
	void NotifyDisplayedPose(bool bTeleport = false);
	void SetPresentationVisible(bool bVisible);
	void SetMiningState(bool bActive, const FVector& Target);
	void SetBeamVisible(bool bVisible);
	void RefreshBeam();
	UFUNCTION(BlueprintPure, Category="Mining|Diagnostics") FString GetVATDiagnosticsJson() const;
private:
	void WritePose(bool bResetHistory);
	void Wake();
	FVector LastPosition = FVector::ZeroVector, TargetWorld = FVector::ZeroVector;
	double LastTickTime = 0, LastPoseTime = 0, LastMotionTime = -1000;
	double WalkEpoch = 0, MiningEpoch = 0;
	float LastYaw = 0;
	bool bResetPoseHistory = true;
	float Phase = 0, Frame = 0, FromFrame = 0, Blend = 1, TransitionTime = .15f;
	float PreviousA = 0, PreviousB = 0, PreviousBlend = 1;
	uint8 Clip = 0; // 0 rest, 1 walk, 2 mining.
	bool bMining = false, bBeamVisible = false, bPresentationVisible = true, bInitialized = false;
};
