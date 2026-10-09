#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Battle/Network/GuLiBattleTypes.h"
#include "GuLiModelAuthoringLibrary.generated.h"

class AActor;
class APlayerController;

/** Editor-only paint encoding; restricted to independent Review assets. Never changes model topology or UVs. */
UCLASS()
class GULISTRIKE_API UGuLiModelAuthoringLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Models|Authoring")
	static bool EncodeCandidateMesh(UObject* CandidateMesh, int32 LODIndex, const FString& RegionFile, FString& Error);
	/** Whole components with one fixed paint role need no correspondence to a triangulated preview. */
	UFUNCTION(BlueprintCallable, Category="Models|Authoring")
	static bool EncodeUniformCandidateMesh(UObject* CandidateMesh, int32 LODIndex, int32 PaintRole, FLinearColor PaintColor, FString& Error);
	/** Python retains diagnostics on failure instead of the bool/out None convention. */
	UFUNCTION(BlueprintCallable, Category="Models|Authoring")
	static FString EncodeCandidateMeshReport(UObject* CandidateMesh, int32 LODIndex, const FString& RegionFile);
	UFUNCTION(BlueprintCallable, Category="Models|Authoring")
	static FString EncodeUniformCandidateMeshReport(UObject* CandidateMesh, int32 LODIndex, int32 PaintRole, FLinearColor PaintColor);
	/** Only existing ship/Ground CDO caches, populated from the model catalogue. */
	UFUNCTION(BlueprintCallable, Category="Models|Authoring")
	static FString SetCompatibilityModelId(UObject* TargetCDO, FName PropertyName, int32 ModelId);
	/** All mesh-description attributes except color, plus the reference skeleton. */
	UFUNCTION(BlueprintCallable, Category="Models|Authoring")
	static FString GetMeshInvariantSnapshot(UObject* Mesh);
	/** Reconcile editor bounds with the built display geometry after a color-only commit. */
	UFUNCTION(BlueprintCallable, Category="Models|Authoring")
	static FString RefreshPaintDisplayBounds(UObject* Mesh);
	/** Read-only geometry export for exact original triangle correspondence. */
	UFUNCTION(BlueprintCallable, Category="Models|Authoring")
	static FString WriteMeshPaintGeometry(UObject* Mesh, int32 LODIndex, const FString& FilePath, bool RenderedStaticLOD = false);
	/** Color-only writes to existing skeletal LOD vertices, without running mesh reduction again. */
	UFUNCTION(BlueprintCallable, Category="Models|Authoring")
	static FString EncodeSkeletalRenderPaint(UObject* CandidateMesh, int32 LODIndex, const FString& RegionFile);
	/** Indexed sidecar checks every position and protected material slot. */
	UFUNCTION(BlueprintCallable, Category="Models|Authoring")
	static FString EncodeIndexedMeshPaint(UObject* CandidateMesh, int32 LODIndex, const FString& RegionFile);
	/** Cache the existing display LOD as editable source so painting cannot re-run legacy reduction. */
	UFUNCTION(BlueprintCallable, Category="Models|Authoring")
	static FString PreserveDisplayLODForPaint(UObject* CandidateMesh, int32 LODIndex);
	/** Explicit live visual inspection only; transient fixtures cannot enter saved maps. */
	UFUNCTION(BlueprintCallable, Category="Models|Authoring", meta=(WorldContext="WorldContext"))
	static AActor* SpawnRuntimePaintSample(UObject* WorldContext, int32 ModelId, EGuLiTeam Team, FVector Location, float Scale = 1.f);
	/** Explicit inspection captures the selected local viewport, including its real Slate UI. */
	UFUNCTION(BlueprintCallable, Category="Models|Authoring")
	static bool CaptureRuntimeViewport(APlayerController* Controller, const FString& FilePath);
};
