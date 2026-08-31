// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Tuning/GuLiRuntimeTuningSubsystem.h"

#include "Commander/Framework/GuLiCommanderGameState.h"
#include "Commander/Mass/GuLiBattleAuthoritySubsystem.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Data/GuLiCommanderSoldierDefinition.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "GuLiStrike.h"
#include "Subsystems/SubsystemCollection.h"

namespace GuLiRuntimeTuningSubsystemPrivate
{
	const FName SoldierMoveSpeed(TEXT("soldier.move_speed_cm_s"));
	const FName SoldierMaxHealth(TEXT("soldier.max_health"));
	const FName SoldierAttackPower(TEXT("soldier.attack_power"));
	const FName SoldierDefense(TEXT("soldier.defense"));
	const FName SoldierAttackRange(TEXT("soldier.attack_range_cm"));
	const FName ShipMaxSpeedMultiplier(TEXT("ship.max_speed_multiplier"));
	const FName ShipAccelerationMultiplier(TEXT("ship.acceleration_multiplier"));

	double Effective(const FGuLiRuntimeTuningRegistry& Registry, const FName Key)
	{
		return Registry.Get(Key.ToString()).Effective;
	}

	double Baseline(const FGuLiRuntimeTuningRegistry& Registry, const FName Key)
	{
		return Registry.Get(Key.ToString()).Baseline;
	}
}

bool UGuLiRuntimeTuningSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}

void UGuLiRuntimeTuningSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UGuLiCommanderDataSubsystem>();
	LoadSoldierBaselines();
}

void UGuLiRuntimeTuningSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	ApplyEffectiveSoldierValues();
	ApplyEffectiveShipValues();
	if (const UGuLiBattleAuthoritySubsystem* Authority = InWorld.GetSubsystem<UGuLiBattleAuthoritySubsystem>())
	{
		PublishReplicatedState(Authority->GetCommittedMovementSpeedCmPerSecond());
	}
}

FGuLiRuntimeTuningResult UGuLiRuntimeTuningSubsystem::GetValue(const FString& Key) const
{
	return Registry.Get(Key);
}

TArray<FGuLiRuntimeTuningEntryView> UGuLiRuntimeTuningSubsystem::ListValues(
	const FString& Prefix) const
{
	return Registry.List(Prefix);
}

FGuLiRuntimeTuningResult UGuLiRuntimeTuningSubsystem::SetValue(
	const FString& Key,
	const FString& ValueText)
{
	if (!GetWorld() || !IsMutationAllowedForNetMode(GetWorld()->GetNetMode()))
	{
		return MakeMutationRejectedResult(Key);
	}

	FGuLiRuntimeTuningResult Result = Registry.Set(Key, ValueText);
	if (!Result.bSuccess || !Result.bChanged)
	{
		return Result;
	}

	TArray<FGuLiRuntimeTuningResult*> ChangedResults = {&Result};
	ApplyChangedResults(ChangedResults);
	return Result;
}

TArray<FGuLiRuntimeTuningResult> UGuLiRuntimeTuningSubsystem::ResetValues(
	const FString& KeyOrAll)
{
	const FString TrimmedKey = KeyOrAll.TrimStartAndEnd();
	if (!GetWorld() || !IsMutationAllowedForNetMode(GetWorld()->GetNetMode()))
	{
		return {MakeMutationRejectedResult(TrimmedKey)};
	}

	TArray<FGuLiRuntimeTuningResult> Results;
	if (TrimmedKey.Equals(TEXT("all"), ESearchCase::IgnoreCase))
	{
		Results = Registry.ResetAll();
	}
	else
	{
		Results.Add(Registry.Reset(TrimmedKey));
	}

	TArray<FGuLiRuntimeTuningResult*> ChangedResults;
	for (FGuLiRuntimeTuningResult& Result : Results)
	{
		if (Result.bSuccess && Result.bChanged)
		{
			ChangedResults.Add(&Result);
		}
	}
	ApplyChangedResults(ChangedResults);
	return Results;
}

FGuLiSoldierRuntimeTuningValues UGuLiRuntimeTuningSubsystem::GetBaselineSoldierValues() const
{
	using namespace GuLiRuntimeTuningSubsystemPrivate;
	FGuLiSoldierRuntimeTuningValues Values;
	Values.MovementSpeedCmPerSecond = static_cast<float>(Baseline(Registry, SoldierMoveSpeed));
	Values.MaxHealth = static_cast<uint8>(Baseline(Registry, SoldierMaxHealth));
	Values.AttackPower = static_cast<float>(Baseline(Registry, SoldierAttackPower));
	Values.Defense = static_cast<float>(Baseline(Registry, SoldierDefense));
	Values.AttackRangeCentimeters = static_cast<float>(Baseline(Registry, SoldierAttackRange));
	return Values;
}

FGuLiSoldierRuntimeTuningValues UGuLiRuntimeTuningSubsystem::GetEffectiveSoldierValues() const
{
	using namespace GuLiRuntimeTuningSubsystemPrivate;
	FGuLiSoldierRuntimeTuningValues Values;
	Values.MovementSpeedCmPerSecond = static_cast<float>(Effective(Registry, SoldierMoveSpeed));
	Values.MaxHealth = static_cast<uint8>(Effective(Registry, SoldierMaxHealth));
	Values.AttackPower = static_cast<float>(Effective(Registry, SoldierAttackPower));
	Values.Defense = static_cast<float>(Effective(Registry, SoldierDefense));
	Values.AttackRangeCentimeters = static_cast<float>(Effective(Registry, SoldierAttackRange));
	return Values;
}

FGuLiShipRuntimeTuningValues UGuLiRuntimeTuningSubsystem::GetEffectiveShipValues() const
{
	using namespace GuLiRuntimeTuningSubsystemPrivate;
	FGuLiShipRuntimeTuningValues Values;
	Values.MaxSpeedMultiplier = static_cast<float>(Effective(Registry, ShipMaxSpeedMultiplier));
	Values.AccelerationMultiplier = static_cast<float>(Effective(Registry, ShipAccelerationMultiplier));
	return Values;
}

// 仅服务器从注册表写入飞船复制状态；倍率均为 1 时移除保留层，不清除其他玩法修饰。
bool UGuLiRuntimeTuningSubsystem::ApplyCurrentShipTuning(AGuLiStrikeShip& Ship) const
{
	if (!Ship.HasAuthority())
	{
		return false;
	}

	const FGuLiShipRuntimeTuningValues Values = GetEffectiveShipValues();
	if (Values.IsIdentity())
	{
		Ship.ClearGMRuntimeMultipliers();
		return true;
	}
	return Ship.SetGMRuntimeMultipliers(
		Values.MaxSpeedMultiplier,
		Values.AccelerationMultiplier);
}

// 异步/延迟提交可能已被后来的 Set/Reset 取代；只发布仍等于当前目标值的提交回调。
void UGuLiRuntimeTuningSubsystem::NotifySoldierMovementSpeedCommitted(
	const float CommittedMoveSpeedCmPerSecond,
	const int32 AppliedEntityCount)
{
	if (!bSoldierMovementSpeedPublicationPending
		|| !GetWorld()
		|| !IsMutationAllowedForNetMode(GetWorld()->GetNetMode())
		|| !FMath::IsFinite(CommittedMoveSpeedCmPerSecond)
		|| CommittedMoveSpeedCmPerSecond <= 0.0f)
	{
		return;
	}

	const float ExpectedMoveSpeed = GetEffectiveSoldierValues().MovementSpeedCmPerSecond;
	if (!FMath::IsNearlyEqual(CommittedMoveSpeedCmPerSecond, ExpectedMoveSpeed))
	{
		UE_LOG(
			LogGuLiStrike,
			Warning,
			TEXT("Ignoring stale Soldier movement tuning commit %.3f cm/s; current effective value is %.3f cm/s."),
			CommittedMoveSpeedCmPerSecond,
			ExpectedMoveSpeed);
		return;
	}

	bSoldierMovementSpeedPublicationPending = false;
	AdvanceRevisionAndPublish(CommittedMoveSpeedCmPerSecond);
	UE_LOG(
		LogGuLiStrike,
		Display,
		TEXT("Published committed Soldier movement tuning %.3f cm/s revision=%u applied=%d."),
		CommittedMoveSpeedCmPerSecond,
		TuningRevision,
		AppliedEntityCount);
}

bool UGuLiRuntimeTuningSubsystem::IsMutationAllowedForNetMode(const ENetMode NetMode)
{
	return NetMode == NM_Standalone
		|| NetMode == NM_ListenServer
		|| NetMode == NM_DedicatedServer;
}

void UGuLiRuntimeTuningSubsystem::LoadSoldierBaselines()
{
	const UWorld* World = GetWorld();
	const UGuLiCommanderDataSubsystem* DataSubsystem = World
		? World->GetSubsystem<UGuLiCommanderDataSubsystem>()
		: nullptr;
	if (!DataSubsystem)
	{
		return;
	}

	const FGuLiSoldierDefinition& Definition = DataSubsystem->GetDefaultSoldierDefinition();
	const EGuLiRuntimeTuningValueSource Source = DataSubsystem->IsDefaultSoldierDefinitionFromDataTable()
		? EGuLiRuntimeTuningValueSource::DataTable
		: EGuLiRuntimeTuningValueSource::CppFallback;
	Registry.SetBaseline(
		GuLiRuntimeTuningSubsystemPrivate::SoldierMoveSpeed,
		Definition.MovementSpeedCmPerSecond,
		Source);
	Registry.SetBaseline(
		GuLiRuntimeTuningSubsystemPrivate::SoldierMaxHealth,
		Definition.MaxHealth,
		Source);
	Registry.SetBaseline(
		GuLiRuntimeTuningSubsystemPrivate::SoldierAttackPower,
		Definition.AttackPower,
		Source);
	Registry.SetBaseline(
		GuLiRuntimeTuningSubsystemPrivate::SoldierDefense,
		Definition.Defense,
		Source);
	Registry.SetBaseline(
		GuLiRuntimeTuningSubsystemPrivate::SoldierAttackRange,
		Definition.AttackRangeCentimeters,
		Source);
}

int32 UGuLiRuntimeTuningSubsystem::ApplyEffectiveSoldierValues()
{
	UGuLiBattleAuthoritySubsystem* Authority = GetWorld()
		? GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()
		: nullptr;
	return Authority
		? Authority->ApplyRuntimeTuning(GetEffectiveSoldierValues())
		: 0;
}

int32 UGuLiRuntimeTuningSubsystem::ApplyEffectiveShipValues() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}

	int32 AppliedCount = 0;
	for (TActorIterator<AGuLiStrikeShip> It(World); It; ++It)
	{
		if (!It->IsActorBeingDestroyed() && ApplyCurrentShipTuning(**It))
		{
			++AppliedCount;
		}
	}
	return AppliedCount;
}

void UGuLiRuntimeTuningSubsystem::ApplyChangedResults(
	TArray<FGuLiRuntimeTuningResult*>& ChangedResults)
{
	if (ChangedResults.IsEmpty())
	{
		return;
	}

	bool bSoldierChanged = false;
	bool bSoldierMovementSpeedChanged = false;
	bool bShipChanged = false;
	for (const FGuLiRuntimeTuningResult* Result : ChangedResults)
	{
		const FString Key = Result->Key.ToString();
		bSoldierChanged |= Key.StartsWith(TEXT("soldier."), ESearchCase::IgnoreCase);
		bSoldierMovementSpeedChanged |= Result->Key ==
			GuLiRuntimeTuningSubsystemPrivate::SoldierMoveSpeed;
		bShipChanged |= Key.StartsWith(TEXT("ship."), ESearchCase::IgnoreCase);
	}

	const int32 SoldierAppliedCount = bSoldierChanged ? ApplyEffectiveSoldierValues() : 0;
	const int32 ShipAppliedCount = bShipChanged ? ApplyEffectiveShipValues() : 0;
	for (FGuLiRuntimeTuningResult* Result : ChangedResults)
	{
		const FString Key = Result->Key.ToString();
		Result->AppliedInstanceCount = Key.StartsWith(TEXT("soldier."), ESearchCase::IgnoreCase)
			? SoldierAppliedCount
			: ShipAppliedCount;
	}

	// 注册表生效值和 Mass 已提交值不同就延迟发布；两者已一致时取消旧待发布请求。
	if (bSoldierMovementSpeedChanged)
	{
		const UGuLiBattleAuthoritySubsystem* Authority = GetWorld()
			? GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()
			: nullptr;
		const float EffectiveMoveSpeed = GetEffectiveSoldierValues().MovementSpeedCmPerSecond;
		if (!Authority || !FMath::IsNearlyEqual(
			Authority->GetCommittedMovementSpeedCmPerSecond(),
			EffectiveMoveSpeed))
		{
			bSoldierMovementSpeedPublicationPending = Authority != nullptr;
			return;
		}

		// A same-frame Reset restored the already committed value, so any older
		// pending publication is cancelled and that stale speed can never escape.
		bSoldierMovementSpeedPublicationPending = false;
	}

	const UGuLiBattleAuthoritySubsystem* Authority = GetWorld()
		? GetWorld()->GetSubsystem<UGuLiBattleAuthoritySubsystem>()
		: nullptr;
	if (Authority)
	{
		AdvanceRevisionAndPublish(
			Authority->GetCommittedMovementSpeedCmPerSecond());
	}
}

void UGuLiRuntimeTuningSubsystem::AdvanceRevisionAndPublish(
	const float CommittedMoveSpeedCmPerSecond)
{
	++TuningRevision;
	if (TuningRevision == 0u)
	{
		TuningRevision = 1u;
	}
	PublishReplicatedState(CommittedMoveSpeedCmPerSecond);
}

// 网络承载对象是 GameState；服务器 setter 广播本地通知，并让复制系统将值送到客户端预测层。
void UGuLiRuntimeTuningSubsystem::PublishReplicatedState(
	const float CommittedMoveSpeedCmPerSecond) const
{
	if (!GetWorld())
	{
		return;
	}
	if (AGuLiCommanderGameState* GameState = GetWorld()->GetGameState<AGuLiCommanderGameState>())
	{
		GameState->SetAuthoritativeSoldierMovementTuning(
			CommittedMoveSpeedCmPerSecond,
			TuningRevision);
	}
}

FGuLiRuntimeTuningResult UGuLiRuntimeTuningSubsystem::MakeMutationRejectedResult(
	const FString& Key) const
{
	FGuLiRuntimeTuningResult Result = Registry.Get(Key);
	if (Result.bSuccess)
	{
		Result.bSuccess = false;
		Result.Error = TEXT("Set/Reset is allowed only in Standalone, Listen Server, or Dedicated Server Worlds");
	}
	return Result;
}
