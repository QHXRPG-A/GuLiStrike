// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Tuning/GuLiRuntimeTuningTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "GuLiRuntimeTuningSubsystem.generated.h"

class AGuLiStrikeShip;

/**
 * Per-World, non-persistent runtime tuning owner.
 *
 * The registry is an explicit whitelist. DataTable values replace C++
 * fallbacks once during World initialization; accepted GM overrides then take
 * precedence until this World is destroyed.
 */
// 每 World 的本地调参注册表，不会自动复制；跨端只发布 GameState 的已提交速度与各飞船的 GM 状态。
UCLASS()
class GULISTRIKE_API UGuLiRuntimeTuningSubsystem final : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;

	FGuLiRuntimeTuningResult GetValue(const FString& Key) const;
	TArray<FGuLiRuntimeTuningEntryView> ListValues(const FString& Prefix = FString()) const;
	// 只允许 Standalone/Listen/Dedicated World 修改白名单值；客户端返回拒绝结果，不自动转发 Server RPC。
	FGuLiRuntimeTuningResult SetValue(const FString& Key, const FString& ValueText);
	TArray<FGuLiRuntimeTuningResult> ResetValues(const FString& KeyOrAll);

	FGuLiSoldierRuntimeTuningValues GetBaselineSoldierValues() const;
	FGuLiSoldierRuntimeTuningValues GetEffectiveSoldierValues() const;
	FGuLiShipRuntimeTuningValues GetEffectiveShipValues() const;
	uint32 GetTuningRevision() const { return TuningRevision; }

	/** Applies the current reserved GM.Runtime layer to a newly spawned Ship. */
	// 服务器给当前/新出生飞船应用 GM.Runtime 层；返回是否可应用，客户端只消费 Ship 的复制状态。
	bool ApplyCurrentShipTuning(AGuLiStrikeShip& Ship) const;

	/** Called by authority only after Mass shared movement parameters commit. */
	// Authority 在 Mass 参数实际提交后本地回调；速度单位 cm/s，AppliedEntityCount 只用于结果日志。
	void NotifySoldierMovementSpeedCommitted(
		float CommittedMoveSpeedCmPerSecond,
		int32 AppliedEntityCount);

	/** Pure permission rule used by the command adapter and automation tests. */
	static bool IsMutationAllowedForNetMode(ENetMode NetMode);

private:
	void LoadSoldierBaselines();
	int32 ApplyEffectiveSoldierValues();
	int32 ApplyEffectiveShipValues() const;
	void ApplyChangedResults(TArray<FGuLiRuntimeTuningResult*>& ChangedResults);
	void AdvanceRevisionAndPublish(float CommittedMoveSpeedCmPerSecond);
	// 只向 GameState 发布已提交速度和版本；不是把整份 Registry 复制给客户端。
	void PublishReplicatedState(float CommittedMoveSpeedCmPerSecond) const;
	FGuLiRuntimeTuningResult MakeMutationRejectedResult(const FString& Key) const;

	FGuLiRuntimeTuningRegistry Registry;
	uint32 TuningRevision = 0u;
	// 标记等待 Mass 安全提交的速度更新；客户端预测不能提前使用尚未生效的目标速度。
	bool bSoldierMovementSpeedPublicationPending = false;
};
