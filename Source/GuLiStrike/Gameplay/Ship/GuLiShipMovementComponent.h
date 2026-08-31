// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Battle/Network/GuLiPlayerNetSyncComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GuLiShipMovementComponent.generated.h"

class AGuLiBattlePlayerState;
class APlayerController;
struct FGuLiShipMovementNetworkStorage;
struct FGuLiShipMovementNetworkStorageDeleter
{
	void operator()(FGuLiShipMovementNetworkStorage* Storage) const;
};
struct FGuLiShipNetworkMoveData;
struct FGuLiShipMoveResponseData;
class FGuLiShipSavedMove;

/** Ship 已聚合完装配/GM 修饰的完整飞行配置；客户端不能提交或改写这些参数。 */
USTRUCT(BlueprintType)
struct FGuLiShipMovementConfig
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement")
	float MaxFlySpeed = 1200.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement")
	float MaxAcceleration = 400.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Ship|Movement")
	float BrakingDecelerationFlying = 60.0f;

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

/** 配置、装配依赖、拥有者身份和暂停基线成组复制；ACK 只确认这一精确屏障。 */
USTRUCT()
struct FGuLiShipMovementSyncState
{
	GENERATED_BODY()

	UPROPERTY()
	FGuLiShipMovementConfig Config;

	UPROPERTY()
	FGuLiConnectionBootstrapState ConnectionIdentity;

	UPROPERTY()
	uint32 ConfigRevision = 0u;

	UPROPERTY()
	uint32 RequiredLoadoutRevision = 0u;

	UPROPERTY()
	uint32 BarrierGeneration = 0u;

	UPROPERTY()
	FTransform BaselineTransform = FTransform::Identity;

	UPROPERTY()
	FVector BaselineVelocity = FVector::ZeroVector;

	UPROPERTY()
	float BaselineYawVelocity = 0.0f;

	UPROPERTY()
	float BaselineBankRoll = 0.0f;

	UPROPERTY()
	bool bServerAcknowledged = false;
};

/** 一次 CMC move 的原始本地轴输入；只在 SavedMove/packed move 内传输，不做属性复制。 */
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
 * 飞船唯一运动写者：复用 CharacterMovement 的移动 RPC、位置预测和网格平滑，
 * 扩展每次 move 的转向输入/积分状态；相机、部件及武器仍由 Ship 管理。
 */
UCLASS(ClassGroup = (GuLiStrike))
class GULISTRIKE_API UGuLiShipMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UGuLiShipMovementComponent(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual ~UGuLiShipMovementComponent() override;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// 本地输入累积，LocalDirection 使用舰体局部轴；组件采样后清除瞬时轴，Boost 保持到释放。
	void AddThrustInput(const FVector& LocalDirection);
	void AddTurnInput(float Value);
	void AddStrafeInput(float Value);
	void SetBoostInput(bool bEnabled);
	void ClearFlightInput();

	// 服务器本地提交；仅完整有效配置或所需装配版本改变时创建新配置代次。
	bool CommitServerMovementConfig(const FGuLiShipMovementConfig& Config, uint32 RequiredLoadoutRevision);

	// 两端在整份装配应用成功后调用；0 表示尚未就绪。不是 ACK/RPC，也不授予服务器写权限。
	void SetAppliedLoadoutRevision(uint32 Revision);

	UFUNCTION(BlueprintPure, Category = "Ship|Movement|Network")
	bool IsMovementConfigReady() const;

	uint32 GetMovementConfigRevision() const { return MovementSyncState.ConfigRevision; }

	uint32 GetMovementBarrierGeneration() const { return MovementSyncState.BarrierGeneration; }

	uint32 GetAppliedLoadoutRevision() const { return AppliedLoadoutRevision; }

	UFUNCTION(BlueprintPure, Category = "Ship|Movement")
	float GetYawVelocity() const { return YawVelocity; }

	UFUNCTION(BlueprintPure, Category = "Ship|Movement")
	float GetCurrentBankRoll() const { return CurrentBankRoll; }

	const FGuLiShipMovementConfig& GetEffectiveMovementConfig() const { return EffectiveConfig; }

	virtual FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	// 引擎占有/重启会真正重置预测时钟；这与保留时间的换装配置屏障是两种生命周期。
	virtual void ResetPredictionData_Client() override;
	virtual void ResetPredictionData_Server() override;
	virtual bool ClientUpdatePositionAfterServerUpdate() override;
	virtual bool ShouldUsePackedMovementRPCs() const override { return true; }
	virtual bool ShouldCorrectRotation() const override { return true; }
	virtual void PerformMovement(float DeltaTime) override;
	virtual void SimulateMovement(float DeltaTime) override;
	virtual void MoveAutonomous(float ClientTimeStamp, float DeltaTime, uint8 CompressedFlags, const FVector& NewAccel) override;
	virtual void ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData) override;
	virtual bool ServerCheckClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel,
		const FVector& ClientWorldLocation, const FVector& RelativeClientLocation,
		UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode) override;
	virtual bool ServerShouldUseAuthoritativePosition(float ClientTimeStamp, float DeltaTime, const FVector& Accel,
		const FVector& ClientWorldLocation, const FVector& RelativeClientLocation,
		UPrimitiveComponent* ClientMovementBase, FName ClientBaseBoneName, uint8 ClientMovementMode) override;
	virtual void ServerMoveHandleClientError(float ClientTimeStamp, float DeltaTime, const FVector& Accel,
		const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase,
		FName ClientBaseBoneName, uint8 ClientMovementMode) override;
	virtual void ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData,
		float TimeStamp, FVector NewLocation, FVector NewVelocity, UPrimitiveComponent* NewBase,
		FName NewBaseBoneName, bool bHasBase, bool bBaseRelativePosition,
		uint8 ServerMovementMode, FVector ServerGravityDirection) override;

private:
	friend class FGuLiShipSavedMove;
	friend struct FGuLiShipNetworkMoveData;
	friend struct FGuLiShipMoveResponseData;

	UFUNCTION()
	void OnRep_MovementSyncState();

	// 拥有客户端只确认服务器给出的完整身份和版本，不能通过此 RPC 选择配置或装配。
	UFUNCTION(Server, Reliable)
	void ServerAcknowledgeMovementConfig(uint32 ConfigRevision, uint32 BarrierGeneration,
		uint32 LoadoutRevision, const FGuLiConnectionBootstrapState& ConnectionIdentity);

	bool BuildCurrentConnectionIdentity(FGuLiConnectionBootstrapState& OutIdentity) const;
	void RefreshMovementSynchronization();
	void RefreshServerMovementSynchronization();
	void RefreshClientMovementSynchronization();
	void BeginServerMovementBarrier(const FGuLiConnectionBootstrapState& Identity);
	void ApplyMovementParameters(const FGuLiShipMovementConfig& Config);
	void ApplyClientMovementBaseline();
	void ClearPredictionHistoryPreservingTime();
	void IntegrateFlightRotation(float DeltaTime, const FVector& WorldThrustIntent);
	bool CanSimulateCurrentMove() const;
	void SampleLocalFlightInput();
	bool HasLocalFlightController() const;

	// 相关客户端都需要移动参数；拥有者使用身份/ACK 屏障，模拟代理仅消费服务器位姿与配置。
	UPROPERTY(ReplicatedUsing = OnRep_MovementSyncState)
	FGuLiShipMovementSyncState MovementSyncState;

	FGuLiShipMovementConfig EffectiveConfig;
	FGuLiShipFlightInput PendingInput;
	FGuLiShipFlightInput ActiveInput;
	TUniquePtr<FGuLiShipMovementNetworkStorage, FGuLiShipMovementNetworkStorageDeleter> NetworkStorage;
	TWeakObjectPtr<APlayerController> ObservedController;
	TWeakObjectPtr<AGuLiBattlePlayerState> ObservedPlayerState;
	TWeakObjectPtr<UGuLiPlayerNetSyncComponent> ObservedConnectionComponent;
	FGuLiNetworkRequestWindow ConfigurationAckWindow;

	uint32 AppliedLoadoutRevision = 0u;
	uint32 AppliedConfigRevision = 0u;
	uint32 AppliedBarrierGeneration = 0u;
	// 只在服务器预测时钟真正重置时更新；更早生命周期的迟到 move 不能推进新时钟。
	uint32 ServerPredictionBarrierFloor = 0u;
	float YawVelocity = 0.0f;
	float CurrentBankRoll = 0.0f;
	double NextConfigurationAckTime = 0.0;
	bool bProcessingRemoteMove = false;
	bool bServerStartedCurrentBarrier = false;
	bool bEndingPlay = false;
	bool bApplyingCorrection = false;
	float CorrectionTimeStamp = 0.0f;
	float CorrectionYawVelocity = 0.0f;
	float CorrectionBankRoll = 0.0f;
};
