// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "GuLiShipMovementComponent.generated.h"

struct FGuLiHybridShipInput;

/** CarrierSourceRef 对服务器重演位姿的查询结果；Pending 仅表示跨通道数据尚未到达。 */
UENUM(BlueprintType)
enum class EGuLiShipCanonicalMoveLookupResult : uint8
{
	Found,
	Pending,
	Expired,
	EpochMismatch,
	Unavailable
};

/**
 * 服务器在一次合法 CMC 重演完成后记录的飞船状态。
 * Epoch/Revision 只在该 Ship 实例的权威历史内有意义，不作为跨重生稳定身份。
 */
USTRUCT(BlueprintType)
struct FGuLiShipCanonicalMoveState
{
	GENERATED_BODY()

	UPROPERTY()
	uint32 CanonicalEpoch = 0u;

	UPROPERTY()
	uint32 MoveRevision = 0u;

	UPROPERTY()
	uint32 MovementConfigRevision = 0u;

	UPROPERTY()
	uint32 BarrierGeneration = 0u;

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement|Canonical")
	double ServerWorldTimeSeconds = 0.0;

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement|Canonical")
	FTransform Transform = FTransform::Identity;

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement|Canonical")
	FVector Velocity = FVector::ZeroVector;

	bool IsValid() const;
};

/** 有界、仅权威端写入的 canonical move 环；独立类型便于协议和自动化测试复用。 */
class GULISTRIKE_API FGuLiShipCanonicalMoveHistory
{
	friend class FGuLiHybridShipAdapter;
public:
	void Reset(uint32 NewEpoch, int32 InMaxEntries = 256);
	const FGuLiShipCanonicalMoveState& Append(double ServerWorldTimeSeconds, const FTransform& Transform,
		const FVector& Velocity, uint32 MovementConfigRevision, uint32 BarrierGeneration);
	EGuLiShipCanonicalMoveLookupResult Lookup(uint32 Epoch, uint32 Revision,
		FGuLiShipCanonicalMoveState& OutState) const;

	uint32 GetEpoch() const { return CanonicalEpoch; }
	uint32 GetLatestRevision() const { return LatestRevision; }
	int32 Num() const { return States.Num(); }
	const FGuLiShipCanonicalMoveState* GetLatest() const { return States.IsEmpty() ? nullptr : &States.Last(); }

private:
	TArray<FGuLiShipCanonicalMoveState> States;
	uint32 CanonicalEpoch = 0u;
	uint32 LatestRevision = 0u;
	int32 MaxEntries = 256;
};

/** Ship 已聚合完装配/GM 修饰的完整飞行配置；客户端不能提交或改写这些参数。 */
USTRUCT(BlueprintType)
struct FGuLiShipMovementConfig
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement")
	float MaxFlySpeed = 240.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement")
	float MaxAcceleration = 80.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement")
	float BrakingDecelerationFlying = 12.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement")
	float BoostThrustMultiplier = 2.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement")
	float YawRate = 40.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement")
	float YawResponseSpeed = 3.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement")
	float YawStopDamping = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement")
	float MaxBankAngle = 5.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement")
	float BankInterpSpeed = 4.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement")
	float OrientTurnSpeed = 2.5f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement")
	float OrientMinForwardDot = 0.3f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement")
	bool bOrientToMovement = false;

	bool IsValid() const;
	bool Equals(const FGuLiShipMovementConfig& Other) const;
};

/** Local committed configuration; it is captured explicitly by the Hybrid Ship adapter. */
USTRUCT()
struct FGuLiShipMovementSyncState
{
    GENERATED_BODY()
    UPROPERTY() FGuLiShipMovementConfig Config;
    UPROPERTY() uint32 ConfigRevision=0;
    UPROPERTY() uint32 RequiredLoadoutRevision=0;
    UPROPERTY() uint32 BarrierGeneration=0;
};

/** Local flight input consumed by the granted simulation owner. */
struct FGuLiShipFlightInput
{
	FVector LocalThrust = FVector::ZeroVector;
	float Turn = 0.0f;
	float Strafe = 0.0f;
	uint32 ConfigRevision = 0u;
	uint32 BarrierGeneration = 0u;
	bool bBoost = false;
	bool bSimulationEnabled = false;

	bool IsFiniteAndBounded() const;
};

/**
 * Granted Ship owner's local flight integrator and collision movement.
 * The World logical clock and Hybrid adapter own synchronization and restore.
 */
UCLASS(ClassGroup = (GuLiStrike))
class GULISTRIKE_API UGuLiShipMovementComponent : public UCharacterMovementComponent, public IGuLiExternalDisplacementTarget
{
	GENERATED_BODY()

public:
	UGuLiShipMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual ~UGuLiShipMovementComponent() override;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// 本地输入累积，LocalDirection 使用舰体局部轴；组件采样后清除瞬时轴，Boost 保持到释放。
	void AddThrustInput(const FVector& LocalDirection);
	void AddTurnInput(float Value);
	void AddStrafeInput(float Value);
	void SetBoostInput(bool bEnabled);
	void ClearFlightInput();
    void FreezeCarrierMotion();
	void AdvanceHybridFlight(float DeltaSeconds,const FGuLiHybridShipInput& Input);
	FGuLiShipFlightInput ConsumeHybridControllerInput();
	virtual void ApplyExternalDisplacement(const FTransform& Transform) override;

	// 服务器本地提交；仅完整有效配置或所需装配版本改变时创建新配置代次。
	bool CommitServerMovementConfig(const FGuLiShipMovementConfig& Config, uint32 RequiredLoadoutRevision);

	// 两端在整份装配应用成功后调用；0 表示尚未就绪。不是 ACK/RPC，也不授予服务器写权限。
	void SetAppliedLoadoutRevision(uint32 Revision);

	UFUNCTION(BlueprintPure, Category = "Ship|Movement|Network")
	bool IsMovementConfigReady() const;

	uint32 GetMovementConfigRevision() const { return MovementSyncState.ConfigRevision; }

	uint32 GetMovementBarrierGeneration() const { return MovementSyncState.BarrierGeneration; }

	uint32 GetAppliedLoadoutRevision() const { return AppliedLoadoutRevision; }

	/** 当前服务器 canonical history 代次；客户端始终返回 0。 */
	uint32 GetCanonicalEpoch() const;

	/** 最近一次服务器重演完成后的 revision；客户端始终返回 0。 */
	uint32 GetCanonicalMoveRevision() const;

	/** 按 CarrierSourceRef 精确查询权威历史；调用者据 Pending 实施最长 0.25 秒等待。 */
	EGuLiShipCanonicalMoveLookupResult LookupCanonicalMove(uint32 CanonicalEpoch, uint32 MoveRevision,
		FGuLiShipCanonicalMoveState& OutState) const;

	/** 取得当前最新状态；仅权威端且至少完成一次记录时成功。 */
	bool GetLatestCanonicalMove(FGuLiShipCanonicalMoveState& OutState) const;

	UFUNCTION(BlueprintPure, Category = "Ship|Movement")
	float GetYawVelocity() const { return YawVelocity; }

	UFUNCTION(BlueprintPure, Category = "Ship|Movement")
	float GetCurrentBankRoll() const { return CurrentBankRoll; }

	/** True only while a locally sampled, simulation-ready move is applying Boost. */
	UFUNCTION(BlueprintPure, Category = "Ship|Movement")
	bool IsBoostActive() const { return ActiveInput.bSimulationEnabled && ActiveInput.bBoost; }

	const FGuLiShipMovementConfig& GetEffectiveMovementConfig() const { return EffectiveConfig; }

    virtual void ResetPredictionData_Client() override;
    virtual bool ShouldUsePackedMovementRPCs() const override { return false; }
    virtual void PerformMovement(float DeltaTime) override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    friend class FGuLiHybridShipAdapter;
    void ApplyMovementParameters(const FGuLiShipMovementConfig& Config);
    void IntegrateFlightRotation(float DeltaTime,const FVector& WorldThrustIntent);
    bool CanSimulateCurrentMove() const;
    bool HasLocalFlightController() const;
    void ResetCanonicalMoveHistory();
    void RecordCanonicalMoveAfterAuthoritySimulation();

    UPROPERTY(Transient) FGuLiShipMovementSyncState MovementSyncState;
    FGuLiShipMovementConfig EffectiveConfig;
    FGuLiShipFlightInput PendingInput,ActiveInput;
    FGuLiShipCanonicalMoveHistory CanonicalMoveHistory;
    uint32 AppliedLoadoutRevision=0;
    float YawVelocity=0,CurrentBankRoll=0;
    bool bEndingPlay=false;
};
