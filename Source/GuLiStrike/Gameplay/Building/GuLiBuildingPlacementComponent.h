// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Building/GuLiBuildingTypes.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "GuLiBuildingPlacementComponent.generated.h"

class AGuLiBuildingPlacementPreview;
class APlayerController;
class APawn;
class AGuLiBattlePlayerState;
class UGuLiBuildingCatalog;
class UInputAction;
class UInputMappingContext;
struct FHitResult;

DECLARE_MULTICAST_DELEGATE_TwoParams(
	FGuLiBuildingFeedback,
	const FText&,
	EGuLiBuildingFeedbackTone);

/** Player-owned placement loop. Local prediction is cosmetic; authority validates and spawns. */
UCLASS(ClassGroup = (GuLiStrike), meta = (BlueprintSpawnableComponent))
class GULISTRIKE_API UGuLiBuildingPlacementComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGuLiBuildingPlacementComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION(BlueprintPure, Category = "Building")
	bool IsBuildModeActive() const { return bBuildModeActive; }

	UFUNCTION(BlueprintPure, Category = "Building")
	EGuLiBuildingType GetSelectedBuildingType() const { return SelectedBuildingType; }
	UFUNCTION(BlueprintPure, Category="Building")
	int32 GetSelectedBuildingDefinitionId() const;
	UFUNCTION(BlueprintCallable, Category="Building")
	bool SelectBuildingDefinition(int32 DefinitionId);

	UGuLiBuildingCatalog* GetBuildingCatalog() const { return CachedCatalog; }
	const AGuLiBuildingPlacementPreview* GetSceneUIPreview() const;

	/** Returns true whenever B was consumed, including an eligibility rejection. */
	bool ToggleBuildMode();
	/** Explicit catalog selection for commander UI; numeric keys remain control groups. */
	bool SelectBuildingType(EGuLiBuildingType Type);
	FText GetPlacementStatusText() const;
	/** Returns true only when an active build mode consumed the number key. */
	bool HandleNumberKey(int32 Number);
	/** Returns true whenever build mode consumed LMB. */
	bool HandlePrimaryAction(bool bWorldInputBlockedByUI);
	/** Returns true whenever build mode consumed RMB/Escape. */
	bool HandleCancelAction();
	void UpdatePlacementPreview(bool bWorldInputBlockedByUI, bool bForceValidation = false);
	void HandleRotateAction();

	FGuLiBuildingFeedback OnBuildingFeedback;
	bool ExecuteHybridPlacement(const FGuLiHybridCommand& Command,const FGuLiHybridCommandMember& Member);
	void AdvanceHybridPlacement();
	bool CaptureHybridPlacement(TArray<uint8>& Bytes) const;
	bool ValidateHybridPlacement(TConstArrayView<uint8> Bytes) const;
	bool RestoreHybridPlacement(TConstArrayView<uint8> Bytes);
	static bool ValidateAdmittedPlacement(UWorld& World,const FGuLiBuildingDefinition& Definition,
		const FVector& Ground,float Yaw,const FGuid& OriginalBuilder,FString* Rejection=nullptr);
	const FGuLiBuildingPlacementResult& GetLastPlacementReceipt() const { return LastPlacementReceipt; }
	uint64 GetPlacementReceiptCount() const { return PlacementReceiptCount; }
    TArray<FGuid> GetPendingPlacementEvents() const;
    void RebindHybridReceipts();
	uint64 GetCommittedPlacementCount() const { return CommittedPlacementCount; }
	FGuLiResourceAmounts GetCommittedPlacementCost() const { return CommittedPlacementCost; }

#if WITH_DEV_AUTOMATION_TESTS
	void TestOnly_SetCatalog(UGuLiBuildingCatalog* Catalog);
	void TestOnly_BypassConnectionGate(bool bBypass) { bTestBypassConnectionGate = bBypass; }

	void TestOnly_ResetServerRequestState();
#endif

#if WITH_EDITOR
	/** Editor-only two-client smoke entry; submits through the ordinary reliable RPC. */
	bool EditorQASubmitPlacement(
		EGuLiBuildingType Type,
		const FVector& GroundLocation,
		float YawDegrees);
	/** Query the unchanged production spatial policy; never spawns or changes the world. */
	bool EditorQAFindClearPlacement(EGuLiBuildingType Type,const FVector& Preferred,float YawDegrees,FVector& Ground);
	bool EditorQAFindClearDefinition(int32 DefinitionId,const FVector& Preferred,float YawDegrees,FVector& Ground);
	bool EditorQASubmitDefinition(int32 DefinitionId,const FVector& GroundLocation,float YawDegrees);
#endif

private:
	APlayerController* GetOwningPlayerController() const;
	APawn* GetPlacementPawn() const;
	AGuLiBattlePlayerState* GetPlacementPlayer() const;
	bool RouteHybridPlacement(const FGuLiBuildingPlacementRequest& Request);
	UFUNCTION() void HandleHybridTransaction(const FGuLiHybridTransactionResult& Result);
	void HandleHybridPlacementCommand(const FGuLiHybridCommandResult& Result);
	TMap<FGuid,uint32> HybridCommandRequests;
	TSet<FGuid> HybridRefundEvents;
	FGuLiBuildingPlacementResult LastPlacementReceipt;
	uint64 PlacementReceiptCount=0;
	uint64 CommittedPlacementCount=0;
	FGuLiResourceAmounts CommittedPlacementCost;
	struct FHybridPlacement
	{
		FGuid Command,AdmitEvent;
		FGuLiHybridObjectKey Source,Work;
		uint64 StartedAt=0;
		int32 Definition=0;
		FVector Ground=FVector::ZeroVector;
		float Yaw=0;
		bool bSubmitted=false;
	};
	TArray<FHybridPlacement> HybridPlacements;
	TMap<FGuid,uint32> HybridClientRequests;
	void ApplyHybridReceipt(const FGuLiHybridTransactionResult& Result);
	UGuLiBuildingCatalog* ResolveCatalog();
	bool CanLocallyBuild(EGuLiBuildingPlacementRejectReason& OutReason) const;
	bool EnterBuildMode();
	void ExitBuildMode(bool bEmitMessage);
	void DestroyPreview();
	void SetRotationInputEnabled(bool bEnabled);
	bool EnsurePreviewForDefinition(const FGuLiBuildingDefinition& Definition);

	bool TraceLocalGround(FHitResult& OutHit, float& OutYawDegrees) const;
	bool TracePlacementGround(const FVector& DesiredLocation, FHitResult& OutHit,int32 FootprintCells=0) const;
	bool IsLegalGroundHit(const FHitResult& Hit) const;
	bool IsLineOfSightClear(
		const FVector& GroundLocation,
		const AActor* SupportingActor) const;
	bool IsPlacementAreaClear(
		const FGuLiBuildingDefinition& Definition,
		const FVector& GroundLocation,
		float YawDegrees,
		const AActor* SupportingActor) const;
	bool DoesPlacementOverlapCommanderSoldier(
		const FVector& QueryCenter,
		const FVector& QueryExtent,
		float YawDegrees) const;
	EGuLiBuildingPlacementRejectReason ValidateSpatialCandidate(
		const FGuLiBuildingDefinition& Definition, const FHitResult& GroundHit, float YawDegrees) const;

	EGuLiBuildingPlacementRejectReason ValidateLocalCandidate(
		const FGuLiBuildingDefinition& Definition,
		const FHitResult& GroundHit,
		float YawDegrees) const;
	EGuLiBuildingPlacementRejectReason ValidatePlacementRequest(
		const FGuLiBuildingPlacementRequest& Request,
		FTransform& OutSpawnTransform,
		const FGuLiBuildingDefinition*& OutDefinition, AActor*& OutSupportingActor) const;
	void CountPlacedBuildings(const FGuid& BuilderGuid, int32& OutBuilderCount, int32& OutWorldCount) const;

	void SendPlacementResult(const FGuLiBuildingPlacementResult& Result);

	void EmitSelectionFeedback();
	void EmitRejectedFeedback(EGuLiBuildingPlacementRejectReason Reason);
	void EmitFeedback(const FText& Message, EGuLiBuildingFeedbackTone Tone);
	FText GetSelectedDisplayName() const;

	UFUNCTION()
	void SubmitPlacementRequest(const FGuLiBuildingPlacementRequest& Request);

	UFUNCTION()
	void ApplyPlacementResult(const FGuLiBuildingPlacementResult& Result);

	UPROPERTY(Transient)
	TObjectPtr<UGuLiBuildingCatalog> CachedCatalog;

	UPROPERTY(Transient)
	TObjectPtr<AGuLiBuildingPlacementPreview> PreviewActor;
	UPROPERTY(Transient) TObjectPtr<UInputAction> RotateAction;
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> RotationMappings;
	bool bRotationInputBound = false;
	double LastCandidateValidationTime = -1.0;
	TWeakObjectPtr<AActor> PreviewSupportingActor;

	EGuLiBuildingType SelectedBuildingType = EGuLiBuildingType::MissileTurret;
	int32 SelectedBuildingDefinitionId = 0;
	bool bBuildModeActive = false;
	bool bHasPreviewCandidate = false;
	FVector PreviewGroundLocation = FVector::ZeroVector;
	float PreviewYawDegrees = 0.0f;
	EGuLiBuildingPlacementRejectReason PreviewRejectReason =
		EGuLiBuildingPlacementRejectReason::NoGround;
	uint32 NextClientRequestId = 1u;

	bool bHasServerRequestHistory = false;
	FGuLiBuildingPlacementRequest LastServerRequest;
	FGuLiBuildingPlacementResult LastServerResult;
	TArray<double> ServerRequestTimes;
	/** Reused by local preview and authority checks; prevents a per-frame 500-entry allocation. */
	mutable TArray<FVector> SoldierLocationScratch;

#if WITH_DEV_AUTOMATION_TESTS
	bool bTestBypassConnectionGate = false;
#endif
};
