// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Gameplay/Building/GuLiBuildingTypes.h"
#include "GuLiBuildingPlacementComponent.generated.h"

class AGuLiBuildingPlacementPreview;
class APlayerController;
class UGuLiBuildingCatalog;
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

	UGuLiBuildingCatalog* GetBuildingCatalog() const { return CachedCatalog; }

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
	void UpdatePlacementPreview(bool bWorldInputBlockedByUI);

	FGuLiBuildingFeedback OnBuildingFeedback;

#if WITH_DEV_AUTOMATION_TESTS
	void TestOnly_SetCatalog(UGuLiBuildingCatalog* Catalog);
	void TestOnly_BypassConnectionGate(bool bBypass) { bTestBypassConnectionGate = bBypass; }
	FGuLiBuildingPlacementResult TestOnly_ProcessServerRequest(
		const FGuLiBuildingPlacementRequest& Request);
	void TestOnly_ResetServerRequestState();
#endif

#if WITH_EDITOR
	/** Editor-only two-client smoke entry; submits through the ordinary reliable RPC. */
	bool EditorQASubmitPlacement(
		EGuLiBuildingType Type,
		const FVector& GroundLocation,
		float YawDegrees);
#endif

private:
	APlayerController* GetOwningPlayerController() const;
	UGuLiBuildingCatalog* ResolveCatalog();
	bool CanLocallyBuild(EGuLiBuildingPlacementRejectReason& OutReason) const;
	bool EnterBuildMode();
	void ExitBuildMode(bool bEmitMessage);
	void DestroyPreview();
	bool EnsurePreviewForDefinition(const FGuLiBuildingDefinition& Definition);

	bool TraceLocalGround(FHitResult& OutHit, float& OutYawDegrees) const;
	bool TraceServerGround(const FVector& DesiredLocation, FHitResult& OutHit) const;
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

	EGuLiBuildingPlacementRejectReason ValidateLocalCandidate(
		const FGuLiBuildingDefinition& Definition,
		const FHitResult& GroundHit,
		float YawDegrees) const;
	EGuLiBuildingPlacementRejectReason ValidateServerRequest(
		const FGuLiBuildingPlacementRequest& Request,
		FTransform& OutSpawnTransform,
		const FGuLiBuildingDefinition*& OutDefinition, AActor*& OutSupportingActor) const;
	void CountPlacedBuildings(const FGuid& BuilderGuid, int32& OutBuilderCount, int32& OutWorldCount) const;
	bool ConsumeServerRequestBudget();
	FGuLiBuildingPlacementResult ProcessServerRequest(
		const FGuLiBuildingPlacementRequest& Request,
		bool bSendResult);
	void SendPlacementResult(const FGuLiBuildingPlacementResult& Result);

	void EmitSelectionFeedback();
	void EmitRejectedFeedback(EGuLiBuildingPlacementRejectReason Reason);
	void EmitFeedback(const FText& Message, EGuLiBuildingFeedbackTone Tone);
	FText GetSelectedDisplayName() const;

	UFUNCTION(Server, Reliable)
	void ServerRequestPlacement(const FGuLiBuildingPlacementRequest& Request);

	UFUNCTION(Client, Reliable)
	void ClientReceivePlacementResult(const FGuLiBuildingPlacementResult& Result);

	UPROPERTY(Transient)
	TObjectPtr<UGuLiBuildingCatalog> CachedCatalog;

	UPROPERTY(Transient)
	TObjectPtr<AGuLiBuildingPlacementPreview> PreviewActor;

	EGuLiBuildingType SelectedBuildingType = EGuLiBuildingType::MissileTurret;
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
