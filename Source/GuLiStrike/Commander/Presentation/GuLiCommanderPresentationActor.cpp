// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commander/Presentation/GuLiCommanderPresentationActor.h"

#include "GuLiStrike.h"
#include "Commander/Framework/GuLiCommanderNetSyncComponent.h"
#include "GameFramework/PlayerController.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Framework/GuLiBattleGameState.h"
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
#include "MassCommandBuffer.h"
#include "MassCommands.h"
#include "MassEntityManager.h"
#include "MassEntitySubsystem.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"
#if !UE_BUILD_SHIPPING
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#endif

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
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	PrimaryActorTick.bRunOnAnyThread = false;

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
#if !UE_BUILD_SHIPPING
	if (bPredictionTraceActive)
	{
		FString Output;
		StopPredictionTrace(Output);
	}
#endif
	DestroyClientMirrorEntities();
	Super::EndPlay(EndPlayReason);
}

void AGuLiCommanderPresentationActor::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	RebuildLocalInstances(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	if (bPredictionTraceActive && GetWorld()
		&& (GetWorld()->GetRealTimeSeconds() >= PredictionTraceEndTime || PredictionTraceRows.Num() >= 20000))
	{
		FString Output;
		StopPredictionTrace(Output);
	}
#endif
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

bool AGuLiCommanderPresentationActor::TryGetAuthoritativeSoldierTransform(
	const FGuLiSoldierId SoldierId,
	FTransform& OutTransform) const
{
	const FGuLiCommanderPresentedSoldier* Soldier = PresentedSoldiers.Find(SoldierId);
	if (!SoldierId.IsValid() || !Soldier || !Soldier->bHasAuthoritativeTransform
		|| Soldier->AuthoritativeTransform.ContainsNaN())
	{
		return false;
	}

	OutTransform = Soldier->AuthoritativeTransform;
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

		for (int32 MemberIndex = 0; MemberIndex < Cohort.MemberIds.Num(); ++MemberIndex)
		{
			const FGuLiSoldierId SoldierId = Cohort.MemberIds[MemberIndex];
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

#if !UE_BUILD_SHIPPING
			if (bPredictionTraceActive)
			{
				if (!PredictionTraceSoldier.IsValid()) PredictionTraceSoldier = SoldierId;
				if (PredictionTraceSoldier == SoldierId)
				{
					if (PredictedMoves.Contains(SoldierId)) AppendPredictionTrace(EGuLiPredictionTraceEvent::ReplacedPrediction);
					PredictionTraceCommandId = ClientCommandId;
					PredictionTraceOrderId = 0;
					PredictionTraceDirection = ToTarget.GetSafeNormal2D();
					PredictionTraceTarget = Target;
					if (PredictionTraceCommands.Num() >= 256) PredictionTraceCommands.RemoveAt(0);
					FGuLiPredictionTraceCommand& TraceCommand = PredictionTraceCommands.AddDefaulted_GetRef();
					TraceCommand.CommandId = ClientCommandId;
					TraceCommand.CohortId = Cohort.CohortId;
					TraceCommand.FrozenMemberIndex = static_cast<uint8>(MemberIndex);
					TraceCommand.Direction = PredictionTraceDirection;
					TraceCommand.Target = Target;
					PredictionTraceOrderSampleTimes.Reset();
					bPredictionTraceHandoffRecorded = false;
					AppendPredictionTrace(EGuLiPredictionTraceEvent::Input);
				}
			}
#endif
			FGuLiCommanderPredictedMove& Prediction = PredictedMoves.FindOrAdd(SoldierId);
			Prediction = FGuLiCommanderPredictedMove{};
			Prediction.CohortId = Cohort.CohortId;
			Prediction.FrozenMemberIndex = static_cast<uint8>(MemberIndex);
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
#if !UE_BUILD_SHIPPING
	// Observation must outlive both prediction expiry and replacement by a newer move.
	TracePredictionAck(Ack);
#endif
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
				bAccepted = Ack.BatchOrderId != 0u
					&& (CohortResult->MemberCount > 0u
						? CohortResult->IsMemberAccepted(Prediction.FrozenMemberIndex)
						: IsAckResultAccepted(CohortResult->Result));
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

#if !UE_BUILD_SHIPPING
void AGuLiCommanderPresentationActor::TracePredictionAck(const FGuLiCommandAck& Ack)
{
	if (!bPredictionTraceActive || !GetWorld() || PredictionTraceRows.Num() >= 20000
		|| Ack.CommandKind != EGuLiCommandKind::Move || Ack.ClientCommandId == 0u) return;
	FGuLiPredictionTraceCommand* Command = PredictionTraceCommands.FindByPredicate(
		[&Ack](const FGuLiPredictionTraceCommand& Candidate) { return Candidate.CommandId == Ack.ClientCommandId; });
	if (!Command || Command->bAckRecorded) return;
	bool bAccepted = Ack.Result == EGuLiCommandAckResult::Accepted && Ack.BatchOrderId != 0u;
	if (!Ack.CohortResults.IsEmpty())
	{
		const FGuLiCohortCommandAck* Cohort = Ack.CohortResults.FindByPredicate(
			[Command](const FGuLiCohortCommandAck& Candidate) { return Candidate.CohortId == Command->CohortId; });
		bAccepted = Cohort && Ack.BatchOrderId != 0u
			&& (Cohort->MemberCount > 0u
				? Cohort->IsMemberAccepted(Command->FrozenMemberIndex)
				: IsAckResultAccepted(Cohort->Result));
	}
	Command->bAckRecorded = true;
	if (PredictionTraceCommandId == Ack.ClientCommandId) PredictionTraceOrderId = Ack.BatchOrderId;
	AppendPredictionTrace(bAccepted ? EGuLiPredictionTraceEvent::AckAccepted : EGuLiPredictionTraceEvent::AckRejected);
	FGuLiPredictionTraceRow& Row = PredictionTraceRows.Last();
	Row.CommandId = Ack.ClientCommandId;
	Row.OrderId = Ack.BatchOrderId;
	Row.Direction = Command->Direction;
	Row.Target = Command->Target;
	const FGuLiCommanderPredictedMove* Prediction = PredictedMoves.Find(PredictionTraceSoldier);
	Row.bHasPrediction = Prediction && Prediction->ClientCommandId == Ack.ClientCommandId;
	Row.bResolving = Row.bHasPrediction && Prediction->bResolving;
}

bool AGuLiCommanderPresentationActor::StartPredictionTrace(
	const bool bDisableDisplacement, const uint32 SoldierId, const float DurationSeconds)
{
	if (bPredictionTraceActive || !GetWorld() || GetNetMode() == NM_DedicatedServer
		|| !FindLocalController() || !FMath::IsFinite(DurationSeconds)
		|| DurationSeconds < 1.0f || DurationSeconds > 120.0f)
	{
		return false;
	}
	UGuLiCommanderNetSyncComponent* AckSource = FindLocalController()->FindComponentByClass<UGuLiCommanderNetSyncComponent>();
	if (!AckSource) return false;
	PredictionTraceRows.Reset();
	PredictionTraceRows.Reserve(4096);
	PredictionTraceCommands.Reset();
	PredictionTraceCommands.Reserve(256);
	PredictionTraceOrderSampleTimes.Reset();
	PredictionTraceSoldier.Value = SoldierId;
	PredictionTraceCommandId = 0;
	PredictionTraceOrderId = 0;
	PredictionTraceDirection = FVector::ZeroVector;
	PredictionTraceTarget = FVector::ZeroVector;
	PredictionTraceRequestedOffset = FVector::ZeroVector;
	PredictionTraceRenderServerTime = 0.0;
	bPredictionTraceHasPreviousFrame = false;
	bPredictionTraceHandoffRecorded = false;
	bPredictionTraceDisableDisplacement = bDisableDisplacement;
	PredictionTraceEndTime = GetWorld()->GetRealTimeSeconds() + DurationSeconds;
	bPredictionTraceActive = true;
	// Observe every delivered move ACK before the controller's latest-command UI filter.
	// Existing per-command trace dedup also covers the later ResolvePredictedMove call.
	PredictionTraceAckSource = AckSource;
	PredictionTraceAckHandle = AckSource->OnCommandAckChanged.AddUObject(this, &AGuLiCommanderPresentationActor::TracePredictionAck);
	AppendPredictionTrace(EGuLiPredictionTraceEvent::Start);
	UE_LOG(LogGuLiStrike, Display, TEXT("PredictionTrace started: mode=%s soldier=%u duration=%.1fs. Local move dispatch calls BeginPredictedMove; mouse_input is a separate controller hook. No move RPC is injected."),
		bDisableDisplacement ? TEXT("no_offset") : TEXT("baseline"), SoldierId, DurationSeconds);
	return true;
}

void AGuLiCommanderPresentationActor::TraceCommanderMoveInput(
	const uint32 ClientCommandId, const FVector& Target)
{
	if (!bPredictionTraceActive || !GetWorld() || PredictionTraceRows.Num() >= 20000) return;
	AppendPredictionTrace(EGuLiPredictionTraceEvent::MouseInput);
	FGuLiPredictionTraceRow& Row = PredictionTraceRows.Last();
	Row.CommandId = ClientCommandId;
	Row.Target = Target;
}

void AGuLiCommanderPresentationActor::AppendPredictionTrace(
	const EGuLiPredictionTraceEvent Event, const FGuLiCommanderBufferedSoldierPose* Sample)
{
	if (!bPredictionTraceActive || PredictionTraceRows.Num() >= 20000 || !GetWorld()) return;
	FGuLiPredictionTraceRow& Row = PredictionTraceRows.AddDefaulted_GetRef();
	Row.Event = Event;
	Row.LocalSeconds = GetWorld()->GetTimeSeconds();
	Row.LocalFrame = GFrameCounter;
	Row.RenderServerSeconds = PredictionTraceRenderServerTime;
	Row.SoldierId = PredictionTraceSoldier.Value;
	Row.CommandId = PredictionTraceCommandId;
	Row.OrderId = PredictionTraceOrderId;
	Row.Direction = PredictionTraceDirection;
	Row.Target = PredictionTraceTarget;
	Row.RequestedOffset = PredictionTraceRequestedOffset;
	if (const FGuLiCommanderPresentedSoldier* Soldier = PresentedSoldiers.Find(PredictionTraceSoldier))
	{
		Row.Authoritative = Soldier->AuthoritativeTransform.GetLocation();
		Row.Presented = Soldier->PresentedTransform.GetLocation();
		Row.Offset = Row.Presented - Row.Authoritative;
	}
	if (const FGuLiCommanderPredictedMove* Prediction = PredictedMoves.Find(PredictionTraceSoldier))
	{
		Row.bHasPrediction = true;
		Row.bResolving = Prediction->bResolving;
	}
	if (Sample)
	{
		Row.SampleServerSeconds = Sample->ServerTimeSeconds;
		Row.SampleFrame = Sample->FrameSequence;
		Row.OrderId = Sample->ActiveOrderId;
		Row.SampleLocation = Sample->Location;
		Row.SampleVelocity = Sample->Velocity;
	}
}

void AGuLiCommanderPresentationActor::CapturePredictionTraceFrame(
	const FGuLiSoldierId SoldierId, const double RenderServerSeconds)
{
	if (!bPredictionTraceActive || SoldierId != PredictionTraceSoldier) return;
	PredictionTraceRenderServerTime = RenderServerSeconds;
	if (!bPredictionTraceHandoffRecorded && PredictionTraceOrderId != 0)
	{
		if (const double* SampleTime = PredictionTraceOrderSampleTimes.Find(PredictionTraceOrderId))
		{
			if (RenderServerSeconds >= *SampleTime)
			{
				bPredictionTraceHandoffRecorded = true;
				AppendPredictionTrace(EGuLiPredictionTraceEvent::RenderHandoff);
				if (!PredictionTraceRows.IsEmpty()) PredictionTraceRows.Last().SampleServerSeconds = *SampleTime;
			}
		}
	}
	if (PredictionTraceRows.Num() >= 20000) return;
	AppendPredictionTrace(EGuLiPredictionTraceEvent::Frame);
	FGuLiPredictionTraceRow& Row = PredictionTraceRows.Last();
	const double Delta = Row.LocalSeconds - PredictionTracePreviousTime;
	if (bPredictionTraceHasPreviousFrame && Delta > UE_DOUBLE_SMALL_NUMBER && !Row.Direction.IsNearlyZero())
	{
		Row.bHasSpeed = true;
		Row.SignedPresentedSpeed = FVector::DotProduct(Row.Presented - PredictionTracePreviousPresented, Row.Direction) / Delta;
		Row.SignedAuthoritativeSpeed = FVector::DotProduct(Row.Authoritative - PredictionTracePreviousAuthoritative, Row.Direction) / Delta;
	}
	PredictionTracePreviousTime = Row.LocalSeconds;
	PredictionTracePreviousPresented = Row.Presented;
	PredictionTracePreviousAuthoritative = Row.Authoritative;
	bPredictionTraceHasPreviousFrame = true;
}

bool AGuLiCommanderPresentationActor::StopPredictionTrace(FString& OutCsvPath)
{
	if (!bPredictionTraceActive) return false;
	AppendPredictionTrace(EGuLiPredictionTraceEvent::Stop);
	// Always restore the temporary override, including when exporting fails.
	bPredictionTraceActive = false;
	if (UGuLiCommanderNetSyncComponent* AckSource = PredictionTraceAckSource.Get())
	{
		AckSource->OnCommandAckChanged.Remove(PredictionTraceAckHandle);
	}
	PredictionTraceAckHandle.Reset();
	PredictionTraceAckSource.Reset();
	const bool bDisabledDisplacement = bPredictionTraceDisableDisplacement;
	bPredictionTraceDisableDisplacement = false;
	const TCHAR* EventNames[] = {TEXT("start"), TEXT("input"), TEXT("mouse_input"), TEXT("replaced_prediction"),
		TEXT("ack_accepted"), TEXT("ack_rejected"), TEXT("sample_arrival"), TEXT("render_handoff"),
		TEXT("resolve"), TEXT("frame"), TEXT("hard_snap"), TEXT("network_reset"), TEXT("stop")};
	FString Csv(TEXT("event,local_seconds,local_frame,soldier_id,command_id,order_id,render_server_seconds,sample_server_seconds,sample_frame,displacement_disabled,net_mode,has_prediction,resolving,has_speed,signed_presented_cm_s,signed_authoritative_cm_s,authoritative_x,authoritative_y,authoritative_z,presented_x,presented_y,presented_z,offset_x,offset_y,offset_z,requested_offset_x,requested_offset_y,requested_offset_z,sample_x,sample_y,sample_z,sample_velocity_x,sample_velocity_y,sample_velocity_z,direction_x,direction_y,direction_z,target_x,target_y,target_z\n"));
	Csv.Reserve(PredictionTraceRows.Num() * 512);
	for (const FGuLiPredictionTraceRow& Row : PredictionTraceRows)
	{
		Csv += FString::Printf(TEXT("%s,%.6f,%llu,%u,%u,%u,%.6f,%.6f,%u,%d,%d,%d,%d,%d,%.3f,%.3f"),
			EventNames[static_cast<uint8>(Row.Event)], Row.LocalSeconds, static_cast<unsigned long long>(Row.LocalFrame),
			Row.SoldierId, Row.CommandId, Row.OrderId, Row.RenderServerSeconds, Row.SampleServerSeconds,
			Row.SampleFrame, bDisabledDisplacement, static_cast<int32>(GetNetMode()), Row.bHasPrediction, Row.bResolving,
			Row.bHasSpeed, Row.SignedPresentedSpeed, Row.SignedAuthoritativeSpeed);
		for (const FVector& V : {Row.Authoritative, Row.Presented, Row.Offset, Row.RequestedOffset,
			Row.SampleLocation, Row.SampleVelocity, Row.Direction, Row.Target})
		{
			Csv += FString::Printf(TEXT(",%.3f,%.3f,%.3f"), V.X, V.Y, V.Z);
		}
		Csv += TEXT("\n");
	}
	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()
		/ TEXT("outputs/commander-selection-20260831/diagnostics"));
	const FString Filename = FString::Printf(TEXT("prediction-%s-%s-%llu.csv"),
		bDisabledDisplacement ? TEXT("no_offset") : TEXT("baseline"),
		*FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S")), static_cast<unsigned long long>(GFrameCounter));
	OutCsvPath = Directory / Filename;
	const bool bSaved = IFileManager::Get().MakeDirectory(*Directory, true)
		&& FFileHelper::SaveStringToFile(Csv, *OutCsvPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogGuLiStrike, Display, TEXT("PredictionTrace stopped: rows=%d saved=%d override_restored=1 csv=%s"),
		PredictionTraceRows.Num(), bSaved, *OutCsvPath);
	PredictionTraceRows.Reset();
	PredictionTraceCommands.Reset();
	return bSaved;
}
#endif

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

APlayerController* AGuLiCommanderPresentationActor::FindLocalController()
{
	if (LocalController.IsValid() && LocalController->IsLocalController()
		&& LocalController->FindComponentByClass<UGuLiCommanderNetSyncComponent>())
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
		if (APlayerController* Candidate = Cast<APlayerController>(It->Get()))
		{
			if (Candidate->IsLocalController() && Candidate->FindComponentByClass<UGuLiCommanderNetSyncComponent>())
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
	APlayerController* Controller = FindLocalController();
	AGuLiSoldierStateReplicator* Replicator = FindStateReplicator();
	if (!Controller || !Replicator)
	{
		return;
	}

	UGuLiCommanderNetSyncComponent* NetSync = Controller->FindComponentByClass<UGuLiCommanderNetSyncComponent>();
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
		|| Chunk.AuthorityEpoch == 0u || Chunk.AuthorityEpoch != LastObservedMatchEpoch
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
		if (const APlayerController* Controller = FindLocalController())
		{
			if (const AGuLiBattlePlayerState* PlayerState = Controller->GetPlayerState<AGuLiBattlePlayerState>())
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
#if !UE_BUILD_SHIPPING
	if (bPredictionTraceActive && SoldierId == PredictionTraceSoldier)
	{
		AppendPredictionTrace(EGuLiPredictionTraceEvent::SampleArrival, &Sample);
		if (Sample.ActiveOrderId != 0 && PredictionTraceOrderSampleTimes.Num() < 64
			&& !PredictionTraceOrderSampleTimes.Contains(Sample.ActiveOrderId))
		{
			PredictionTraceOrderSampleTimes.Add(Sample.ActiveOrderId, Sample.ServerTimeSeconds);
		}
	}
#endif
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
#if !UE_BUILD_SHIPPING
		if (bPredictionTraceActive && SoldierId == PredictionTraceSoldier)
		{
			AppendPredictionTrace(EGuLiPredictionTraceEvent::HardSnap, &Sample);
		}
#endif
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

#if !UE_BUILD_SHIPPING
	if (bPredictionTraceActive && SoldierId == PredictionTraceSoldier)
	{
		PredictionTraceRequestedOffset = AppliedOffset;
	}
	// Diagnostic A/B only. The default branch and all prediction timing remain unchanged.
	if (!bPredictionTraceActive || !bPredictionTraceDisableDisplacement)
#endif
	{
		InOutTransform.AddToTranslation(AppliedOffset);
	}
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
#if !UE_BUILD_SHIPPING
	if (bPredictionTraceActive && Prediction.ClientCommandId == PredictionTraceCommandId)
	{
		const FGuLiCommanderPredictedMove* TracedPrediction = PredictedMoves.Find(PredictionTraceSoldier);
		if (TracedPrediction == &Prediction) AppendPredictionTrace(EGuLiPredictionTraceEvent::Resolve);
	}
#endif
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

	const FMassEntityHandle ReservedEntity = EntityManager.ReserveEntity();
	if (!EntityManager.IsEntityValid(ReservedEntity))
	{
		return;
	}

	const FGuLiSoldierId SoldierId = ReliableState.SoldierId;
	const EGuLiTeam Team = ReliableState.Team;
	const float HealthValue = ReliableState.Health;
	const bool bDead = !ReliableState.IsAlive();
	const FMassArchetypeHandle Archetype = ClientMirrorArchetype;
	ClientMirrorEntities.Add(SoldierId, ReservedEntity);
	// Replication callbacks and presentation ticks can overlap parallel Mass phases.
	// Reserve synchronously so the map has a stable handle, then build and initialize it
	// through the default command buffer at the next Mass-safe boundary.
	EntityManager.Defer().PushCommand<FMassDeferredCreateCommand>(
		[ReservedEntity, Archetype, SoldierId, Team, HealthValue, bDead](
			FMassEntityManager& Manager)
		{
			if (!Manager.IsEntityReserved(ReservedEntity) || !Archetype.IsValid())
			{
				return;
			}
			Manager.BuildEntity(ReservedEntity, Archetype);
			FGuLiMassIdentityFragment& Identity =
				Manager.GetFragmentDataChecked<FGuLiMassIdentityFragment>(ReservedEntity);
			Identity.SoldierId = SoldierId;
			Identity.Team = Team;
			FGuLiMassHealthFragment& Health =
				Manager.GetFragmentDataChecked<FGuLiMassHealthFragment>(ReservedEntity);
			Health.Health = HealthValue;
			Health.bDead = bDead;
			Health.WreckSecondsRemaining = 0.0f;
			Manager.GetFragmentDataChecked<FTransformFragment>(ReservedEntity)
				.SetTransform(FTransform::Identity);
		});
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
	if (!EntityManager.IsEntityActive(*Entity))
	{
		// The reserved handle is waiting for the deferred create boundary. The next
		// presentation tick will enqueue the newest state after it becomes active.
		return;
	}

	float WreckSecondsRemaining = 0.0f;
	const bool bDead = !ReliableState.IsAlive();
	if (bDead && GetWorld())
	{
		if (const double* ExpireTime = WreckExpireTimes.Find(ReliableState.SoldierId))
		{
			WreckSecondsRemaining = static_cast<float>(FMath::Max(
				0.0,
				*ExpireTime - static_cast<double>(GetWorld()->GetTimeSeconds())));
		}
	}
	const FMassEntityHandle TargetEntity = *Entity;
	const FGuLiSoldierId SoldierId = ReliableState.SoldierId;
	const EGuLiTeam Team = ReliableState.Team;
	const float HealthValue = ReliableState.Health;
	const bool bHasPresentedTransform = PresentedTransform != nullptr;
	const FTransform PendingTransform = PresentedTransform
		? *PresentedTransform
		: FTransform::Identity;
	EntityManager.Defer().PushCommand<FMassDeferredSetCommand>(
		[TargetEntity, SoldierId, Team, HealthValue, bDead, WreckSecondsRemaining,
			bHasPresentedTransform, PendingTransform](FMassEntityManager& Manager)
		{
			if (!Manager.IsEntityActive(TargetEntity))
			{
				return;
			}
			FGuLiMassIdentityFragment& Identity =
				Manager.GetFragmentDataChecked<FGuLiMassIdentityFragment>(TargetEntity);
			Identity.SoldierId = SoldierId;
			Identity.Team = Team;
			FGuLiMassHealthFragment& Health =
				Manager.GetFragmentDataChecked<FGuLiMassHealthFragment>(TargetEntity);
			Health.Health = HealthValue;
			Health.bDead = bDead;
			Health.WreckSecondsRemaining = WreckSecondsRemaining;
			if (bHasPresentedTransform)
			{
				// Client mirrors have no simulation fragments; presentation is their sole
				// Transform writer, committed at a Mass-safe command-buffer boundary.
				Manager.GetFragmentDataChecked<FTransformFragment>(TargetEntity)
					.SetTransform(PendingTransform);
			}
		});
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
			EntityManager.Defer().DestroyEntities(MoveTemp(ValidEntities));
		}
	}

	ClientMirrorEntities.Reset();
	ClientMirrorArchetype = FMassArchetypeHandle();
	ClientMirrorMassSubsystem.Reset();
}

// 同步代次变化时丢弃旧样本、预测和时钟，防止把新战局数据接到旧时间线上。
void AGuLiCommanderPresentationActor::ResetNetworkPresentationState()
{
#if !UE_BUILD_SHIPPING
	if (bPredictionTraceActive)
	{
		AppendPredictionTrace(EGuLiPredictionTraceEvent::NetworkReset);
		FString Output;
		StopPredictionTrace(Output);
	}
#endif
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

bool AGuLiCommanderPresentationActor::UpdateNetworkPresentationSource(
	AGuLiSoldierStateReplicator* Replicator)
{
	APlayerController* Controller = FindLocalController();
	UGuLiCommanderNetSyncComponent* NetSync = Controller
		? Controller->FindComponentByClass<UGuLiCommanderNetSyncComponent>() : nullptr;
	AGuLiBattlePlayerState* BattlePlayerState = Controller
		? Controller->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	const AGuLiBattleGameState* BattleGameState = GetWorld()
		? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	const uint32 MatchEpoch = BattleGameState ? BattleGameState->GetMatchEpoch() : 0u;
	const uint32 ConnectionGeneration = NetSync ? NetSync->GetConnectionGeneration() : 0u;
	const uint32 SyncGeneration = NetSync ? NetSync->GetSyncGeneration() : 0u;
	const bool bStreamReady = NetSync && BattlePlayerState && Replicator && MatchEpoch != 0u
		&& SyncGeneration != 0u && NetSync->IsConnectionReady() && NetSync->IsSoldierStreamReady()
		&& Replicator->GetSnapshotRevision() != 0u && Replicator->GetSnapshotMatchEpoch() == MatchEpoch;
	const bool bSourceChanged = ObservedNetSyncComponent.Get() != NetSync
		|| ObservedPlayerState.Get() != BattlePlayerState || ObservedStateReplicator.Get() != Replicator
		|| LastObservedMatchEpoch != MatchEpoch || LastObservedConnectionGeneration != ConnectionGeneration
		|| LastObservedSyncGeneration != SyncGeneration;

	if (bSourceChanged || bObservedSoldierStreamReady != bStreamReady)
	{
		// 同一 SoldierId 可在新战局复用；新姿态未到时也不能继续旧样本、预测或 Mass 镜像。
		ResetNetworkPresentationState();
		DestroyClientMirrorEntities();
		if (UnitInstances)
		{
			UnitInstances->SetVisibility(false);
		}
		if (RingInstances)
		{
			RingInstances->SetVisibility(false);
		}
		bNetworkPresentationHidden = true;
		ObservedNetSyncComponent = NetSync;
		ObservedPlayerState = BattlePlayerState;
		ObservedStateReplicator = Replicator;
		LastObservedMatchEpoch = MatchEpoch;
		LastObservedConnectionGeneration = ConnectionGeneration;
		LastObservedSyncGeneration = SyncGeneration;
		bObservedSoldierStreamReady = bStreamReady;
	}
	return bStreamReady;
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
	// 先检查失效边沿，Replicator/Controller 暂时消失时也要立即隐藏旧表现。
	if (!UpdateNetworkPresentationSource(Replicator) || !UnitInstances || !RingInstances || !GetWorld())
	{
		return;
	}

	const double LocalNowSeconds = GetWorld()->GetTimeSeconds();
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
	if (APlayerController* Controller = FindLocalController())
	{
		if (const UGuLiCommanderNetSyncComponent* NetSync = Controller->FindComponentByClass<UGuLiCommanderNetSyncComponent>())
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
#if !UE_BUILD_SHIPPING
			if (bPredictionTraceActive && ReliableState.SoldierId == PredictionTraceSoldier)
			{
				PredictionTraceRequestedOffset = FVector::ZeroVector;
			}
#endif
			ApplyPrediction(ReliableState.SoldierId, LocalNowSeconds, PresentedTransform);
			Soldier.PresentedTransform = PresentedTransform;
			Soldier.bHasPresentedTransform = true;
#if !UE_BUILD_SHIPPING
			if (bPredictionTraceActive && ReliableState.SoldierId == PredictionTraceSoldier)
			{
				CapturePredictionTraceFrame(ReliableState.SoldierId, RenderServerTimeSeconds);
			}
#endif
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

	if (bNetworkPresentationHidden)
	{
		// 先用当前同步源更新/隐藏各槽位，再恢复可见，避免短暂显示前一代次的变换。
		UnitInstances->SetVisibility(true);
		RingInstances->SetVisibility(true);
		bNetworkPresentationHidden = false;
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
