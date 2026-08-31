// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Presentation/GuLiCommanderPresentationActor.h"

#include "GuLiStrike.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Commander/Framework/GuLiCommanderPlayerState.h"
#include "Commander/Framework/GuLiCommanderGameState.h"
#include "Commander/Mass/GuLiCommanderMassFragments.h"
#include "Commander/Network/GuLiSoldierStateReplicator.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Tuning/GuLiRuntimeTuningTypes.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MassCommonFragments.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"

CSV_DEFINE_CATEGORY(GuLiCommanderPresentation, true);

namespace GuLiCommanderPresentation
{
	constexpr int32 MaximumPresentedSoldiers = 500;
	constexpr int32 MaximumBufferedPoseSamples = 8;
	constexpr float MaximumClockRoundTripMilliseconds = 500.0f;
	constexpr double MaximumForwardClockCorrectionSeconds = 0.025;
	constexpr float RingHeight = 35.0f;
	const FVector RingScale(12.0f, 12.0f, 0.02f);
	const FLinearColor SelectedColor(1.0f, 0.82f, 0.04f, 0.95f);
	const FLinearColor RedTeamColor(1.0f, 0.04f, 0.03f, 0.72f);
	const FLinearColor BlueTeamColor(0.02f, 0.28f, 1.0f, 0.72f);
	const FLinearColor UnassignedColor(0.5f, 0.5f, 0.5f, 0.55f);
	const FLinearColor WreckColor(0.22f, 0.0f, 0.0f, 0.88f);

	FLinearColor GetTeamColor(const EGuLiTeam Team)
	{
		switch (Team)
		{
		case EGuLiTeam::Red:
			return RedTeamColor;
		case EGuLiTeam::Blue:
			return BlueTeamColor;
		default:
			return UnassignedColor;
		}
	}

	FTransform MakeHiddenTransform()
	{
		return FTransform(FQuat::Identity, FVector::ZeroVector, FVector::ZeroVector);
	}

	bool AreTransformsEqual(const TArray<FTransform>& Lhs, const TArray<FTransform>& Rhs)
	{
		if (Lhs.Num() != Rhs.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < Lhs.Num(); ++Index)
		{
			if (!Lhs[Index].Equals(Rhs[Index], 0.01f))
			{
				return false;
			}
		}
		return true;
	}

	bool AreColorsEqual(const TArray<FLinearColor>& Lhs, const TArray<FLinearColor>& Rhs)
	{
		if (Lhs.Num() != Rhs.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < Lhs.Num(); ++Index)
		{
			if (!Lhs[Index].Equals(Rhs[Index], KINDA_SMALL_NUMBER))
			{
				return false;
			}
		}
		return true;
	}

	bool IsNewerSerial(const uint32 Candidate, const uint32 Baseline)
	{
		return static_cast<int32>(Candidate - Baseline) > 0;
	}

	// 收样本时估计服务器当前时间 = 样本服务器时间 + RTT/2；假定链路近似对称，调度/抖动仍会带来误差。
	double EstimateServerNowAtPoseReceipt(
		const double SampleServerTimeSeconds,
		const float RoundTripMilliseconds)
	{
		if (!FMath::IsFinite(SampleServerTimeSeconds))
		{
			return 0.0;
		}
		const float SanitizedRoundTripMilliseconds = FMath::IsFinite(RoundTripMilliseconds)
			? FMath::Clamp(RoundTripMilliseconds, 0.0f, MaximumClockRoundTripMilliseconds)
			: 0.0f;
		return SampleServerTimeSeconds
			+ static_cast<double>(SanitizedRoundTripMilliseconds) * 0.0005;
	}

	// 在本地帧推进上限幅修正时钟；不让估计时间倒退，避免插值回放。
	double AdvanceEstimatedServerTime(
		const double CurrentEstimateSeconds,
		const double LatestMeasuredServerNowSeconds,
		const float DeltaSeconds)
	{
		const double SafeDeltaSeconds = FMath::Max(0.0, static_cast<double>(DeltaSeconds));
		const double PredictedSeconds = CurrentEstimateSeconds + SafeDeltaSeconds;
		if (!FMath::IsFinite(LatestMeasuredServerNowSeconds))
		{
			return PredictedSeconds;
		}
		const double ErrorSeconds = LatestMeasuredServerNowSeconds - PredictedSeconds;
		const double MaximumBackwardCorrectionSeconds = FMath::Min(
			SafeDeltaSeconds,
			0.01 + SafeDeltaSeconds * 0.25);
		const double CorrectionSeconds = FMath::Clamp(
			ErrorSeconds * 0.25,
			-MaximumBackwardCorrectionSeconds,
			MaximumForwardClockCorrectionSeconds);
		return FMath::Max(CurrentEstimateSeconds, PredictedSeconds + CorrectionSeconds);
	}

	double CalculateRenderServerTime(
		const double EstimatedServerNowSeconds,
		const float BackTimeSeconds)
	{
		return EstimatedServerNowSeconds
			- static_cast<double>(FMath::Max(0.0f, BackTimeSeconds));
	}

	double ExtrapolateMeasuredServerNow(
		const double MeasuredServerNowAtReceiptSeconds,
		const double SecondsSinceReceipt)
	{
		return MeasuredServerNowAtReceiptSeconds
			+ FMath::Max(0.0, SecondsSinceReceipt);
	}

	// 优先连接统计中的有效 RTT，缺失时退回 PlayerState ping；毫秒/秒转换在此集中处理。
	float SelectClockRoundTripMilliseconds(
		const float ConnectionAverageLagSeconds,
		const double ConnectionRawPingSeconds,
		const float PlayerStateRoundTripMilliseconds,
		bool& bOutFromConnectionStats)
	{
		const double SanitizedAverageLagSeconds = FMath::IsFinite(ConnectionAverageLagSeconds)
			? FMath::Max(0.0, static_cast<double>(ConnectionAverageLagSeconds))
			: 0.0;
		const double SanitizedRawPingSeconds = FMath::IsFinite(ConnectionRawPingSeconds)
			? FMath::Max(0.0, ConnectionRawPingSeconds)
			: 0.0;
		const double SelectedConnectionRoundTripSeconds = SanitizedAverageLagSeconds > 0.0
			? SanitizedAverageLagSeconds
			: SanitizedRawPingSeconds;
		const float ConnectionRoundTripMilliseconds = static_cast<float>(
			SelectedConnectionRoundTripSeconds * 1000.0);
		if (ConnectionRoundTripMilliseconds > KINDA_SMALL_NUMBER)
		{
			bOutFromConnectionStats = true;
			return FMath::Min(ConnectionRoundTripMilliseconds, MaximumClockRoundTripMilliseconds);
		}

		bOutFromConnectionStats = false;
		return FMath::IsFinite(PlayerStateRoundTripMilliseconds)
			? FMath::Clamp(
				PlayerStateRoundTripMilliseconds,
				0.0f,
				MaximumClockRoundTripMilliseconds)
			: 0.0f;
	}

	FTransform MakePoseTransform(const FVector& Location, const float FacingYawDegrees)
	{
		return FTransform(
			FRotator(0.0f, FRotator::ClampAxis(FacingYawDegrees), 0.0f).Quaternion(),
			Location,
			FVector::OneVector);
	}
}

AGuLiCommanderPresentationActor::AGuLiCommanderPresentationActor()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	bNetLoadOnClient = true;
	// 只复制 Actor 的存在；每名士兵/ISM 的变换从独立姿态流本地重建。
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickInterval = 0.0f;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	UnitInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("UnitInstances"));
	UnitInstances->SetupAttachment(SceneRoot);
	UnitInstances->SetMobility(EComponentMobility::Movable);
	UnitInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UnitInstances->SetCanEverAffectNavigation(false);
	UnitInstances->SetIsReplicated(false);
	UnitInstances->SetCullDistances(
		FGuLiCommanderPresentationPerformanceSettings::DefaultUnitCullDistanceCentimeters,
		FGuLiCommanderPresentationPerformanceSettings::DefaultUnitCullDistanceCentimeters);
	UnitInstances->SetCastShadow(false);
	UnitInstances->SetAffectDistanceFieldLighting(false);
	UnitInstances->SetAffectDynamicIndirectLighting(false);
	UnitInstances->SetVisibleInRayTracing(false);

	RingInstances = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("RingInstances"));
	RingInstances->SetupAttachment(SceneRoot);
	RingInstances->SetMobility(EComponentMobility::Movable);
	RingInstances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	RingInstances->SetCanEverAffectNavigation(false);
	RingInstances->SetIsReplicated(false);
	RingInstances->SetCastShadow(false);
	RingInstances->SetAffectDistanceFieldLighting(false);
	RingInstances->SetAffectDynamicIndirectLighting(false);
	RingInstances->SetVisibleInRayTracing(false);
	RingInstances->SetCullDistances(0, 0);
	RingInstances->NumCustomDataFloats = 4;

	UnitMeshAsset = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
		TEXT("/Game/Commander/Units/SM_CommanderFourFRobot_Crowd.SM_CommanderFourFRobot_Crowd")));
	RingMeshAsset = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(
		TEXT("/Game/Commander/Units/SM_CommanderUnitRing.SM_CommanderUnitRing")));
	UnitMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
		TEXT("/Game/Commander/Units/M_CommanderUnitProxy.M_CommanderUnitProxy")));
	RingMaterialAsset = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(
		TEXT("/Game/Commander/UI/M_CommanderUnitRing.M_CommanderUnitRing")));
}

void AGuLiCommanderPresentationActor::BeginPlay()
{
	Super::BeginPlay();

	if (GetNetMode() == NM_DedicatedServer)
	{
		SetActorTickEnabled(false);
		return;
	}
	InitializePresentationPerformanceSettings();
	if (const UGuLiCommanderDataSubsystem* DataSubsystem = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>())
	{
		if (UStaticMesh* SoldierModel = DataSubsystem->GetDefaultSoldierDefinition().Model)
		{
			UnitMeshAsset = TSoftObjectPtr<UStaticMesh>(SoldierModel);
		}
	}

	ResolveSoftAssets();
	UnitInstances->PreAllocateInstancesMemory(GuLiCommanderPresentation::MaximumPresentedSoldiers);
	RingInstances->PreAllocateInstancesMemory(GuLiCommanderPresentation::MaximumPresentedSoldiers);
	EnsureClientMirrorArchetype();
	FindStateReplicator();
	FindLocalController();
	RebuildLocalInstances(0.0f);
}

TArray<FGuLiCommanderPresentationSettingView>
AGuLiCommanderPresentationActor::ListPresentationPerformanceSettings(const FString& Prefix) const
{
	return PerformanceSettingsRegistry.List(Prefix);
}

FGuLiCommanderPresentationSettingResult
AGuLiCommanderPresentationActor::GetPresentationPerformanceSetting(const FString& Key) const
{
	return PerformanceSettingsRegistry.Get(Key);
}

FGuLiCommanderPresentationSettingResult
AGuLiCommanderPresentationActor::ApplyLocalPresentationPerformanceOverride(
	const FString& Key,
	const FString& Value)
{
	FGuLiCommanderPresentationSettingResult Result = PerformanceSettingsRegistry.Set(Key, Value);
	if (Result.bSuccess)
	{
		ApplyPresentationPerformanceSettings();
		Result.AppliedActorCount = 1;
	}
	return Result;
}

TArray<FGuLiCommanderPresentationSettingResult>
AGuLiCommanderPresentationActor::ClearLocalPresentationPerformanceOverrides(
	const FString& KeyOrAll)
{
	TArray<FGuLiCommanderPresentationSettingResult> Results = PerformanceSettingsRegistry.Reset(KeyOrAll);
	bool bHasSuccess = false;
	for (FGuLiCommanderPresentationSettingResult& Result : Results)
	{
		if (Result.bSuccess)
		{
			Result.AppliedActorCount = 1;
			bHasSuccess = true;
		}
	}
	if (bHasSuccess)
	{
		ApplyPresentationPerformanceSettings();
	}
	return Results;
}

void AGuLiCommanderPresentationActor::InitializePresentationPerformanceSettings()
{
	FGuLiCommanderPresentationRawConfigSettings ConfigSettings;
	const FString ConfigSection = GetClass()->GetPathName();
	const auto ReadRawConfig = [&ConfigSection](
		const FName PropertyName,
		TOptional<FString>& OutRawValue)
	{
		FString RawValue;
		if (GConfig
			&& GConfig->GetString(
				*ConfigSection,
				*PropertyName.ToString(),
				RawValue,
				GGameIni))
		{
			OutRawValue = MoveTemp(RawValue);
		}
	};

	ReadRawConfig(
		GET_MEMBER_NAME_CHECKED(
			AGuLiCommanderPresentationActor,
			UnitCullDistanceCentimeters),
		ConfigSettings.UnitCullDistanceCentimeters);
	ReadRawConfig(
		GET_MEMBER_NAME_CHECKED(
			AGuLiCommanderPresentationActor,
			RingCullDistanceCentimeters),
		ConfigSettings.RingCullDistanceCentimeters);
	ReadRawConfig(
		GET_MEMBER_NAME_CHECKED(AGuLiCommanderPresentationActor, bUnitCastShadow),
		ConfigSettings.bUnitCastShadow);
	ReadRawConfig(
		GET_MEMBER_NAME_CHECKED(
			AGuLiCommanderPresentationActor,
			bUnitAffectDistanceFieldLighting),
		ConfigSettings.bUnitAffectDistanceFieldLighting);
	ReadRawConfig(
		GET_MEMBER_NAME_CHECKED(
			AGuLiCommanderPresentationActor,
			bUnitAffectDynamicIndirectLighting),
		ConfigSettings.bUnitAffectDynamicIndirectLighting);
	ReadRawConfig(
		GET_MEMBER_NAME_CHECKED(
			AGuLiCommanderPresentationActor,
			bUnitVisibleInRayTracing),
		ConfigSettings.bUnitVisibleInRayTracing);

	TArray<FString> ValidationErrors;
	PerformanceSettingsRegistry.InitializeFromRawConfig(ConfigSettings, ValidationErrors);
	if (!ValidationErrors.IsEmpty() && !bLoggedInvalidPerformanceConfig)
	{
		UE_LOG(
			LogGuLiStrike,
			Error,
			TEXT("Commander presentation Config rejected; invalid keys use C++ defaults: %s"),
			*FString::Join(ValidationErrors, TEXT("; ")));
		bLoggedInvalidPerformanceConfig = true;
	}
	ApplyPresentationPerformanceSettings();
}

void AGuLiCommanderPresentationActor::ApplyPresentationPerformanceSettings()
{
	if (!UnitInstances || !RingInstances)
	{
		return;
	}

	const FGuLiCommanderPresentationPerformanceSettings Effective = PerformanceSettingsRegistry.GetEffectiveSettings();
	UnitInstances->SetCullDistances(
		Effective.UnitCullDistanceCentimeters,
		Effective.UnitCullDistanceCentimeters);
	UnitInstances->SetCastShadow(Effective.bUnitCastShadow);
	UnitInstances->SetAffectDistanceFieldLighting(
		Effective.bUnitAffectDistanceFieldLighting);
	UnitInstances->SetAffectDynamicIndirectLighting(
		Effective.bUnitAffectDynamicIndirectLighting);
	UnitInstances->SetVisibleInRayTracing(Effective.bUnitVisibleInRayTracing);

	RingInstances->SetCullDistances(
		Effective.RingCullDistanceCentimeters,
		Effective.RingCullDistanceCentimeters);
	RingInstances->SetCastShadow(false);
	RingInstances->SetAffectDistanceFieldLighting(false);
	RingInstances->SetAffectDynamicIndirectLighting(false);
	RingInstances->SetVisibleInRayTracing(false);
}

void AGuLiCommanderPresentationActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	DestroyClientMirrorEntities();
	Super::EndPlay(EndPlayReason);
}

void AGuLiCommanderPresentationActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	RebuildLocalInstances(DeltaSeconds);
}

bool AGuLiCommanderPresentationActor::TryGetPresentedSoldierTransform(
	const FGuLiSoldierId SoldierId,
	FTransform& OutTransform) const
{
	const FGuLiCommanderPresentedSoldier* Soldier = PresentedSoldiers.Find(SoldierId);
	if (!SoldierId.IsValid() || !Soldier || !Soldier->bHasPresentedTransform)
	{
		return false;
	}

	OutTransform = Soldier->PresentedTransform;
	return true;
}

// 预测距离由已发布速度、持续时间和上限计算；不在客户端执行权威导航或写 Authority。
void AGuLiCommanderPresentationActor::BeginPredictedMove(
	const FGuLiCommanderSelectionState& Selection,
	const FVector& Target,
	const uint32 ClientCommandId)
{
	if (ClientCommandId == 0u || Target.ContainsNaN() || !GetWorld())
	{
		return;
	}

	AGuLiSoldierStateReplicator* Replicator = FindStateReplicator();
	if (!Replicator)
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	float EffectiveMoveSpeedCmPerSecond = 0.0f;
	if (const AGuLiCommanderGameState* GameState = GetWorld()->GetGameState<AGuLiCommanderGameState>())
	{
		EffectiveMoveSpeedCmPerSecond = GameState->GetEffectiveSoldierMoveSpeedCmPerSecond();
	}
	if (EffectiveMoveSpeedCmPerSecond <= 0.0f)
	{
		if (const UGuLiCommanderDataSubsystem* DataSubsystem = GetWorld()->GetSubsystem<UGuLiCommanderDataSubsystem>())
		{
			EffectiveMoveSpeedCmPerSecond = DataSubsystem
				->GetDefaultSoldierDefinition().MovementSpeedCmPerSecond;
		}
	}
	const float EffectivePredictionDistance = GuLiRuntimeTuning::CalculatePredictionDistance(
		EffectiveMoveSpeedCmPerSecond,
		PredictionDurationSeconds,
		MaximumPredictionDistanceCentimeters);
	TSet<FGuLiSoldierId> AddedSoldiers;
	for (const FGuLiControlCohortDescriptor& Cohort : Selection.Cohorts)
	{
		if (!Cohort.CohortId.IsValid())
		{
			continue;
		}

		for (const FGuLiSoldierId SoldierId : Cohort.MemberIds)
		{
			if (!SoldierId.IsValid() || AddedSoldiers.Contains(SoldierId))
			{
				continue;
			}
			AddedSoldiers.Add(SoldierId);

			const FGuLiSoldierStateItem* ReliableState = Replicator->FindSoldierState(SoldierId);
			const FGuLiCommanderPresentedSoldier* Presented = PresentedSoldiers.Find(SoldierId);
			if (!ReliableState || !ReliableState->IsAlive()
				|| !Presented || !Presented->bHasPresentedTransform)
			{
				continue;
			}

			const FVector ToTarget = Target - Presented->PresentedTransform.GetLocation();
			const float Distance = static_cast<float>(ToTarget.Size2D());
			if (Distance <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			FGuLiCommanderPredictedMove& Prediction = PredictedMoves.FindOrAdd(SoldierId);
			Prediction = FGuLiCommanderPredictedMove{};
			Prediction.CohortId = Cohort.CohortId;
			Prediction.ClientCommandId = ClientCommandId;
			Prediction.StartTimeSeconds = Now;
			Prediction.Direction = FVector(ToTarget.X, ToTarget.Y, 0.0).GetSafeNormal();
			Prediction.MaximumDistance = FMath::Min(Distance, EffectivePredictionDistance);
			Prediction.TargetYawDegrees = Prediction.Direction.Rotation().Yaw;
		}
	}
}

// 总体部分接受时必须看本组结果；成功组记录批次号，失败组开始撤销本地偏移。
void AGuLiCommanderPresentationActor::ResolvePredictedMove(const FGuLiCommandAck& Ack)
{
	if (Ack.CommandKind != EGuLiCommandKind::Move
		|| Ack.ClientCommandId == 0u || !GetWorld())
	{
		return;
	}

	const double Now = GetWorld()->GetTimeSeconds();
	for (TPair<FGuLiSoldierId, FGuLiCommanderPredictedMove>& Pair : PredictedMoves)
	{
		FGuLiCommanderPredictedMove& Prediction = Pair.Value;
		if (Prediction.ClientCommandId != Ack.ClientCommandId)
		{
			continue;
		}

		bool bAccepted = Ack.Result == EGuLiCommandAckResult::Accepted
			&& Ack.BatchOrderId != 0u;
		if (!Ack.CohortResults.IsEmpty())
		{
			bAccepted = false;
			if (const FGuLiCohortCommandAck* CohortResult = Ack.CohortResults.FindByPredicate(
				[&Prediction](const FGuLiCohortCommandAck& Candidate)
				{
					return Candidate.CohortId == Prediction.CohortId;
				}))
			{
				bAccepted = IsAckResultAccepted(CohortResult->Result) && Ack.BatchOrderId != 0u;
			}
		}

		if (bAccepted)
		{
			Prediction.ExpectedOrderId = Ack.BatchOrderId;
			Prediction.bAwaitingAuthoritativeOrder = true;
		}
		else
		{
			BeginPredictionResolution(Prediction, Now);
		}
	}
}

void AGuLiCommanderPresentationActor::ResetSoldierPresentationDiagnostics(
	const FGuLiSoldierId SoldierId)
{
	if (FGuLiCommanderPresentedSoldier* Soldier = PresentedSoldiers.Find(SoldierId))
	{
		Soldier->LastPoseReceiptLocalTimeSeconds = 0.0;
		Soldier->MaximumPoseReceiptGapSeconds = 0.0;
		Soldier->LastUntaggedHardSnapDelta = FVector::ZeroVector;
		Soldier->LastHardSnapPriorSampleDelta = FVector::ZeroVector;
		Soldier->LastHardSnapSampleVelocity = FVector::ZeroVector;
		Soldier->LastHardSnapCurrentAnchor = FVector::ZeroVector;
		Soldier->LastHardSnapCurrentRelative = FVector::ZeroVector;
		Soldier->LastHardSnapPreviousAnchor = FVector::ZeroVector;
		Soldier->LastHardSnapPreviousRelative = FVector::ZeroVector;
		Soldier->LastHardSnapServerTimeGapSeconds = 0.0;
		Soldier->LastHardSnapFrameGap = 0u;
		Soldier->LastHardSnapCurrentChunkIndex = 0u;
		Soldier->LastHardSnapPreviousChunkIndex = 0u;
		Soldier->LastHardSnapCurrentSampleIndex = 0u;
		Soldier->LastHardSnapPreviousSampleIndex = 0u;
		Soldier->UntaggedHardSnapCount = 0u;
		Soldier->TeleportSnapCount = 0u;
	}
}

bool AGuLiCommanderPresentationActor::TryGetSoldierPresentationDiagnostics(
	const FGuLiSoldierId SoldierId,
	FGuLiCommanderSoldierPresentationDiagnostics& OutDiagnostics) const
{
	const FGuLiCommanderPresentedSoldier* Soldier = PresentedSoldiers.Find(SoldierId);
	if (!SoldierId.IsValid() || !Soldier)
	{
		return false;
	}

	OutDiagnostics.MaximumPoseReceiptGapSeconds = Soldier->MaximumPoseReceiptGapSeconds;
	OutDiagnostics.LastUntaggedHardSnapDelta = Soldier->LastUntaggedHardSnapDelta;
	OutDiagnostics.LastHardSnapPriorSampleDelta = Soldier->LastHardSnapPriorSampleDelta;
	OutDiagnostics.LastHardSnapSampleVelocity = Soldier->LastHardSnapSampleVelocity;
	OutDiagnostics.LastHardSnapCurrentAnchor = Soldier->LastHardSnapCurrentAnchor;
	OutDiagnostics.LastHardSnapCurrentRelative = Soldier->LastHardSnapCurrentRelative;
	OutDiagnostics.LastHardSnapPreviousAnchor = Soldier->LastHardSnapPreviousAnchor;
	OutDiagnostics.LastHardSnapPreviousRelative = Soldier->LastHardSnapPreviousRelative;
	OutDiagnostics.LastHardSnapServerTimeGapSeconds = Soldier->LastHardSnapServerTimeGapSeconds;
	OutDiagnostics.LastHardSnapFrameGap = Soldier->LastHardSnapFrameGap;
	OutDiagnostics.LastHardSnapCurrentChunkIndex = Soldier->LastHardSnapCurrentChunkIndex;
	OutDiagnostics.LastHardSnapPreviousChunkIndex = Soldier->LastHardSnapPreviousChunkIndex;
	OutDiagnostics.LastHardSnapCurrentSampleIndex = Soldier->LastHardSnapCurrentSampleIndex;
	OutDiagnostics.LastHardSnapPreviousSampleIndex = Soldier->LastHardSnapPreviousSampleIndex;
	OutDiagnostics.UntaggedHardSnapCount = Soldier->UntaggedHardSnapCount;
	OutDiagnostics.TeleportSnapCount = Soldier->TeleportSnapCount;
	return true;
}

#if WITH_DEV_AUTOMATION_TESTS
double AGuLiCommanderPresentationActor::TestOnly_EstimateServerNowAtPoseReceipt(
	const double SampleServerTimeSeconds,
	const float RoundTripMilliseconds)
{
	return GuLiCommanderPresentation::EstimateServerNowAtPoseReceipt(
		SampleServerTimeSeconds,
		RoundTripMilliseconds);
}

double AGuLiCommanderPresentationActor::TestOnly_CalculateRenderServerTime(
	const double EstimatedServerNowSeconds,
	const float BackTimeSeconds)
{
	return GuLiCommanderPresentation::CalculateRenderServerTime(
		EstimatedServerNowSeconds,
		BackTimeSeconds);
}

double AGuLiCommanderPresentationActor::TestOnly_AdvanceEstimatedServerTime(
	const double CurrentEstimateSeconds,
	const double MeasuredServerNowAtReceiptSeconds,
	const double SecondsSinceReceipt,
	const float DeltaSeconds)
{
	return GuLiCommanderPresentation::AdvanceEstimatedServerTime(
		CurrentEstimateSeconds,
		GuLiCommanderPresentation::ExtrapolateMeasuredServerNow(
			MeasuredServerNowAtReceiptSeconds,
			SecondsSinceReceipt),
		DeltaSeconds);
}

float AGuLiCommanderPresentationActor::TestOnly_SelectClockRoundTripMilliseconds(
	const float ConnectionAverageLagSeconds,
	const double ConnectionRawPingSeconds,
	const float PlayerStateRoundTripMilliseconds,
	bool& bOutFromConnectionStats)
{
	return GuLiCommanderPresentation::SelectClockRoundTripMilliseconds(
		ConnectionAverageLagSeconds,
		ConnectionRawPingSeconds,
		PlayerStateRoundTripMilliseconds,
		bOutFromConnectionStats);
}
#endif

void AGuLiCommanderPresentationActor::ResolveSoftAssets()
{
	if (UStaticMesh* UnitMesh = UnitMeshAsset.LoadSynchronous())
	{
		UnitInstances->SetStaticMesh(UnitMesh);
		UnitInstances->EmptyOverrideMaterials();
	}
	else
	{
		UE_LOG(LogGuLiStrike, Error, TEXT("Commander presentation could not load unit proxy %s."), *UnitMeshAsset.ToString());
	}

	if (UStaticMesh* RingMesh = RingMeshAsset.LoadSynchronous())
	{
		RingInstances->SetStaticMesh(RingMesh);
	}
	else
	{
		UE_LOG(LogGuLiStrike, Error, TEXT("Commander presentation could not load ring mesh %s."), *RingMeshAsset.ToString());
	}

	const int32 UnitMaterialSlotCount = FMath::Max(1, UnitInstances->GetNumMaterials());
	int32 MissingUnitMaterialSlotCount = 0;
	for (int32 MaterialIndex = 0; MaterialIndex < UnitMaterialSlotCount; ++MaterialIndex)
	{
		if (!UnitInstances->GetMaterial(MaterialIndex))
		{
			++MissingUnitMaterialSlotCount;
		}
	}

	// The generated FourFRobot static proxy preserves all source skeletal-mesh
	// materials. Keep those mesh defaults and load the inexpensive proxy only as
	// a safety fallback when an actually empty slot exists.
	if (MissingUnitMaterialSlotCount > 0)
	{
		if (UMaterialInterface* UnitMaterial = UnitMaterialAsset.LoadSynchronous())
		{
			for (int32 MaterialIndex = 0; MaterialIndex < UnitMaterialSlotCount; ++MaterialIndex)
			{
				if (!UnitInstances->GetMaterial(MaterialIndex))
				{
					UnitInstances->SetMaterial(MaterialIndex, UnitMaterial);
				}
			}

			UE_LOG(
				LogGuLiStrike,
				Warning,
				TEXT("Commander unit proxy supplied fallback material for %d empty slot(s)."),
				MissingUnitMaterialSlotCount);
		}
		else
		{
			UE_LOG(LogGuLiStrike, Error, TEXT("Commander presentation could not load unit fallback material %s."), *UnitMaterialAsset.ToString());
		}
	}

	if (UMaterialInterface* RingMaterial = RingMaterialAsset.LoadSynchronous())
	{
		RingInstances->SetMaterial(0, RingMaterial);
	}
	else
	{
		UE_LOG(LogGuLiStrike, Error, TEXT("Commander presentation could not load ring material %s."), *RingMaterialAsset.ToString());
	}
}

AGuLiSoldierStateReplicator* AGuLiCommanderPresentationActor::FindStateReplicator()
{
	if (StateReplicator.IsValid())
	{
		return StateReplicator.Get();
	}

	if (!GetWorld())
	{
		return nullptr;
	}
	for (TActorIterator<AGuLiSoldierStateReplicator> It(GetWorld()); It; ++It)
	{
		StateReplicator = *It;
		return *It;
	}
	return nullptr;
}

AGuLiCommanderPlayerController* AGuLiCommanderPresentationActor::FindLocalController()
{
	if (LocalController.IsValid() && LocalController->IsLocalController())
	{
		return LocalController.Get();
	}

	LocalController.Reset();
	if (!GetWorld())
	{
		return nullptr;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (AGuLiCommanderPlayerController* Candidate = Cast<AGuLiCommanderPlayerController>(It->Get()))
		{
			if (Candidate->IsLocalController())
			{
				LocalController = Candidate;
				return Candidate;
			}
		}
	}
	return nullptr;
}

// 先对本帧收取的块排序，再逐块入样本缓存；同帧缺块不会阻塞已收到的其他士兵。
void AGuLiCommanderPresentationActor::ConsumePoseChunks(const double LocalNowSeconds)
{
	AGuLiCommanderPlayerController* Controller = FindLocalController();
	AGuLiSoldierStateReplicator* Replicator = FindStateReplicator();
	if (!Controller || !Replicator)
	{
		return;
	}

	UGuLiCommanderNetSyncComponent* NetSync = Controller->GetCommanderNetSyncComponent();
	if (!NetSync)
	{
		return;
	}

	TArray<FGuLiSoldierPoseChunk> Chunks;
	NetSync->ConsumePendingPoseChunks(Chunks);
	Chunks.Sort([](const FGuLiSoldierPoseChunk& Lhs, const FGuLiSoldierPoseChunk& Rhs)
	{
		if (!FMath::IsNearlyEqual(Lhs.ServerTimeSeconds, Rhs.ServerTimeSeconds))
		{
			return Lhs.ServerTimeSeconds < Rhs.ServerTimeSeconds;
		}
		if (Lhs.FrameSequence != Rhs.FrameSequence)
		{
			return Lhs.FrameSequence < Rhs.FrameSequence;
		}
		return Lhs.ChunkIndex < Rhs.ChunkIndex;
	});

	for (const FGuLiSoldierPoseChunk& Chunk : Chunks)
	{
		IngestPoseChunk(Chunk, LocalNowSeconds);
	}
}

// NetSync 已校验 Bootstrap 战局；这里进一步解压并绑定名册身份，未知 SoldierId 的姿态不能创建士兵。
void AGuLiCommanderPresentationActor::IngestPoseChunk(
	const FGuLiSoldierPoseChunk& Chunk,
	const double LocalNowSeconds)
{
	if (Chunk.ProtocolVersion != GULI_COMMANDER_PROTOCOL_VERSION
		|| Chunk.AuthorityEpoch == 0u
		|| Chunk.FrameSequence == 0u || Chunk.ChunkCount == 0u
		|| Chunk.ChunkIndex >= Chunk.ChunkCount || Chunk.Samples.IsEmpty())
	{
		return;
	}

	// NetSync has already admitted only the exact bootstrap MatchEpoch. Treat a
	// different non-zero token as a new identity, not as a numerically ordered epoch.
	if (CurrentAuthorityEpoch == 0u || Chunk.AuthorityEpoch != CurrentAuthorityEpoch)
	{
		CurrentAuthorityEpoch = Chunk.AuthorityEpoch;
		PredictedMoves.Reset();
		WreckExpireTimes.Reset();
		for (TPair<FGuLiSoldierId, FGuLiCommanderPresentedSoldier>& Pair : PresentedSoldiers)
		{
			Pair.Value.Samples.Reset();
			Pair.Value.bHasAuthoritativeTransform = false;
			Pair.Value.bHasPresentedTransform = false;
			Pair.Value.LastPoseReceiptLocalTimeSeconds = 0.0;
			Pair.Value.MaximumPoseReceiptGapSeconds = 0.0;
			Pair.Value.LastUntaggedHardSnapDelta = FVector::ZeroVector;
			Pair.Value.LastHardSnapPriorSampleDelta = FVector::ZeroVector;
			Pair.Value.LastHardSnapSampleVelocity = FVector::ZeroVector;
			Pair.Value.LastHardSnapCurrentAnchor = FVector::ZeroVector;
			Pair.Value.LastHardSnapCurrentRelative = FVector::ZeroVector;
			Pair.Value.LastHardSnapPreviousAnchor = FVector::ZeroVector;
			Pair.Value.LastHardSnapPreviousRelative = FVector::ZeroVector;
			Pair.Value.LastHardSnapServerTimeGapSeconds = 0.0;
			Pair.Value.LastHardSnapFrameGap = 0u;
			Pair.Value.LastHardSnapCurrentChunkIndex = 0u;
			Pair.Value.LastHardSnapPreviousChunkIndex = 0u;
			Pair.Value.LastHardSnapCurrentSampleIndex = 0u;
			Pair.Value.LastHardSnapPreviousSampleIndex = 0u;
			Pair.Value.UntaggedHardSnapCount = 0u;
			Pair.Value.TeleportSnapCount = 0u;
		}
		LatestMeasuredServerNowSeconds = 0.0;
		LatestClockMeasurementLocalTimeSeconds = 0.0;
		EstimatedServerTimeSeconds = 0.0;
		LatestClockRoundTripMilliseconds = 0.0f;
		LatestClockFrameSequence = 0u;
		bServerClockInitialized = false;
		bLatestClockRoundTripFromConnectionStats = false;
	}

	double ServerTimeSeconds = FMath::IsFinite(Chunk.ServerTimeSeconds)
		? static_cast<double>(Chunk.ServerTimeSeconds)
		: 0.0;
	if (ServerTimeSeconds <= 0.0 && Chunk.ServerSimTick > 0u)
	{
		ServerTimeSeconds = static_cast<double>(Chunk.ServerSimTick) / 30.0;
	}
	// 同捕获帧的后续块不会反复更新时钟测量，避免发送分摊时间被当成样本时间推进。
	const bool bAdvancesClockFrame = LatestClockFrameSequence == 0u
		|| GuLiCommanderPresentation::IsNewerSerial(
			Chunk.FrameSequence,
			LatestClockFrameSequence);
	if (!bServerClockInitialized || bAdvancesClockFrame)
	{
		float PlayerStateRoundTripMilliseconds = 0.0f;
		if (const AGuLiCommanderPlayerController* Controller = FindLocalController())
		{
			if (const AGuLiCommanderPlayerState* PlayerState = Controller->GetPlayerState<AGuLiCommanderPlayerState>())
			{
				PlayerStateRoundTripMilliseconds = PlayerState->GetPingInMilliseconds();
			}
		}
		float ConnectionAverageLagSeconds = 0.0f;
		double ConnectionRawPingSeconds = 0.0;
		if (const UNetDriver* NetDriver = GetWorld() ? GetWorld()->GetNetDriver() : nullptr)
		{
			if (const UNetConnection* Connection = NetDriver->ServerConnection)
			{
				ConnectionAverageLagSeconds = Connection->AvgLag;
				ConnectionRawPingSeconds = Connection->RawPingInSeconds;
			}
		}
		const float RoundTripMilliseconds = GuLiCommanderPresentation::SelectClockRoundTripMilliseconds(
				ConnectionAverageLagSeconds,
				ConnectionRawPingSeconds,
				PlayerStateRoundTripMilliseconds,
				bLatestClockRoundTripFromConnectionStats);
		LatestClockRoundTripMilliseconds = RoundTripMilliseconds;
		const double MeasuredServerNowSeconds = GuLiCommanderPresentation::EstimateServerNowAtPoseReceipt(
				ServerTimeSeconds,
				RoundTripMilliseconds);
		LatestMeasuredServerNowSeconds = MeasuredServerNowSeconds;
		LatestClockMeasurementLocalTimeSeconds = LocalNowSeconds;
		LatestClockFrameSequence = Chunk.FrameSequence;
		if (!bServerClockInitialized)
		{
			EstimatedServerTimeSeconds = MeasuredServerNowSeconds;
			bServerClockInitialized = true;
		}
	}

	AGuLiSoldierStateReplicator* Replicator = FindStateReplicator();
	if (!Replicator)
	{
		return;
	}
	for (int32 SampleIndex = 0; SampleIndex < Chunk.Samples.Num(); ++SampleIndex)
	{
		const FGuLiCompressedSoldierPose& CompressedPose = Chunk.Samples[SampleIndex];
		if (!CompressedPose.SoldierId.IsValid()
			|| !Replicator->FindSoldierState(CompressedPose.SoldierId))
		{
			// A lossy transform packet cannot create a reliable Soldier identity.
			continue;
		}

		FGuLiCommanderBufferedSoldierPose Sample;
		Sample.ServerTimeSeconds = ServerTimeSeconds;
		Sample.FrameSequence = Chunk.FrameSequence;
		Sample.ChunkAnchor = FVector(Chunk.Anchor);
		Sample.RelativeLocation = CompressedPose.GetRelativeLocationCentimeters();
		// 恢复世界位置；若出现大跳变，应同时检查锚点和相对偏移，而不只检查单个 int16。
		Sample.Location = Sample.ChunkAnchor + Sample.RelativeLocation;
		Sample.Velocity = CompressedPose.GetVelocityCentimetersPerSecond();
		Sample.FacingYawDegrees = GuLiCommanderProtocol::DequantizeYawDegrees(CompressedPose.FacingYaw);
		Sample.ActiveOrderId = CompressedPose.ActiveOrderId;
		Sample.ChunkIndex = Chunk.ChunkIndex;
		Sample.SampleIndex = static_cast<uint8>(SampleIndex);
		Sample.bTeleport = CompressedPose.IsTeleport();
		InsertPoseSample(CompressedPose.SoldierId, Sample, LocalNowSeconds);
	}
}

// 按单兵维护时间线：同帧替换、窗口内迟到可补洞，窗口外旧样本和历史瞬移不能让表现倒退。
void AGuLiCommanderPresentationActor::InsertPoseSample(
	const FGuLiSoldierId SoldierId,
	const FGuLiCommanderBufferedSoldierPose& Sample,
	const double LocalNowSeconds)
{
	FGuLiCommanderPresentedSoldier& Soldier = PresentedSoldiers.FindOrAdd(SoldierId);
	const int32 ExistingBeforeCorrection = Soldier.Samples.IndexOfByPredicate(
		[&Sample](const FGuLiCommanderBufferedSoldierPose& Existing)
		{
			return Existing.FrameSequence == Sample.FrameSequence;
		});
	const bool bHasLatestSample = !Soldier.Samples.IsEmpty();
	const bool bAdvancesLatest = !bHasLatestSample
		|| GuLiCommanderPresentation::IsNewerSerial(
			Sample.FrameSequence,
			Soldier.Samples.Last().FrameSequence);
	if (bAdvancesLatest)
	{
		if (Soldier.LastPoseReceiptLocalTimeSeconds > 0.0)
		{
			Soldier.MaximumPoseReceiptGapSeconds = FMath::Max(
				Soldier.MaximumPoseReceiptGapSeconds,
				FMath::Max(0.0, LocalNowSeconds - Soldier.LastPoseReceiptLocalTimeSeconds));
		}
		Soldier.LastPoseReceiptLocalTimeSeconds = LocalNowSeconds;
	}
	if (bHasLatestSample && !bAdvancesLatest && ExistingBeforeCorrection == INDEX_NONE)
	{
		if (Sample.bTeleport
			|| GuLiCommanderPresentation::IsNewerSerial(
				Soldier.Samples[0].FrameSequence,
				Sample.FrameSequence))
		{
			// A late packet may fill a gap inside the interpolation window, but it
			// can never rewind past the retained window or replay a historical teleport.
			return;
		}
	}

	// 仅最新帧可触发硬校正；显式瞬移与未标记的大误差分别计数，后者用于网络验收排错。
	const bool bCorrectionTooLarge = bAdvancesLatest && Soldier.bHasPresentedTransform
		&& FVector::DistSquared(
			Soldier.PresentedTransform.GetLocation(),
			Sample.Location) > FMath::Square(HardSnapDistanceCentimeters);
	if ((Sample.bTeleport && bAdvancesLatest) || bCorrectionTooLarge)
	{
		if (Sample.bTeleport && bAdvancesLatest)
		{
			++Soldier.TeleportSnapCount;
		}
		else if (bCorrectionTooLarge)
		{
			++Soldier.UntaggedHardSnapCount;
			Soldier.LastUntaggedHardSnapDelta = Sample.Location - Soldier.PresentedTransform.GetLocation();
			Soldier.LastHardSnapSampleVelocity = Sample.Velocity;
			Soldier.LastHardSnapCurrentAnchor = Sample.ChunkAnchor;
			Soldier.LastHardSnapCurrentRelative = Sample.RelativeLocation;
			Soldier.LastHardSnapCurrentChunkIndex = Sample.ChunkIndex;
			Soldier.LastHardSnapCurrentSampleIndex = Sample.SampleIndex;
			if (bHasLatestSample)
			{
				const FGuLiCommanderBufferedSoldierPose& PreviousLatest = Soldier.Samples.Last();
				Soldier.LastHardSnapPriorSampleDelta = Sample.Location - PreviousLatest.Location;
				Soldier.LastHardSnapPreviousAnchor = PreviousLatest.ChunkAnchor;
				Soldier.LastHardSnapPreviousRelative = PreviousLatest.RelativeLocation;
				Soldier.LastHardSnapPreviousChunkIndex = PreviousLatest.ChunkIndex;
				Soldier.LastHardSnapPreviousSampleIndex = PreviousLatest.SampleIndex;
				Soldier.LastHardSnapServerTimeGapSeconds = FMath::Max(
					0.0,
					Sample.ServerTimeSeconds - PreviousLatest.ServerTimeSeconds);
				Soldier.LastHardSnapFrameGap = Sample.FrameSequence - PreviousLatest.FrameSequence;
			}
		}
		Soldier.Samples.Reset();
		Soldier.AuthoritativeTransform = GuLiCommanderPresentation::MakePoseTransform(
			Sample.Location,
			Sample.FacingYawDegrees);
		Soldier.PresentedTransform = Soldier.AuthoritativeTransform;
		Soldier.bHasAuthoritativeTransform = true;
		Soldier.bHasPresentedTransform = true;
		PredictedMoves.Remove(SoldierId);
	}

	const int32 ExistingIndex = Soldier.Samples.IndexOfByPredicate(
		[&Sample](const FGuLiCommanderBufferedSoldierPose& Existing)
		{
			return Existing.FrameSequence == Sample.FrameSequence;
		});
	if (ExistingIndex != INDEX_NONE)
	{
		Soldier.Samples[ExistingIndex] = Sample;
	}
	else
	{
		int32 InsertIndex = 0;
		while (InsertIndex < Soldier.Samples.Num())
		{
			const FGuLiCommanderBufferedSoldierPose& Existing = Soldier.Samples[InsertIndex];
			if (Existing.ServerTimeSeconds > Sample.ServerTimeSeconds
				|| (FMath::IsNearlyEqual(Existing.ServerTimeSeconds, Sample.ServerTimeSeconds)
					&& Existing.FrameSequence > Sample.FrameSequence))
			{
				break;
			}
			++InsertIndex;
		}
		Soldier.Samples.Insert(Sample, InsertIndex);
	}

	while (Soldier.Samples.Num() > GuLiCommanderPresentation::MaximumBufferedPoseSamples)
	{
		Soldier.Samples.RemoveAt(0, 1, EAllowShrinking::No);
	}

	if (bAdvancesLatest)
	{
		if (FGuLiCommanderPredictedMove* Prediction = PredictedMoves.Find(SoldierId))
		{
			if (!Prediction->bResolving && Prediction->bAwaitingAuthoritativeOrder
				&& Prediction->ExpectedOrderId != 0u
				&& Sample.ActiveOrderId == Prediction->ExpectedOrderId)
			{
				BeginPredictionResolution(*Prediction, LocalNowSeconds);
			}
		}
	}
}

bool AGuLiCommanderPresentationActor::EvaluateAuthoritativeTransform(
	FGuLiCommanderPresentedSoldier& Soldier,
	const double RenderServerTimeSeconds,
	FTransform& OutTransform) const
{
	if (Soldier.Samples.IsEmpty())
	{
		if (Soldier.bHasAuthoritativeTransform)
		{
			OutTransform = Soldier.AuthoritativeTransform;
			return true;
		}
		return false;
	}

	const FGuLiCommanderBufferedSoldierPose& First = Soldier.Samples[0];
	if (Soldier.Samples.Num() == 1 || RenderServerTimeSeconds <= First.ServerTimeSeconds)
	{
		OutTransform = GuLiCommanderPresentation::MakePoseTransform(First.Location, First.FacingYawDegrees);
		return true;
	}

	for (int32 Index = 1; Index < Soldier.Samples.Num(); ++Index)
	{
		const FGuLiCommanderBufferedSoldierPose& Next = Soldier.Samples[Index];
		if (RenderServerTimeSeconds > Next.ServerTimeSeconds)
		{
			continue;
		}

		const FGuLiCommanderBufferedSoldierPose& Previous = Soldier.Samples[Index - 1];
		const double IntervalSeconds = Next.ServerTimeSeconds - Previous.ServerTimeSeconds;
		if (IntervalSeconds <= UE_DOUBLE_SMALL_NUMBER || Next.bTeleport)
		{
			OutTransform = GuLiCommanderPresentation::MakePoseTransform(
				Next.Location,
				Next.FacingYawDegrees);
			return true;
		}

		const float Alpha = static_cast<float>(FMath::Clamp(
			(RenderServerTimeSeconds - Previous.ServerTimeSeconds) / IntervalSeconds,
			0.0,
			1.0));
		// 以速度乘样本间隔作为切线做三次插值；结果异常时退回线性插值，朝向走最短角差。
		FVector InterpolatedLocation = FMath::CubicInterp(
			Previous.Location,
			Previous.Velocity * IntervalSeconds,
			Next.Location,
			Next.Velocity * IntervalSeconds,
			Alpha);
		if (InterpolatedLocation.ContainsNaN())
		{
			InterpolatedLocation = FMath::Lerp(Previous.Location, Next.Location, Alpha);
		}
		const float DeltaYaw = FMath::FindDeltaAngleDegrees(
			Previous.FacingYawDegrees,
			Next.FacingYawDegrees);
		const float InterpolatedYaw = Previous.FacingYawDegrees + DeltaYaw * Alpha;
		OutTransform = GuLiCommanderPresentation::MakePoseTransform(
			InterpolatedLocation,
			InterpolatedYaw);
		return true;
	}

	const FGuLiCommanderBufferedSoldierPose& Latest = Soldier.Samples.Last();
	// 越过最新样本只能按最后速度短时外推；不使用无限外推掩盖长时间断流。
	const double ExtrapolationSeconds = FMath::Clamp(
		RenderServerTimeSeconds - Latest.ServerTimeSeconds,
		0.0,
		static_cast<double>(MaximumExtrapolationSeconds));
	OutTransform = GuLiCommanderPresentation::MakePoseTransform(
		Latest.Location + Latest.Velocity * ExtrapolationSeconds,
		Latest.FacingYawDegrees);
	return true;
}

// 在样本求值结果上叠加本地偏移；到期或收到拒绝/对应命令样本后渐退，不改服务器状态。
void AGuLiCommanderPresentationActor::ApplyPrediction(
	const FGuLiSoldierId SoldierId,
	const double LocalNowSeconds,
	FTransform& InOutTransform)
{
	FGuLiCommanderPredictedMove* Prediction = PredictedMoves.Find(SoldierId);
	if (!Prediction)
	{
		return;
	}

	const float BaseYaw = InOutTransform.Rotator().Yaw;
	if (!Prediction->bResolving)
	{
		const double ElapsedSeconds = FMath::Max(0.0, LocalNowSeconds - Prediction->StartTimeSeconds);
		const float PredictionAlpha = PredictionDurationSeconds > UE_SMALL_NUMBER
			? static_cast<float>(FMath::Clamp(
				ElapsedSeconds / static_cast<double>(PredictionDurationSeconds),
				0.0,
				1.0))
			: 1.0f;
		const float SmoothedAlpha = FMath::SmoothStep(0.0f, 1.0f, PredictionAlpha);
		Prediction->LastAppliedOffset = Prediction->Direction * Prediction->MaximumDistance * SmoothedAlpha;
		Prediction->LastAppliedYawOffsetDegrees = FMath::FindDeltaAngleDegrees(
			BaseYaw,
			Prediction->TargetYawDegrees) * SmoothedAlpha;

		if (ElapsedSeconds >= static_cast<double>(PredictionDurationSeconds))
		{
			BeginPredictionResolution(*Prediction, LocalNowSeconds);
		}
	}

	FVector AppliedOffset = Prediction->LastAppliedOffset;
	float AppliedYawOffsetDegrees = Prediction->LastAppliedYawOffsetDegrees;
	if (Prediction->bResolving)
	{
		const double ResolveElapsed = FMath::Max(
			0.0,
			LocalNowSeconds - Prediction->ResolveStartTimeSeconds);
		const float ResolveAlpha = PredictionResolveSeconds > UE_SMALL_NUMBER
			? static_cast<float>(FMath::Clamp(
				ResolveElapsed / static_cast<double>(PredictionResolveSeconds),
				0.0,
				1.0))
			: 1.0f;
		const float Remaining = 1.0f - FMath::SmoothStep(0.0f, 1.0f, ResolveAlpha);
		AppliedOffset = Prediction->ResolveStartOffset * Remaining;
		AppliedYawOffsetDegrees = Prediction->ResolveStartYawOffsetDegrees * Remaining;
		if (ResolveAlpha >= 1.0f)
		{
			PredictedMoves.Remove(SoldierId);
			return;
		}
	}

	InOutTransform.AddToTranslation(AppliedOffset);
	InOutTransform.SetRotation(FRotator(
		0.0f,
		FRotator::ClampAxis(BaseYaw + AppliedYawOffsetDegrees),
		0.0f).Quaternion());
}

void AGuLiCommanderPresentationActor::BeginPredictionResolution(
	FGuLiCommanderPredictedMove& Prediction,
	const double LocalNowSeconds)
{
	if (Prediction.bResolving)
	{
		return;
	}
	Prediction.bResolving = true;
	Prediction.ResolveStartTimeSeconds = LocalNowSeconds;
	Prediction.ResolveStartOffset = Prediction.LastAppliedOffset;
	Prediction.ResolveStartYawOffsetDegrees = Prediction.LastAppliedYawOffsetDegrees;
}

bool AGuLiCommanderPresentationActor::EnsureClientMirrorArchetype()
{
	if (GetNetMode() == NM_DedicatedServer || !GetWorld())
	{
		return false;
	}
	if (ClientMirrorMassSubsystem.IsValid() && ClientMirrorArchetype.IsValid())
	{
		return true;
	}

	UMassEntitySubsystem* MassSubsystem = GetWorld()->GetSubsystem<UMassEntitySubsystem>();
	if (!MassSubsystem)
	{
		return false;
	}

	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	// 镜像没有导航/速度/移动目标等模拟 Fragment，Transform 由表现层唯一写入。
	const TArray<const UScriptStruct*> FragmentAndTagTypes = {
		FTransformFragment::StaticStruct(),
		FGuLiMassIdentityFragment::StaticStruct(),
		FGuLiMassHealthFragment::StaticStruct(),
		FGuLiClientSnapshotMirrorMassTag::StaticStruct()
	};
	FMassArchetypeCreationParams ArchetypeParams;
	ArchetypeParams.DebugName = TEXT("GuLiClientSnapshotMirrorSoldiers");
	ClientMirrorArchetype = EntityManager.CreateArchetype(FragmentAndTagTypes, ArchetypeParams);
	if (!ClientMirrorArchetype.IsValid())
	{
		return false;
	}

	ClientMirrorMassSubsystem = MassSubsystem;
	return true;
}

void AGuLiCommanderPresentationActor::EnsureClientMirrorEntity(
	const FGuLiSoldierStateItem& ReliableState)
{
	if (!ReliableState.SoldierId.IsValid() || !EnsureClientMirrorArchetype())
	{
		return;
	}

	UMassEntitySubsystem* MassSubsystem = ClientMirrorMassSubsystem.Get();
	if (!MassSubsystem)
	{
		return;
	}
	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	if (const FMassEntityHandle* Existing = ClientMirrorEntities.Find(ReliableState.SoldierId))
	{
		if (EntityManager.IsEntityValid(*Existing))
		{
			return;
		}
		ClientMirrorEntities.Remove(ReliableState.SoldierId);
	}

	const FMassEntityHandle Entity = EntityManager.CreateEntity(ClientMirrorArchetype);
	if (!EntityManager.IsEntityValid(Entity))
	{
		return;
	}

	FGuLiMassIdentityFragment& Identity = EntityManager.GetFragmentDataChecked<FGuLiMassIdentityFragment>(Entity);
	Identity.SoldierId = ReliableState.SoldierId;
	Identity.Team = ReliableState.Team;
	FGuLiMassHealthFragment& Health = EntityManager.GetFragmentDataChecked<FGuLiMassHealthFragment>(Entity);
	Health.Health = ReliableState.Health;
	Health.bDead = !ReliableState.IsAlive();
	Health.WreckSecondsRemaining = 0.0f;
	EntityManager.GetFragmentDataChecked<FTransformFragment>(Entity).SetTransform(FTransform::Identity);
	ClientMirrorEntities.Add(ReliableState.SoldierId, Entity);
}

void AGuLiCommanderPresentationActor::UpdateClientMirrorEntity(
	const FGuLiSoldierStateItem& ReliableState,
	const FTransform* PresentedTransform)
{
	EnsureClientMirrorEntity(ReliableState);
	UMassEntitySubsystem* MassSubsystem = ClientMirrorMassSubsystem.Get();
	FMassEntityHandle* Entity = ClientMirrorEntities.Find(ReliableState.SoldierId);
	if (!MassSubsystem || !Entity)
	{
		return;
	}

	FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
	if (!EntityManager.IsEntityValid(*Entity))
	{
		ClientMirrorEntities.Remove(ReliableState.SoldierId);
		return;
	}

	FGuLiMassIdentityFragment& Identity = EntityManager.GetFragmentDataChecked<FGuLiMassIdentityFragment>(*Entity);
	Identity.SoldierId = ReliableState.SoldierId;
	Identity.Team = ReliableState.Team;
	FGuLiMassHealthFragment& Health = EntityManager.GetFragmentDataChecked<FGuLiMassHealthFragment>(*Entity);
	Health.Health = ReliableState.Health;
	Health.bDead = !ReliableState.IsAlive();
	Health.WreckSecondsRemaining = 0.0f;
	if (Health.bDead && GetWorld())
	{
		if (const double* ExpireTime = WreckExpireTimes.Find(ReliableState.SoldierId))
		{
			Health.WreckSecondsRemaining = static_cast<float>(FMath::Max(
				0.0,
				*ExpireTime - static_cast<double>(GetWorld()->GetTimeSeconds())));
		}
	}
	if (PresentedTransform)
	{
		// No client Mass processor owns this fragment; presentation is its sole writer.
		EntityManager.GetFragmentDataChecked<FTransformFragment>(*Entity).SetTransform(*PresentedTransform);
	}
}

void AGuLiCommanderPresentationActor::DestroyClientMirrorEntities()
{
	if (UMassEntitySubsystem* MassSubsystem = ClientMirrorMassSubsystem.Get())
	{
		FMassEntityManager& EntityManager = MassSubsystem->GetMutableEntityManager();
		TArray<FMassEntityHandle> ValidEntities;
		ValidEntities.Reserve(ClientMirrorEntities.Num());
		for (const TPair<FGuLiSoldierId, FMassEntityHandle>& Pair : ClientMirrorEntities)
		{
			if (EntityManager.IsEntityValid(Pair.Value))
			{
				ValidEntities.Add(Pair.Value);
			}
		}
		if (!ValidEntities.IsEmpty())
		{
			EntityManager.BatchDestroyEntities(ValidEntities);
		}
	}

	ClientMirrorEntities.Reset();
	ClientMirrorArchetype = FMassArchetypeHandle();
	ClientMirrorMassSubsystem.Reset();
}

// 同步代次变化时丢弃旧样本、预测和时钟，防止把新战局数据接到旧时间线上。
void AGuLiCommanderPresentationActor::ResetNetworkPresentationState()
{
	PredictedMoves.Reset();
	WreckExpireTimes.Reset();
	for (TPair<FGuLiSoldierId, FGuLiCommanderPresentedSoldier>& Pair : PresentedSoldiers)
	{
		Pair.Value.Samples.Reset();
		Pair.Value.bHasAuthoritativeTransform = false;
		Pair.Value.bHasPresentedTransform = false;
		Pair.Value.bLifeStateInitialized = false;
		Pair.Value.LastPoseReceiptLocalTimeSeconds = 0.0;
		Pair.Value.MaximumPoseReceiptGapSeconds = 0.0;
		Pair.Value.LastUntaggedHardSnapDelta = FVector::ZeroVector;
		Pair.Value.LastHardSnapPriorSampleDelta = FVector::ZeroVector;
		Pair.Value.LastHardSnapSampleVelocity = FVector::ZeroVector;
		Pair.Value.LastHardSnapCurrentAnchor = FVector::ZeroVector;
		Pair.Value.LastHardSnapCurrentRelative = FVector::ZeroVector;
		Pair.Value.LastHardSnapPreviousAnchor = FVector::ZeroVector;
		Pair.Value.LastHardSnapPreviousRelative = FVector::ZeroVector;
		Pair.Value.LastHardSnapServerTimeGapSeconds = 0.0;
		Pair.Value.LastHardSnapFrameGap = 0u;
		Pair.Value.LastHardSnapCurrentChunkIndex = 0u;
		Pair.Value.LastHardSnapPreviousChunkIndex = 0u;
		Pair.Value.LastHardSnapCurrentSampleIndex = 0u;
		Pair.Value.LastHardSnapPreviousSampleIndex = 0u;
		Pair.Value.UntaggedHardSnapCount = 0u;
		Pair.Value.TeleportSnapCount = 0u;
	}
	LatestMeasuredServerNowSeconds = 0.0;
	LatestClockMeasurementLocalTimeSeconds = 0.0;
	EstimatedServerTimeSeconds = 0.0;
	LatestClockRoundTripMilliseconds = 0.0f;
	LatestClockFrameSequence = 0u;
	CurrentAuthorityEpoch = 0u;
	bServerClockInitialized = false;
	bLatestClockRoundTripFromConnectionStats = false;
}

void AGuLiCommanderPresentationActor::EnsureStableInstancePool(
	const AGuLiSoldierStateReplicator& Replicator)
{
	if (!UnitInstances || !RingInstances)
	{
		return;
	}

	TArray<FGuLiSoldierId> NewSoldierIds;
	for (const FGuLiSoldierStateItem& State : Replicator.GetItems())
	{
		EnsureClientMirrorEntity(State);
		if (State.SoldierId.IsValid() && !SoldierInstanceIndices.Contains(State.SoldierId))
		{
			NewSoldierIds.Add(State.SoldierId);
		}
	}
	NewSoldierIds.Sort([](const FGuLiSoldierId& Lhs, const FGuLiSoldierId& Rhs)
	{
		return Lhs.Value < Rhs.Value;
	});

	for (const FGuLiSoldierId SoldierId : NewSoldierIds)
	{
		if (SoldierInstanceIndices.Contains(SoldierId))
		{
			continue;
		}
		if (SoldierInstanceIndices.Num() >= GuLiCommanderPresentation::MaximumPresentedSoldiers)
		{
			if (!bLoggedInstancePoolFailure)
			{
				UE_LOG(LogGuLiStrike, Error, TEXT("Commander presentation exceeded its stable 500-Soldier instance pool."));
				bLoggedInstancePoolFailure = true;
			}
			break;
		}

		const FTransform HiddenTransform = GuLiCommanderPresentation::MakeHiddenTransform();
		const int32 UnitIndex = UnitInstances->AddInstance(HiddenTransform, true);
		const int32 RingIndex = RingInstances->AddInstance(HiddenTransform, true);
		const int32 ExpectedIndex = CachedUnitTransforms.Num();
		if (UnitIndex == INDEX_NONE || RingIndex == INDEX_NONE
			|| UnitIndex != RingIndex || UnitIndex != ExpectedIndex)
		{
			if (UnitIndex != INDEX_NONE && UnitIndex == UnitInstances->GetInstanceCount() - 1)
			{
				UnitInstances->RemoveInstance(UnitIndex);
			}
			if (RingIndex != INDEX_NONE && RingIndex == RingInstances->GetInstanceCount() - 1)
			{
				RingInstances->RemoveInstance(RingIndex);
			}
			if (!bLoggedInstancePoolFailure)
			{
				UE_LOG(LogGuLiStrike, Error, TEXT("Commander presentation failed to extend its stable ISM instance pool."));
				bLoggedInstancePoolFailure = true;
			}
			break;
		}

		SoldierInstanceIndices.Add(SoldierId, UnitIndex);
		CachedUnitTransforms.Add(HiddenTransform);
		CachedRingTransforms.Add(HiddenTransform);
		CachedRingColors.Add(GuLiCommanderPresentation::UnassignedColor);
	}
}

void AGuLiCommanderPresentationActor::RebuildLocalInstances(const float DeltaSeconds)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GuLiCommanderPresentation_RebuildLocalInstances);
	CSV_SCOPED_TIMING_STAT(GuLiCommanderPresentation, RebuildLocalInstances);

	AGuLiSoldierStateReplicator* Replicator = FindStateReplicator();
	if (!Replicator || !UnitInstances || !RingInstances || !GetWorld())
	{
		return;
	}

	const double LocalNowSeconds = GetWorld()->GetTimeSeconds();
	if (AGuLiCommanderPlayerController* Controller = FindLocalController())
	{
		if (const UGuLiCommanderNetSyncComponent* NetSync = Controller->GetCommanderNetSyncComponent())
		{
			const uint32 SyncGeneration = NetSync->GetSyncGeneration();
			if (SyncGeneration != 0u && SyncGeneration != LastObservedSyncGeneration)
			{
				ResetNetworkPresentationState();
				LastObservedSyncGeneration = SyncGeneration;
			}
		}
	}
	ConsumePoseChunks(LocalNowSeconds);
	EnsureStableInstancePool(*Replicator);
	if (bServerClockInitialized)
	{
		const double ExtrapolatedMeasuredServerNowSeconds = GuLiCommanderPresentation::ExtrapolateMeasuredServerNow(
				LatestMeasuredServerNowSeconds,
				LocalNowSeconds - LatestClockMeasurementLocalTimeSeconds);
		EstimatedServerTimeSeconds = GuLiCommanderPresentation::AdvanceEstimatedServerTime(
			EstimatedServerTimeSeconds,
			ExtrapolatedMeasuredServerNowSeconds,
			DeltaSeconds);
	}
	// 先估计服务器当前时间，再减插值回退时间；不可在旧样本时间上重复叠加网络延迟。
	const double RenderServerTimeSeconds = GuLiCommanderPresentation::CalculateRenderServerTime(
		EstimatedServerTimeSeconds,
		InterpolationBackTimeSeconds);

	TSet<FGuLiSoldierId> SelectedSoldiers;
	if (AGuLiCommanderPlayerController* Controller = FindLocalController())
	{
		if (const UGuLiCommanderNetSyncComponent* NetSync = Controller->GetCommanderNetSyncComponent())
		{
			for (const FGuLiControlCohortDescriptor& Cohort : NetSync->GetSelectionState().Cohorts)
			{
				for (const FGuLiSoldierId SoldierId : Cohort.MemberIds)
				{
					if (SoldierId.IsValid())
					{
						SelectedSoldiers.Add(SoldierId);
					}
				}
			}
		}
	}

	TArray<FTransform> DesiredUnitTransforms = CachedUnitTransforms;
	TArray<FTransform> DesiredRingTransforms = CachedRingTransforms;
	TArray<FLinearColor> DesiredRingColors = CachedRingColors;
	for (FTransform& Transform : DesiredUnitTransforms)
	{
		Transform.SetScale3D(FVector::ZeroVector);
	}
	for (FTransform& Transform : DesiredRingTransforms)
	{
		Transform.SetScale3D(FVector::ZeroVector);
	}

	for (const FGuLiSoldierStateItem& ReliableState : Replicator->GetItems())
	{
		const int32* InstanceIndex = SoldierInstanceIndices.Find(ReliableState.SoldierId);
		if (!InstanceIndex || !DesiredUnitTransforms.IsValidIndex(*InstanceIndex)
			|| !DesiredRingTransforms.IsValidIndex(*InstanceIndex)
			|| !DesiredRingColors.IsValidIndex(*InstanceIndex))
		{
			continue;
		}

		FGuLiCommanderPresentedSoldier& Soldier = PresentedSoldiers.FindOrAdd(ReliableState.SoldierId);
		const bool bAlive = ReliableState.IsAlive();
		if (!Soldier.bLifeStateInitialized)
		{
			Soldier.bLifeStateInitialized = true;
			if (!bAlive)
			{
				WreckExpireTimes.FindOrAdd(ReliableState.SoldierId) = LocalNowSeconds + static_cast<double>(WreckLifetimeSeconds);
			}
		}
		else if (Soldier.LastLifeState == EGuLiSoldierLifeState::Alive && !bAlive)
		{
			WreckExpireTimes.FindOrAdd(ReliableState.SoldierId) = LocalNowSeconds + static_cast<double>(WreckLifetimeSeconds);
		}
		if (bAlive)
		{
			WreckExpireTimes.Remove(ReliableState.SoldierId);
		}
		Soldier.LastLifeState = bAlive
			? EGuLiSoldierLifeState::Alive
			: EGuLiSoldierLifeState::Destroyed;
		if (!bAlive)
		{
			PredictedMoves.Remove(ReliableState.SoldierId);
		}

		FTransform AuthoritativeTransform;
		if (bServerClockInitialized
			&& EvaluateAuthoritativeTransform(
				Soldier,
				RenderServerTimeSeconds,
				AuthoritativeTransform))
		{
			Soldier.AuthoritativeTransform = AuthoritativeTransform;
			Soldier.bHasAuthoritativeTransform = true;
			FTransform PresentedTransform = AuthoritativeTransform;
			ApplyPrediction(ReliableState.SoldierId, LocalNowSeconds, PresentedTransform);
			Soldier.PresentedTransform = PresentedTransform;
			Soldier.bHasPresentedTransform = true;
		}

		if (!Soldier.bHasPresentedTransform)
		{
			UpdateClientMirrorEntity(ReliableState, nullptr);
			continue;
		}
		UpdateClientMirrorEntity(ReliableState, &Soldier.PresentedTransform);

		FTransform HiddenUnitTransform = Soldier.PresentedTransform;
		HiddenUnitTransform.SetScale3D(FVector::ZeroVector);
		DesiredUnitTransforms[*InstanceIndex] = HiddenUnitTransform;
		FTransform HiddenRingTransform = BuildRingTransform(Soldier.PresentedTransform);
		HiddenRingTransform.SetScale3D(FVector::ZeroVector);
		DesiredRingTransforms[*InstanceIndex] = HiddenRingTransform;

		if (bAlive)
		{
			FTransform UnitTransform = Soldier.PresentedTransform;
			UnitTransform.SetScale3D(FVector::OneVector);
			DesiredUnitTransforms[*InstanceIndex] = UnitTransform;
			DesiredRingTransforms[*InstanceIndex] = BuildRingTransform(Soldier.PresentedTransform);
			DesiredRingColors[*InstanceIndex] = SelectedSoldiers.Contains(ReliableState.SoldierId)
				? GuLiCommanderPresentation::SelectedColor
				: GuLiCommanderPresentation::GetTeamColor(ReliableState.Team);
		}
		else if (const double* ExpireTime = WreckExpireTimes.Find(ReliableState.SoldierId))
		{
			if (*ExpireTime > LocalNowSeconds)
			{
				DesiredRingTransforms[*InstanceIndex] = BuildRingTransform(Soldier.PresentedTransform);
				DesiredRingColors[*InstanceIndex] = GuLiCommanderPresentation::WreckColor;
			}
		}
	}

	for (auto It = WreckExpireTimes.CreateIterator(); It; ++It)
	{
		if (It.Value() <= LocalNowSeconds)
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = PredictedMoves.CreateIterator(); It; ++It)
	{
		const double MaximumLifetime = static_cast<double>(
			PredictionDurationSeconds + PredictionResolveSeconds + 0.25f);
		if (LocalNowSeconds - It.Value().StartTimeSeconds > MaximumLifetime)
		{
			It.RemoveCurrent();
		}
	}

	const bool bUnitTransformsChanged = !GuLiCommanderPresentation::AreTransformsEqual(
		CachedUnitTransforms,
		DesiredUnitTransforms);
	const bool bRingTransformsChanged = !GuLiCommanderPresentation::AreTransformsEqual(
		CachedRingTransforms,
		DesiredRingTransforms);
	const bool bRingColorsChanged = !GuLiCommanderPresentation::AreColorsEqual(
		CachedRingColors,
		DesiredRingColors);

	if (bUnitTransformsChanged && !DesiredUnitTransforms.IsEmpty()
		&& UnitInstances->BatchUpdateInstancesTransforms(
			0,
			DesiredUnitTransforms,
			true,
			false,
			false))
	{
		CachedUnitTransforms = DesiredUnitTransforms;
		UnitInstances->MarkRenderStateDirty();
	}
	if (bRingTransformsChanged && !DesiredRingTransforms.IsEmpty()
		&& RingInstances->BatchUpdateInstancesTransforms(
			0,
			DesiredRingTransforms,
			true,
			false,
			false))
	{
		CachedRingTransforms = DesiredRingTransforms;
		RingInstances->MarkRenderStateDirty();
	}

	if (bRingColorsChanged && RingInstances->GetInstanceCount() == DesiredRingColors.Num())
	{
		for (int32 InstanceIndex = 0; InstanceIndex < DesiredRingColors.Num(); ++InstanceIndex)
		{
			if (CachedRingColors.IsValidIndex(InstanceIndex)
				&& CachedRingColors[InstanceIndex].Equals(
					DesiredRingColors[InstanceIndex],
					KINDA_SMALL_NUMBER))
			{
				continue;
			}

			const FLinearColor& Color = DesiredRingColors[InstanceIndex];
			RingInstances->SetCustomDataValue(InstanceIndex, 0, Color.R, false);
			RingInstances->SetCustomDataValue(InstanceIndex, 1, Color.G, false);
			RingInstances->SetCustomDataValue(InstanceIndex, 2, Color.B, false);
			RingInstances->SetCustomDataValue(InstanceIndex, 3, Color.A, false);
		}
		CachedRingColors = DesiredRingColors;
		RingInstances->MarkRenderStateDirty();
	}
}

FTransform AGuLiCommanderPresentationActor::BuildRingTransform(
	const FTransform& SoldierTransform) const
{
	FTransform RingTransform = SoldierTransform;
	FVector RingLocation = RingTransform.GetLocation();
	RingLocation.Z += GuLiCommanderPresentation::RingHeight;
	RingTransform.SetLocation(RingLocation);
	RingTransform.SetScale3D(GuLiCommanderPresentation::RingScale);
	return RingTransform;
}

bool AGuLiCommanderPresentationActor::IsAckResultAccepted(const EGuLiCommandAckResult Result)
{
	return Result == EGuLiCommandAckResult::Accepted
		|| Result == EGuLiCommandAckResult::PartiallyAccepted;
}
