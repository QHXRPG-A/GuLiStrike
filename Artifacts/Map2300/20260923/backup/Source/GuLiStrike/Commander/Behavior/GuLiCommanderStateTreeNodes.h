#pragma once
#include "StateTreeConditionBase.h"
#include "StateTreeTaskBase.h"
#include "MassStateTreeTypes.h"
#include "Commander/Behavior/GuLiCommanderWorkTypes.h"
#include "GuLiCommanderStateTreeNodes.generated.h"

/** Observations only; priority and transitions live in the authored StateTree. */
namespace GuLiCommanderBehaviorFacts
{
	constexpr uint32 CancelPending = 1u << 0;
	constexpr uint32 Stopped = 1u << 1;
	constexpr uint32 PendingMove = 1u << 2;
	constexpr uint32 Active = 1u << 3;
	constexpr uint32 Automatic = 1u << 4;
	constexpr uint32 Queued = 1u << 5;
	constexpr uint32 AutomaticReady = 1u << 6;
	constexpr uint32 WaitingTarget = 1u << 7;
	constexpr uint32 Mining = 1u << 8;
	constexpr uint32 Construction = 1u << 9;
	constexpr uint32 Advance = 1u << 10;
	constexpr uint32 Move = 1u << 11;
	constexpr uint32 Return = 1u << 12;
	constexpr uint32 Transit = 1u << 13;
	constexpr uint32 Cargo = 1u << 14;
	constexpr uint32 Started = 1u << 15;
	constexpr uint32 RetryReady = 1u << 16;
	constexpr uint32 Suspended = 1u << 17;
	constexpr uint32 WorkComplete = 1u << 18;
	constexpr uint32 ReturnFirst = 1u << 19;
}

UENUM()
enum class EGuLiCommanderBehaviorStep : uint8
{
	Wait, CancelPending, ReplaceMove, RunTask, TakeManual, TakeAutomatic,
	MiningSelect, MiningMove, MiningExtract, MiningFactory, MiningReturn, MiningEnter, MiningUnload, MiningExit,
	MiningFinish, MiningRetry, MiningFail, MiningComplete,
	ConstructionMove, ConstructionWork,
	AdvanceSelect, AdvanceMove, AdvanceCapture, AdvanceComplete, AdvanceReject, AdvanceWait,
	MiningReposition, ConstructionReserve, ConstructionRetry
};

USTRUCT()
struct FGuLiCommanderBehaviorNodeData { GENERATED_BODY() };

USTRUCT(meta=(DisplayName="Commander Actor Context", Category="GuLiStrike|Commander"))
struct FGuLiCommanderActorBehaviorCondition final : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()
	FGuLiCommanderActorBehaviorCondition() = default;
	FGuLiCommanderActorBehaviorCondition(uint32 InRequired, uint32 InForbidden,
		EGuLiCommanderWorkPhase InPhase = EGuLiCommanderWorkPhase::Any, EGuLiCommanderWorkResult InResult = EGuLiCommanderWorkResult::Any)
		: Required(InRequired), Forbidden(InForbidden), Phase(InPhase), Result(InResult) {}
	using FInstanceDataType = FGuLiCommanderBehaviorNodeData;
	virtual const UStruct* GetInstanceDataType() const override { return FGuLiCommanderBehaviorNodeData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
	// Static parameters: StateTree reserves Category="Context" for bound object/struct references.
	UPROPERTY(EditAnywhere, Category="Condition") uint32 Required = 0;
	UPROPERTY(EditAnywhere, Category="Condition") uint32 Forbidden = 0;
	UPROPERTY(EditAnywhere, Category="Condition") EGuLiCommanderWorkPhase Phase = EGuLiCommanderWorkPhase::Any;
	UPROPERTY(EditAnywhere, Category="Condition") EGuLiCommanderWorkResult Result = EGuLiCommanderWorkResult::Any;
};

USTRUCT(meta=(DisplayName="Commander Mass Context", Category="GuLiStrike|Commander"))
struct FGuLiCommanderMassBehaviorCondition final : public FMassStateTreeConditionBase
{
	GENERATED_BODY()
	FGuLiCommanderMassBehaviorCondition() = default;
	FGuLiCommanderMassBehaviorCondition(uint32 InRequired, uint32 InForbidden,
		EGuLiCommanderWorkPhase InPhase = EGuLiCommanderWorkPhase::Any, EGuLiCommanderWorkResult InResult = EGuLiCommanderWorkResult::Any)
		: Required(InRequired), Forbidden(InForbidden), Phase(InPhase), Result(InResult) {}
	using FInstanceDataType = FGuLiCommanderBehaviorNodeData;
	virtual const UStruct* GetInstanceDataType() const override { return FGuLiCommanderBehaviorNodeData::StaticStruct(); }
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;
	UPROPERTY(EditAnywhere, Category="Condition") uint32 Required = 0;
	UPROPERTY(EditAnywhere, Category="Condition") uint32 Forbidden = 0;
	UPROPERTY(EditAnywhere, Category="Condition") EGuLiCommanderWorkPhase Phase = EGuLiCommanderWorkPhase::Any;
	UPROPERTY(EditAnywhere, Category="Condition") EGuLiCommanderWorkResult Result = EGuLiCommanderWorkResult::Any;
};

USTRUCT(meta=(DisplayName="Commander Actor Operation", Category="GuLiStrike|Commander"))
struct FGuLiCommanderActorBehaviorTask final : public FStateTreeTaskCommonBase
{
	GENERATED_BODY()
	FGuLiCommanderActorBehaviorTask();
	explicit FGuLiCommanderActorBehaviorTask(EGuLiCommanderBehaviorStep InStep);
	using FInstanceDataType = FGuLiCommanderBehaviorNodeData;
	virtual const UStruct* GetInstanceDataType() const override { return FGuLiCommanderBehaviorNodeData::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	UPROPERTY(EditAnywhere, Category="Operation") EGuLiCommanderBehaviorStep Step = EGuLiCommanderBehaviorStep::Wait;
};

USTRUCT(meta=(DisplayName="Commander Mass Operation", Category="GuLiStrike|Commander"))
struct FGuLiCommanderMassBehaviorTask final : public FMassStateTreeTaskBase
{
	GENERATED_BODY()
	FGuLiCommanderMassBehaviorTask();
	explicit FGuLiCommanderMassBehaviorTask(EGuLiCommanderBehaviorStep InStep);
	using FInstanceDataType = FGuLiCommanderBehaviorNodeData;
	virtual const UStruct* GetInstanceDataType() const override { return FGuLiCommanderBehaviorNodeData::StaticStruct(); }
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) const override;
	virtual void GetDependencies(UE::MassBehavior::FStateTreeDependencyBuilder& Builder) const override;
	UPROPERTY(EditAnywhere, Category="Operation") EGuLiCommanderBehaviorStep Step = EGuLiCommanderBehaviorStep::Wait;
};
