// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/GuLiShipMovementComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"

#include "Battle/Framework/GuLiBattleGameState.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "GuLiStrike.h"
#include "Net/UnrealNetwork.h"

namespace GuLiShipMovement
{
	// CarrierSourceRef is consumed across actor channels and may legally remain pending for
	// 0.25 seconds.  Recording once per uncapped authority render frame can otherwise evict a
	// 256-entry history in less than that window under NullRHI or very high refresh rates.
	// 60 Hz remains finer than the 30 Hz Wingman simulation and lets the 256-entry ring
	// cover more than four seconds. That span safely contains the two-second Candidate
	// capture window, its 0.25-second cross-channel pending allowance, and a near-revoke
	// Resume lookup without making history growth depend on render rate.
	constexpr double CanonicalMoveSampleIntervalSeconds = 1.0 / 60.0;

	uint32 NextRevision(const uint32 Current)
	{
		const uint32 Next = Current + 1u;
		return Next == 0u ? 1u : Next;
	}

	bool MatchesIdentity(const FGuLiConnectionBootstrapState& Lhs, const FGuLiConnectionBootstrapState& Rhs)
	{
		return Lhs.Generation == Rhs.Generation && GuLiConnectionBootstrap::HasSameIdentity(Lhs, Rhs);
	}

	bool SameInput(const FGuLiShipFlightInput& Lhs, const FGuLiShipFlightInput& Rhs)
	{
		return Lhs.LocalThrust == Rhs.LocalThrust && Lhs.Turn == Rhs.Turn && Lhs.Strafe == Rhs.Strafe
			&& Lhs.bBoost == Rhs.bBoost && Lhs.bSimulationEnabled == Rhs.bSimulationEnabled
			&& Lhs.ConfigRevision == Rhs.ConfigRevision && Lhs.BarrierGeneration == Rhs.BarrierGeneration;
	}
}

bool FGuLiShipCanonicalMoveState::IsValid() const
{
	return CanonicalEpoch != 0u && MoveRevision != 0u && MovementConfigRevision != 0u
		&& BarrierGeneration != 0u && FMath::IsFinite(ServerWorldTimeSeconds)
		&& ServerWorldTimeSeconds >= 0.0 && !Transform.ContainsNaN() && !Velocity.ContainsNaN();
}

void FGuLiShipCanonicalMoveHistory::Reset(const uint32 NewEpoch, const int32 InMaxEntries)
{
	CanonicalEpoch = NewEpoch == 0u ? 1u : NewEpoch;
	LatestRevision = 0u;
	MaxEntries = FMath::Max(1, InMaxEntries);
	States.Reset(MaxEntries);
}

const FGuLiShipCanonicalMoveState& FGuLiShipCanonicalMoveHistory::Append(
	const double ServerWorldTimeSeconds, const FTransform& Transform, const FVector& Velocity,
	const uint32 MovementConfigRevision, const uint32 BarrierGeneration)
{
	if (CanonicalEpoch == 0u)
	{
		Reset(1u, MaxEntries);
	}
	if (LatestRevision == MAX_uint32)
	{
		Reset(GuLiShipMovement::NextRevision(CanonicalEpoch), MaxEntries);
	}

	LatestRevision = GuLiShipMovement::NextRevision(LatestRevision);
	FGuLiShipCanonicalMoveState& State = States.AddDefaulted_GetRef();
	State.CanonicalEpoch = CanonicalEpoch;
	State.MoveRevision = LatestRevision;
	State.MovementConfigRevision = MovementConfigRevision;
	State.BarrierGeneration = BarrierGeneration;
	State.ServerWorldTimeSeconds = ServerWorldTimeSeconds;
	State.Transform = Transform;
	State.Velocity = Velocity;

	const int32 Overflow = States.Num() - MaxEntries;
	if (Overflow > 0)
	{
		States.RemoveAt(0, Overflow, EAllowShrinking::No);
	}
	return States.Last();
}

EGuLiShipCanonicalMoveLookupResult FGuLiShipCanonicalMoveHistory::Lookup(
	const uint32 Epoch, const uint32 Revision, FGuLiShipCanonicalMoveState& OutState) const
{
	OutState = FGuLiShipCanonicalMoveState();
	if (CanonicalEpoch == 0u || LatestRevision == 0u || States.IsEmpty() || Epoch == 0u || Revision == 0u)
	{
		return EGuLiShipCanonicalMoveLookupResult::Unavailable;
	}
	if (Epoch != CanonicalEpoch)
	{
		return EGuLiShipCanonicalMoveLookupResult::EpochMismatch;
	}
	if (static_cast<int32>(Revision - LatestRevision) > 0)
	{
		return EGuLiShipCanonicalMoveLookupResult::Pending;
	}
	for (int32 Index = States.Num() - 1; Index >= 0; --Index)
	{
		if (States[Index].MoveRevision == Revision)
		{
			OutState = States[Index];
			return EGuLiShipCanonicalMoveLookupResult::Found;
		}
	}
	return EGuLiShipCanonicalMoveLookupResult::Expired;
}

bool FGuLiShipMovementConfig::IsValid() const
{
	return FMath::IsFinite(MaxFlySpeed) && MaxFlySpeed >= 0.0f
		&& FMath::IsFinite(MaxAcceleration) && MaxAcceleration >= 0.0f
		&& FMath::IsFinite(BrakingDecelerationFlying) && BrakingDecelerationFlying >= 0.0f
		&& FMath::IsFinite(BoostThrustMultiplier) && BoostThrustMultiplier >= 1.0f
		&& FMath::IsFinite(YawRate) && YawRate >= 0.0f
		&& FMath::IsFinite(YawResponseSpeed) && YawResponseSpeed > 0.0f
		&& FMath::IsFinite(YawStopDamping) && YawStopDamping > 0.0f
		&& FMath::IsFinite(MaxBankAngle) && MaxBankAngle >= 0.0f && MaxBankAngle <= 45.0f
		&& FMath::IsFinite(BankInterpSpeed) && BankInterpSpeed > 0.0f
		&& FMath::IsFinite(OrientTurnSpeed) && OrientTurnSpeed > 0.0f
		&& FMath::IsFinite(OrientMinForwardDot) && OrientMinForwardDot >= -1.0f && OrientMinForwardDot <= 1.0f;
}

bool FGuLiShipMovementConfig::Equals(const FGuLiShipMovementConfig& Other) const
{
	return MaxFlySpeed == Other.MaxFlySpeed && MaxAcceleration == Other.MaxAcceleration
		&& BrakingDecelerationFlying == Other.BrakingDecelerationFlying
		&& BoostThrustMultiplier == Other.BoostThrustMultiplier && YawRate == Other.YawRate
		&& YawResponseSpeed == Other.YawResponseSpeed && YawStopDamping == Other.YawStopDamping
		&& MaxBankAngle == Other.MaxBankAngle && BankInterpSpeed == Other.BankInterpSpeed
		&& OrientTurnSpeed == Other.OrientTurnSpeed && OrientMinForwardDot == Other.OrientMinForwardDot
		&& bOrientToMovement == Other.bOrientToMovement;
}

bool FGuLiShipFlightInput::IsFiniteAndBounded() const
{
	return !LocalThrust.ContainsNaN() && LocalThrust.GetAbsMax() <= 1.0
		&& FMath::IsFinite(Turn) && FMath::Abs(Turn) <= 1.0f
		&& FMath::IsFinite(Strafe) && FMath::Abs(Strafe) <= 1.0f;
}

/** 客户端历史只保存输入与观测状态；回放从服务器纠正的积分状态继续，不能恢复旧预测积分值。 */
class FGuLiShipSavedMove final : public FSavedMove_Character
{
public:
	FGuLiShipFlightInput Input;
	float StartYawVelocity = 0.0f;
	float StartBankRoll = 0.0f;
	float EndYawVelocity = 0.0f;
	float EndBankRoll = 0.0f;

	virtual void Clear() override
	{
		FSavedMove_Character::Clear();
		Input = FGuLiShipFlightInput();
		StartYawVelocity = StartBankRoll = EndYawVelocity = EndBankRoll = 0.0f;
	}

	virtual void SetMoveFor(ACharacter* Character, const float MoveDeltaTime, const FVector& NewAccel,
		FNetworkPredictionData_Client_Character& ClientData) override
	{
		FSavedMove_Character::SetMoveFor(Character, MoveDeltaTime, NewAccel, ClientData);
		const UGuLiShipMovementComponent* Movement = CastChecked<UGuLiShipMovementComponent>(Character->GetCharacterMovement());
		Input = Movement->ActiveInput;
		StartYawVelocity = Movement->YawVelocity;
		StartBankRoll = Movement->CurrentBankRoll;
		// FInterpTo(a, dt1) 再 FInterpTo(a, dt2) 不等于一次 dt1+dt2；禁止合并才能保留原惯性方程。
		bForceNoCombine = true;
	}

	virtual void PostUpdate(ACharacter* Character, EPostUpdateMode PostUpdateMode) override
	{
		FSavedMove_Character::PostUpdate(Character, PostUpdateMode);
		const UGuLiShipMovementComponent* Movement = CastChecked<UGuLiShipMovementComponent>(Character->GetCharacterMovement());
		EndYawVelocity = Movement->YawVelocity;
		EndBankRoll = Movement->CurrentBankRoll;
		// 新一帧可能先经历旋转纠正；记录实际用于积分的加速度，而非 Tick 开始时的旧世界方向。
		Acceleration = Movement->GetCurrentAcceleration();
		AccelMag = static_cast<float>(Acceleration.Size());
		AccelNormal = AccelMag > UE_SMALL_NUMBER ? Acceleration / AccelMag : FVector::ZeroVector;
	}

	virtual void PrepMoveFor(ACharacter* Character) override
	{
		FSavedMove_Character::PrepMoveFor(Character);
		UGuLiShipMovementComponent* Movement = CastChecked<UGuLiShipMovementComponent>(Character->GetCharacterMovement());
		Movement->ActiveInput = Input;
	}

	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override
	{
		return false;
	}

	virtual bool IsImportantMove(const FSavedMovePtr& LastAckedMove) const override
	{
		if (!LastAckedMove.IsValid())
		{
			return true;
		}
		const FGuLiShipSavedMove& Previous = static_cast<const FGuLiShipSavedMove&>(*LastAckedMove);
		return !GuLiShipMovement::SameInput(Input, Previous.Input)
			|| FMath::Abs(EndYawVelocity) > 0.05f || FMath::Abs(EndBankRoll) > 0.01f
			|| FSavedMove_Character::IsImportantMove(LastAckedMove);
	}
};

class FGuLiShipClientPredictionData final : public FNetworkPredictionData_Client_Character
{
public:
	explicit FGuLiShipClientPredictionData(const UCharacterMovementComponent& Movement)
		: FNetworkPredictionData_Client_Character(Movement)
	{
	}

	virtual FSavedMovePtr AllocateNewMove() override
	{
		return FSavedMovePtr(new FGuLiShipSavedMove());
	}
};

/** 原始轴输入供服务器重演；客户端结果旋转/积分值只用于比较，绝不覆盖服务器运动状态。 */
struct FGuLiShipNetworkMoveData final : public FCharacterNetworkMoveData
{
	FGuLiShipFlightInput Input;
	FRotator ClientRotation = FRotator::ZeroRotator;
	float ClientYawVelocity = 0.0f;
	float ClientBankRoll = 0.0f;

	virtual void ClientFillNetworkMoveData(const FSavedMove_Character& ClientMove, ENetworkMoveType MoveType) override
	{
		FCharacterNetworkMoveData::ClientFillNetworkMoveData(ClientMove, MoveType);
		const FGuLiShipSavedMove& ShipMove = static_cast<const FGuLiShipSavedMove&>(ClientMove);
		Input = ShipMove.Input;
		ClientRotation = ShipMove.SavedRotation;
		ClientYawVelocity = ShipMove.EndYawVelocity;
		ClientBankRoll = ShipMove.EndBankRoll;
	}

	virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar,
		UPackageMap* PackageMap, ENetworkMoveType MoveType) override
	{
		const bool bBaseSuccess = FCharacterNetworkMoveData::Serialize(Movement, Ar, PackageMap, MoveType);
		Ar << Input.LocalThrust << Input.Turn << Input.Strafe;
		Ar << Input.ConfigRevision << Input.BarrierGeneration;
		Ar.SerializeBits(&Input.bBoost, 1);
		Ar.SerializeBits(&Input.bSimulationEnabled, 1);
		Ar << ClientRotation << ClientYawVelocity << ClientBankRoll;
		return bBaseSuccess && !Ar.IsError() && Input.IsFiniteAndBounded()
			&& !ClientRotation.ContainsNaN() && FMath::IsFinite(ClientYawVelocity) && FMath::IsFinite(ClientBankRoll);
	}
};

struct FGuLiShipNetworkMoveDataContainer final : public FCharacterNetworkMoveDataContainer
{
	FGuLiShipNetworkMoveData ShipMoves[3];

	FGuLiShipNetworkMoveDataContainer()
	{
		NewMoveData = &ShipMoves[0];
		PendingMoveData = &ShipMoves[1];
		OldMoveData = &ShipMoves[2];
	}
};

struct FGuLiShipAngularAdjustment
{
	float TimeStamp = 0.0f;
	uint32 ConfigRevision = 0u;
	uint32 BarrierGeneration = 0u;
	float YawVelocity = 0.0f;
	float BankRoll = 0.0f;
	bool bValid = false;
};

struct FGuLiShipMoveResponseData final : public FCharacterMoveResponseDataContainer
{
	FGuLiShipAngularAdjustment AngularAdjustment;

	virtual void ServerFillResponseData(const UCharacterMovementComponent& Movement,
		const FClientAdjustment& PendingAdjustment) override;

	virtual bool Serialize(UCharacterMovementComponent& Movement, FArchive& Ar, UPackageMap* PackageMap) override
	{
		const bool bBaseSuccess = FCharacterMoveResponseDataContainer::Serialize(Movement, Ar, PackageMap);
		Ar << AngularAdjustment.ConfigRevision << AngularAdjustment.BarrierGeneration;
		Ar.SerializeBits(&AngularAdjustment.bValid, 1);
		if (IsCorrection() && AngularAdjustment.bValid)
		{
			Ar << AngularAdjustment.YawVelocity << AngularAdjustment.BankRoll;
		}
		AngularAdjustment.TimeStamp = ClientAdjustment.TimeStamp;
		return bBaseSuccess && !Ar.IsError() && FMath::IsFinite(AngularAdjustment.YawVelocity)
			&& FMath::IsFinite(AngularAdjustment.BankRoll);
	}
};

struct FGuLiShipMovementNetworkStorage
{
	FGuLiShipNetworkMoveDataContainer MoveData;
	FGuLiShipMoveResponseData MoveResponse;
	FGuLiShipAngularAdjustment PendingAngularAdjustment;
};

void FGuLiShipMovementNetworkStorageDeleter::operator()(FGuLiShipMovementNetworkStorage* Storage) const
{
	delete Storage;
}

void FGuLiShipMoveResponseData::ServerFillResponseData(const UCharacterMovementComponent& Movement,
	const FClientAdjustment& PendingAdjustment)
{
	FCharacterMoveResponseDataContainer::ServerFillResponseData(Movement, PendingAdjustment);
	const UGuLiShipMovementComponent& ShipMovement = static_cast<const UGuLiShipMovementComponent&>(Movement);
	AngularAdjustment = ShipMovement.NetworkStorage->PendingAngularAdjustment;
	// PendingAdjustment 可能延迟发送；这里只取生成该时间戳时的积分值，不能读取发送瞬间的新状态。
	if (!AngularAdjustment.bValid || AngularAdjustment.TimeStamp != PendingAdjustment.TimeStamp)
	{
		AngularAdjustment = FGuLiShipAngularAdjustment();
		AngularAdjustment.ConfigRevision = ShipMovement.MovementSyncState.ConfigRevision;
		AngularAdjustment.BarrierGeneration = ShipMovement.MovementSyncState.BarrierGeneration;
	}
}

UGuLiShipMovementComponent::UGuLiShipMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(true);
	NetworkStorage.Reset(new FGuLiShipMovementNetworkStorage());
	SetNetworkMoveDataContainer(NetworkStorage->MoveData);
	SetMoveResponseDataContainer(NetworkStorage->MoveResponse);
	GravityScale = 0.0f;
	bConstrainToPlane = false;
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = false;
	RotationRate = FRotator::ZeroRotator;
	DefaultLandMovementMode = MOVE_Flying;
	DefaultWaterMovementMode = MOVE_Flying;
	// 不允许引擎的可选“接受客户端位置”开关绕过本组件的服务器重演。
	bServerAcceptClientAuthoritativePosition = false;
	bIgnoreClientMovementErrorChecksAndCorrection = false;
}

UGuLiShipMovementComponent::~UGuLiShipMovementComponent() = default;

void UGuLiShipMovementComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGuLiShipMovementComponent, MovementSyncState);
}

void UGuLiShipMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	SetMovementMode(MOVE_Flying);
	RefreshMovementSynchronization();
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ResetCanonicalMoveHistory();
		RecordCanonicalMoveAfterAuthoritySimulation();
	}
}

void UGuLiShipMovementComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bEndingPlay = true;
	ClearFlightInput();
	Super::EndPlay(EndPlayReason);
}

bool UGuLiShipMovementComponent::HasLocalFlightController() const
{
	return !bEndingPlay && CharacterOwner && CharacterOwner->IsLocallyControlled()
		&& CharacterOwner->GetController() && CharacterOwner->GetController()->GetPawn() == CharacterOwner;
}

uint32 UGuLiShipMovementComponent::GetCanonicalEpoch() const
{
	return GetOwner() && GetOwner()->HasAuthority() ? CanonicalMoveHistory.GetEpoch() : 0u;
}

uint32 UGuLiShipMovementComponent::GetCanonicalMoveRevision() const
{
	return GetOwner() && GetOwner()->HasAuthority() ? CanonicalMoveHistory.GetLatestRevision() : 0u;
}

EGuLiShipCanonicalMoveLookupResult UGuLiShipMovementComponent::LookupCanonicalMove(
	const uint32 CanonicalEpoch, const uint32 MoveRevision, FGuLiShipCanonicalMoveState& OutState) const
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		OutState = FGuLiShipCanonicalMoveState();
		return EGuLiShipCanonicalMoveLookupResult::Unavailable;
	}
	return CanonicalMoveHistory.Lookup(CanonicalEpoch, MoveRevision, OutState);
}

bool UGuLiShipMovementComponent::GetLatestCanonicalMove(FGuLiShipCanonicalMoveState& OutState) const
{
	OutState = FGuLiShipCanonicalMoveState();
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return false;
	}
	const FGuLiShipCanonicalMoveState* Latest = CanonicalMoveHistory.GetLatest();
	if (!Latest)
	{
		return false;
	}
	OutState = *Latest;
	return OutState.IsValid();
}

void UGuLiShipMovementComponent::ResetCanonicalMoveHistory()
{
	CanonicalMoveHistory.Reset(GuLiShipMovement::NextRevision(CanonicalMoveHistory.GetEpoch()));
}

void UGuLiShipMovementComponent::RecordCanonicalMoveAfterAuthoritySimulation()
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !CharacterOwner || !GetWorld()
		|| MovementSyncState.ConfigRevision == 0u || MovementSyncState.BarrierGeneration == 0u)
	{
		return;
	}
	const FTransform Transform = CharacterOwner->GetActorTransform();
	if (Transform.ContainsNaN() || Velocity.ContainsNaN())
	{
		return;
	}
	const double ServerWorldTimeSeconds = static_cast<double>(GetWorld()->GetTimeSeconds());
	const FGuLiShipCanonicalMoveState* Latest = CanonicalMoveHistory.GetLatest();
	const bool bConfigurationBoundary = Latest
		&& (Latest->MovementConfigRevision != MovementSyncState.ConfigRevision
			|| Latest->BarrierGeneration != MovementSyncState.BarrierGeneration);
	if (Latest && !bConfigurationBoundary
		&& ServerWorldTimeSeconds >= Latest->ServerWorldTimeSeconds
		&& ServerWorldTimeSeconds - Latest->ServerWorldTimeSeconds
			+ UE_DOUBLE_SMALL_NUMBER < GuLiShipMovement::CanonicalMoveSampleIntervalSeconds)
	{
		return;
	}
	CanonicalMoveHistory.Append(ServerWorldTimeSeconds, Transform, Velocity,
		MovementSyncState.ConfigRevision, MovementSyncState.BarrierGeneration);
}

void UGuLiShipMovementComponent::AddThrustInput(const FVector& LocalDirection)
{
	if (HasLocalFlightController() && !LocalDirection.ContainsNaN())
	{
		PendingInput.LocalThrust += LocalDirection;
	}
}

void UGuLiShipMovementComponent::AddTurnInput(const float Value)
{
	if (HasLocalFlightController() && FMath::IsFinite(Value))
	{
		PendingInput.Turn += Value;
	}
}

void UGuLiShipMovementComponent::AddStrafeInput(const float Value)
{
	if (HasLocalFlightController() && FMath::IsFinite(Value))
	{
		PendingInput.Strafe += Value;
	}
}

void UGuLiShipMovementComponent::SetBoostInput(const bool bEnabled)
{
	PendingInput.bBoost = bEnabled && HasLocalFlightController();
}

void UGuLiShipMovementComponent::ClearFlightInput()
{
	PendingInput = FGuLiShipFlightInput();
	ActiveInput = FGuLiShipFlightInput();
	if (CharacterOwner)
	{
		CharacterOwner->ConsumeMovementInputVector();
	}
}

void UGuLiShipMovementComponent::SampleLocalFlightInput()
{
	ActiveInput = PendingInput;
	ActiveInput.LocalThrust.X = FMath::Clamp(ActiveInput.LocalThrust.X, -1.0, 1.0);
	ActiveInput.LocalThrust.Y = FMath::Clamp(ActiveInput.LocalThrust.Y, -1.0, 1.0);
	ActiveInput.LocalThrust.Z = FMath::Clamp(ActiveInput.LocalThrust.Z, -1.0, 1.0);
	ActiveInput.Turn = FMath::Clamp(ActiveInput.Turn, -1.0f, 1.0f);
	ActiveInput.Strafe = FMath::Clamp(ActiveInput.Strafe, -1.0f, 1.0f);
	ActiveInput.ConfigRevision = MovementSyncState.ConfigRevision;
	ActiveInput.BarrierGeneration = MovementSyncState.BarrierGeneration;
	ActiveInput.bSimulationEnabled = IsMovementConfigReady();
	const bool bHeldBoost = PendingInput.bBoost;
	PendingInput = FGuLiShipFlightInput();
	PendingInput.bBoost = bHeldBoost;

	// 输入只从这一入口送入 CMC，避免蓝图/旧 Ship AddMovementInput 与 packed 轴输入不一致。
	CharacterOwner->ConsumeMovementInputVector();
	if (!ActiveInput.bSimulationEnabled)
	{
		ActiveInput.LocalThrust = FVector::ZeroVector;
		ActiveInput.Turn = ActiveInput.Strafe = 0.0f;
		ActiveInput.bBoost = false;
		PendingInput.bBoost = false;
		return;
	}
	const FVector WorldThrust = CharacterOwner->GetActorQuat().RotateVector(ActiveInput.LocalThrust);
	CharacterOwner->AddMovementInput(WorldThrust,
		ActiveInput.bBoost ? EffectiveConfig.BoostThrustMultiplier : 1.0f);
}

void UGuLiShipMovementComponent::TickComponent(const float DeltaTime, const ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	RefreshMovementSynchronization();
	if (HasLocalFlightController())
	{
		SampleLocalFlightInput();
	}
	else
	{
		PendingInput = FGuLiShipFlightInput();
	}
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

bool UGuLiShipMovementComponent::BuildCurrentConnectionIdentity(FGuLiConnectionBootstrapState& OutIdentity) const
{
	OutIdentity = FGuLiConnectionBootstrapState();
	const APlayerController* Controller = CharacterOwner ? Cast<APlayerController>(CharacterOwner->GetController()) : nullptr;
	const AGuLiBattlePlayerState* BattlePlayerState = Controller ? Controller->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	const UGuLiPlayerNetSyncComponent* Connection = Controller ? Controller->FindComponentByClass<UGuLiPlayerNetSyncComponent>() : nullptr;
	const AGuLiBattleGameState* BattleGameState = GetWorld() ? GetWorld()->GetGameState<AGuLiBattleGameState>() : nullptr;
	// 公共连接已就绪不代表 UE 已确认当前 Pawn；CMC 的服务器移动入口也要求 AcknowledgedPawn 匹配。
	// 引擎 ServerAcknowledgePossession 独立于本组件握手，先完成它再开放配置 ACK/预测，不会循环等待。
	if (!Controller || Controller->GetPawn() != CharacterOwner || Controller->AcknowledgedPawn != CharacterOwner
		|| !BattlePlayerState || !BattleGameState || !Connection || !Connection->IsConnectionReady()
		|| BattlePlayerState->GetBattleRole() != EGuLiCommanderRole::Air)
	{
		return false;
	}
	OutIdentity.Generation = Connection->GetConnectionGeneration();
	OutIdentity.MatchEpoch = BattleGameState->GetMatchEpoch();
	OutIdentity.PlayerGuid = BattlePlayerState->GetPlayerGuid();
	OutIdentity.Team = BattlePlayerState->GetTeam();
	OutIdentity.Role = BattlePlayerState->GetBattleRole();
	OutIdentity.SlotIndex = BattlePlayerState->GetBattleSlotIndex();
	OutIdentity.bServerAcknowledged = true;
	return OutIdentity.Generation != 0u && GuLiConnectionBootstrap::IsValidIdentity(OutIdentity);
}

void UGuLiShipMovementComponent::ApplyMovementParameters(const FGuLiShipMovementConfig& Config)
{
	EffectiveConfig = Config;
	MaxFlySpeed = Config.MaxFlySpeed;
	MaxAcceleration = Config.MaxAcceleration;
	BrakingDecelerationFlying = Config.BrakingDecelerationFlying;
	GravityScale = 0.0f;
	bConstrainToPlane = false;
	bOrientRotationToMovement = false;
	bUseControllerDesiredRotation = false;
	RotationRate = FRotator::ZeroRotator;
	bServerAcceptClientAuthoritativePosition = false;
	bIgnoreClientMovementErrorChecksAndCorrection = false;
}

bool UGuLiShipMovementComponent::CommitServerMovementConfig(const FGuLiShipMovementConfig& Config,
	const uint32 RequiredLoadoutRevision)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || bEndingPlay || !Config.IsValid() || RequiredLoadoutRevision == 0u)
	{
		return false;
	}
	if (MovementSyncState.ConfigRevision != 0u && MovementSyncState.Config.Equals(Config)
		&& MovementSyncState.RequiredLoadoutRevision == RequiredLoadoutRevision)
	{
		RefreshServerMovementSynchronization();
		return true;
	}
	MovementSyncState.Config = Config;
	MovementSyncState.RequiredLoadoutRevision = RequiredLoadoutRevision;
	MovementSyncState.ConfigRevision = GuLiShipMovement::NextRevision(MovementSyncState.ConfigRevision);
	ApplyMovementParameters(Config);
	AppliedConfigRevision = MovementSyncState.ConfigRevision;
	FGuLiConnectionBootstrapState Identity;
	BuildCurrentConnectionIdentity(Identity);
	BeginServerMovementBarrier(Identity);
	RecordCanonicalMoveAfterAuthoritySimulation();
	RefreshServerMovementSynchronization();
	return true;
}

void UGuLiShipMovementComponent::SetAppliedLoadoutRevision(const uint32 Revision)
{
	AppliedLoadoutRevision = Revision;
	RefreshMovementSynchronization();
}

bool UGuLiShipMovementComponent::IsMovementConfigReady() const
{
	if (bEndingPlay || !CharacterOwner || MovementSyncState.ConfigRevision == 0u
		|| MovementSyncState.BarrierGeneration == 0u || !MovementSyncState.bServerAcknowledged
		|| AppliedConfigRevision != MovementSyncState.ConfigRevision
		|| AppliedLoadoutRevision == 0u || AppliedLoadoutRevision != MovementSyncState.RequiredLoadoutRevision)
	{
		return false;
	}
	FGuLiConnectionBootstrapState Identity;
	if (!BuildCurrentConnectionIdentity(Identity) || !GuLiShipMovement::MatchesIdentity(Identity, MovementSyncState.ConnectionIdentity))
	{
		return false;
	}
	return CharacterOwner->HasAuthority() || AppliedBarrierGeneration == MovementSyncState.BarrierGeneration;
}

void UGuLiShipMovementComponent::RefreshMovementSynchronization()
{
	if (!GetOwner() || bEndingPlay)
	{
		return;
	}
	if (GetOwner()->HasAuthority())
	{
		RefreshServerMovementSynchronization();
	}
	else
	{
		RefreshClientMovementSynchronization();
	}
}

void UGuLiShipMovementComponent::ApplyExternalDisplacement(const FTransform& Transform)
{
	if (!CharacterOwner || !CharacterOwner->HasAuthority() || Transform.ContainsNaN()) { return; }
	CharacterOwner->SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	CharacterOwner->SetBase(nullptr);
	StopMovementImmediately();
	ClearFlightInput();
	YawVelocity = 0.0f;
	CurrentBankRoll = Transform.Rotator().Roll;
	bJustTeleported = true;
	FGuLiConnectionBootstrapState Identity;
	BuildCurrentConnectionIdentity(Identity);
	BeginServerMovementBarrier(Identity);
	ServerPredictionBarrierFloor = MovementSyncState.BarrierGeneration;
	ResetCanonicalMoveHistory();
	RecordCanonicalMoveAfterAuthoritySimulation();
}

void UGuLiShipMovementComponent::BeginServerMovementBarrier(const FGuLiConnectionBootstrapState& Identity)
{
	MovementSyncState.BarrierGeneration = GuLiShipMovement::NextRevision(MovementSyncState.BarrierGeneration);
	MovementSyncState.ConnectionIdentity = Identity;
	MovementSyncState.bServerAcknowledged = false;
	MovementSyncState.BaselineTransform = CharacterOwner ? CharacterOwner->GetActorTransform() : FTransform::Identity;
	MovementSyncState.BaselineVelocity = Velocity;
	MovementSyncState.BaselineYawVelocity = YawVelocity;
	MovementSyncState.BaselineBankRoll = CurrentBankRoll;
	bServerStartedCurrentBarrier = false;
	ClearFlightInput();
	ConfigurationAckWindow.Reset();
	NetworkStorage->PendingAngularAdjustment = FGuLiShipAngularAdjustment();
	if (ServerPredictionData)
	{
		// 丢弃旧屏障待发送的响应，但保留客户端时间戳与防作弊时间累计。
		ServerPredictionData->PendingAdjustment = FClientAdjustment();
		ServerPredictionData->bForceClientUpdate = true;
	}
	if (GetOwner())
	{
		GetOwner()->ForceNetUpdate();
	}
}

void UGuLiShipMovementComponent::RefreshServerMovementSynchronization()
{
	if (!CharacterOwner || !CharacterOwner->HasAuthority() || MovementSyncState.ConfigRevision == 0u || bEndingPlay)
	{
		return;
	}
	APlayerController* Controller = Cast<APlayerController>(CharacterOwner->GetController());
	AGuLiBattlePlayerState* BattlePlayerState = Controller ? Controller->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
	UGuLiPlayerNetSyncComponent* Connection = Controller ? Controller->FindComponentByClass<UGuLiPlayerNetSyncComponent>() : nullptr;
	FGuLiConnectionBootstrapState Identity;
	const bool bValidIdentity = BuildCurrentConnectionIdentity(Identity);
	const bool bSourceChanged = ObservedController.Get() != Controller || ObservedPlayerState.Get() != BattlePlayerState
		|| ObservedConnectionComponent.Get() != Connection;
	const bool bLostLoadout = MovementSyncState.bServerAcknowledged
		&& AppliedLoadoutRevision != MovementSyncState.RequiredLoadoutRevision;
	if (bSourceChanged || !GuLiShipMovement::MatchesIdentity(Identity, MovementSyncState.ConnectionIdentity) || bLostLoadout)
	{
		BeginServerMovementBarrier(Identity);
	}
	ObservedController = Controller;
	ObservedPlayerState = BattlePlayerState;
	ObservedConnectionComponent = Connection;
	if (!MovementSyncState.bServerAcknowledged && bValidIdentity && CharacterOwner->IsLocallyControlled()
		&& AppliedLoadoutRevision != 0u && AppliedLoadoutRevision == MovementSyncState.RequiredLoadoutRevision)
	{
		MovementSyncState.bServerAcknowledged = true;
		bServerStartedCurrentBarrier = true;
		CharacterOwner->ForceNetUpdate();
	}
}

void UGuLiShipMovementComponent::OnRep_MovementSyncState()
{
	RefreshClientMovementSynchronization();
}

void UGuLiShipMovementComponent::ClearPredictionHistoryPreservingTime()
{
	if (ClientPredictionData)
	{
		// 只清旧配置的历史，不能 ResetPredictionData_Client：后者会把 CurrentTimeStamp 归零。
		ClientPredictionData->SavedMoves.Reset();
		ClientPredictionData->PendingMove.Reset();
		ClientPredictionData->LastAckedMove.Reset();
		ClientPredictionData->bUpdatePosition = false;
		ClientPredictionData->MeshTranslationOffset = FVector::ZeroVector;
		ClientPredictionData->OriginalMeshTranslationOffset = FVector::ZeroVector;
		ClientPredictionData->MeshRotationOffset = FQuat::Identity;
		ClientPredictionData->OriginalMeshRotationOffset = FQuat::Identity;
		ClientPredictionData->MeshRotationTarget = FQuat::Identity;
	}
	bNetworkSmoothingComplete = true;
	if (CharacterOwner && CharacterOwner->GetMesh())
	{
		CharacterOwner->GetMesh()->SetRelativeLocationAndRotation(
			CharacterOwner->GetBaseTranslationOffset(), CharacterOwner->GetBaseRotationOffset());
	}
}

void UGuLiShipMovementComponent::ApplyClientMovementBaseline()
{
	ClearPredictionHistoryPreservingTime();
	ClearFlightInput();
	if (CharacterOwner)
	{
		CharacterOwner->SetActorLocationAndRotation(MovementSyncState.BaselineTransform.GetLocation(),
			MovementSyncState.BaselineTransform.GetRotation(), false, nullptr, ETeleportType::TeleportPhysics);
		CharacterOwner->SetBase(nullptr);
	}
	Velocity = MovementSyncState.BaselineVelocity;
	YawVelocity = MovementSyncState.BaselineYawVelocity;
	CurrentBankRoll = MovementSyncState.BaselineBankRoll;
	Acceleration = FVector::ZeroVector;
	bJustTeleported = true;
	SetMovementMode(MOVE_Flying);
	UpdateComponentVelocity();
	AppliedBarrierGeneration = MovementSyncState.BarrierGeneration;
	NextConfigurationAckTime = 0.0;
}

void UGuLiShipMovementComponent::RefreshClientMovementSynchronization()
{
	if (!CharacterOwner || CharacterOwner->HasAuthority() || bEndingPlay
		|| MovementSyncState.ConfigRevision == 0u || MovementSyncState.BarrierGeneration == 0u
		|| !MovementSyncState.Config.IsValid())
	{
		return;
	}
	if (AppliedConfigRevision != MovementSyncState.ConfigRevision)
	{
		ApplyMovementParameters(MovementSyncState.Config);
		AppliedConfigRevision = MovementSyncState.ConfigRevision;
	}
	// 模拟代理没有拥有连接；它不 ACK/预测控制，仅使用 CMC 的服务器位姿和平滑。
	if (!CharacterOwner->IsLocallyControlled())
	{
		return;
	}
	FGuLiConnectionBootstrapState Identity;
	if (!BuildCurrentConnectionIdentity(Identity)
		|| !GuLiShipMovement::MatchesIdentity(Identity, MovementSyncState.ConnectionIdentity)
		|| AppliedLoadoutRevision == 0u || AppliedLoadoutRevision != MovementSyncState.RequiredLoadoutRevision)
	{
		return;
	}
	if (AppliedBarrierGeneration != MovementSyncState.BarrierGeneration)
	{
		ApplyClientMovementBaseline();
	}
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (!MovementSyncState.bServerAcknowledged && Now >= NextConfigurationAckTime)
	{
		NextConfigurationAckTime = Now + 0.5;
		ServerAcknowledgeMovementConfig(MovementSyncState.ConfigRevision, MovementSyncState.BarrierGeneration,
			AppliedLoadoutRevision, Identity);
	}
}

void UGuLiShipMovementComponent::ServerAcknowledgeMovementConfig_Implementation(const uint32 ConfigRevision,
	const uint32 BarrierGeneration, const uint32 LoadoutRevision, const FGuLiConnectionBootstrapState& ConnectionIdentity)
{
	RefreshServerMovementSynchronization();
	FGuLiConnectionBootstrapState CurrentIdentity;
	if (!CharacterOwner || !CharacterOwner->HasAuthority() || !BuildCurrentConnectionIdentity(CurrentIdentity)
		|| ConfigRevision == 0u || ConfigRevision != MovementSyncState.ConfigRevision
		|| BarrierGeneration != MovementSyncState.BarrierGeneration
		|| LoadoutRevision == 0u || LoadoutRevision != MovementSyncState.RequiredLoadoutRevision
		|| AppliedLoadoutRevision != LoadoutRevision
		|| !GuLiShipMovement::MatchesIdentity(ConnectionIdentity, CurrentIdentity)
		|| !GuLiShipMovement::MatchesIdentity(ConnectionIdentity, MovementSyncState.ConnectionIdentity))
	{
		return;
	}
	if (MovementSyncState.bServerAcknowledged)
	{
		return;
	}
	if (!GetWorld() || !ConfigurationAckWindow.Consume(GetWorld()->GetTimeSeconds(), 8, 1.0))
	{
		return;
	}
	MovementSyncState.bServerAcknowledged = true;
	CharacterOwner->ForceNetUpdate();
}

bool UGuLiShipMovementComponent::CanSimulateCurrentMove() const
{
	if (UGuLiExternalUnitControlComponent::AreActorActionsLocked(GetOwner())) { return false; }
	if (!HasValidData() || !IsMovementConfigReady()
		|| ActiveInput.ConfigRevision != MovementSyncState.ConfigRevision
		|| ActiveInput.BarrierGeneration != MovementSyncState.BarrierGeneration)
	{
		return false;
	}
	if (CharacterOwner->HasAuthority() && !CharacterOwner->IsLocallyControlled())
	{
		return bProcessingRemoteMove && bServerStartedCurrentBarrier;
	}
	return ActiveInput.bSimulationEnabled;
}

void UGuLiShipMovementComponent::IntegrateFlightRotation(const float DeltaTime, const FVector& WorldThrustIntent)
{
	const float TargetYawVelocity = FMath::Clamp(ActiveInput.Turn, -1.0f, 1.0f) * EffectiveConfig.YawRate;
	if (ActiveInput.Turn != 0.0f)
	{
		YawVelocity = FMath::FInterpTo(YawVelocity, TargetYawVelocity, DeltaTime, EffectiveConfig.YawResponseSpeed);
	}
	else
	{
		YawVelocity = FMath::FInterpTo(YawVelocity, 0.0f, DeltaTime, EffectiveConfig.YawStopDamping);
		if (FMath::Abs(YawVelocity) < 0.05f)
		{
			YawVelocity = 0.0f;
		}
	}
	FRotator FlightRotation = CharacterOwner->GetActorRotation();
	if (!FMath::IsNearlyZero(YawVelocity))
	{
		// 保留原实现的世界竖直轴偏航；不能改用已经倾斜的局部上轴。
		FlightRotation.Yaw += YawVelocity * DeltaTime;
	}
	if (EffectiveConfig.bOrientToMovement && WorldThrustIntent.SizeSquared() > KINDA_SMALL_NUMBER)
	{
		const FVector IntentDirection = WorldThrustIntent.GetSafeNormal();
		if (FVector::DotProduct(IntentDirection, FlightRotation.Vector()) > EffectiveConfig.OrientMinForwardDot)
		{
			FlightRotation = FMath::RInterpTo(FlightRotation, IntentDirection.Rotation(), DeltaTime, EffectiveConfig.OrientTurnSpeed);
		}
	}
	// YawRate 可合法设为 0；此时只有侧移压弯，避免原除法产生 NaN。
	const float YawBank = EffectiveConfig.YawRate > UE_SMALL_NUMBER ? YawVelocity / EffectiveConfig.YawRate : 0.0f;
	const float BankDirection = FMath::Clamp(YawBank + ActiveInput.Strafe, -1.0f, 1.0f);
	CurrentBankRoll = FMath::FInterpTo(CurrentBankRoll, -BankDirection * EffectiveConfig.MaxBankAngle,
		DeltaTime, EffectiveConfig.BankInterpSpeed);
	FlightRotation.Roll = CurrentBankRoll;
	CharacterOwner->SetActorRotation(FlightRotation);
}

void UGuLiShipMovementComponent::PerformMovement(const float DeltaTime)
{
	if (!CanSimulateCurrentMove())
	{
		// 屏障期间保留 Velocity/积分基线；仍让外层 ServerMove 验证并推进其网络时间戳。
		Acceleration = FVector::ZeroVector;
		return;
	}
	const FVector WorldThrustIntent = CharacterOwner->GetActorQuat().RotateVector(ActiveInput.LocalThrust);
	// CMC 会先缓存 Tick 输入再回放纠正；fresh move 并不经过 MoveAutonomous。
	// 因此三条路径在此按纠正后的当前朝向统一重建，不能沿用 Tick 开始时的世界加速度。
	const float InputScale = ActiveInput.bBoost ? EffectiveConfig.BoostThrustMultiplier : 1.0f;
	Acceleration = ScaleInputAcceleration(ConstrainInputAcceleration(WorldThrustIntent * InputScale));
	AnalogInputModifier = ComputeAnalogInputModifier();
	Super::PerformMovement(DeltaTime);
	if (HasValidData())
	{
		// 原 Ship 在 CMC 位移之后的 Actor Tick 转向；现在放进同一次 SavedMove，仍只积分一次。
		IntegrateFlightRotation(DeltaTime, WorldThrustIntent);
		RecordCanonicalMoveAfterAuthoritySimulation();
	}
}

void UGuLiShipMovementComponent::SimulateMovement(const float DeltaTime)
{
	if (MovementSyncState.ConfigRevision != 0u && MovementSyncState.bServerAcknowledged)
	{
		Super::SimulateMovement(DeltaTime);
	}
}

void UGuLiShipMovementComponent::MoveAutonomous(const float ClientTimeStamp, const float DeltaTime,
	const uint8 CompressedFlags, const FVector& NewAccel)
{
	if (bProcessingRemoteMove && ActiveInput.ConfigRevision == MovementSyncState.ConfigRevision
		&& ActiveInput.BarrierGeneration == MovementSyncState.BarrierGeneration
		&& IsMovementConfigReady() && ActiveInput.bSimulationEnabled)
	{
		// 已通过引擎时间戳、DeltaTime、控制器许可和暂停校验，才允许首个启用 move 打开运动区间。
		bServerStartedCurrentBarrier = true;
	}
	// 服务器/纠正回放都从本端重演后的舰体朝向重建加速度，不信任客户端提交的世界空间加速度。
	const FVector WorldInput = CharacterOwner
		? CharacterOwner->GetActorQuat().RotateVector(ActiveInput.LocalThrust) : FVector::ZeroVector;
	const float InputScale = ActiveInput.bBoost ? EffectiveConfig.BoostThrustMultiplier : 1.0f;
	const FVector ReplayedAcceleration = ConstrainInputAcceleration(WorldInput * InputScale).GetClampedToMaxSize(1.0)
		* GetMaxAcceleration();
	Super::MoveAutonomous(ClientTimeStamp, DeltaTime, CompressedFlags, ReplayedAcceleration);
}

FNetworkPredictionData_Client* UGuLiShipMovementComponent::GetPredictionData_Client() const
{
	if (!ClientPredictionData)
	{
		UGuLiShipMovementComponent* MutableThis = const_cast<UGuLiShipMovementComponent*>(this);
		MutableThis->ClientPredictionData = new FGuLiShipClientPredictionData(*this);
	}
	return ClientPredictionData;
}

void UGuLiShipMovementComponent::ResetPredictionData_Client()
{
	Super::ResetPredictionData_Client();
	// ACharacter::PawnClientRestart 会 StopMovement 并删除 SavedMoves；即使配置已先到，也须重取该屏障基准。
	AppliedBarrierGeneration = 0u;
	bApplyingCorrection = false;
	ClearFlightInput();
}

void UGuLiShipMovementComponent::ResetPredictionData_Server()
{
	Super::ResetPredictionData_Server();
	NetworkStorage->PendingAngularAdjustment = FGuLiShipAngularAdjustment();
	if (!CharacterOwner || !CharacterOwner->HasAuthority() || bEndingPlay || MovementSyncState.ConfigRevision == 0u)
	{
		return;
	}
	// UE 在 UnPossessed、OnPossess 和拥有者确认 Pawn 时都会删除服务器预测数据、把时钟归零。
	// 必须同时换屏障：旧生命周期的高时间戳若进入 Super，会先推进时钟，再因尚未 ACK 占有而退出；
	// 随后客户端 PawnClientRestart 从零发的新 move 就会持续过期，SavedMoves 最终积满。
	FGuLiConnectionBootstrapState Identity;
	BuildCurrentConnectionIdentity(Identity);
	BeginServerMovementBarrier(Identity);
	ServerPredictionBarrierFloor = MovementSyncState.BarrierGeneration;
	ResetCanonicalMoveHistory();
	RecordCanonicalMoveAfterAuthoritySimulation();
}

bool UGuLiShipMovementComponent::ClientUpdatePositionAfterServerUpdate()
{
	// PrepMoveFor 会切换 ActiveInput；回放完要恢复本帧刚采样的输入，不能把最后一份历史输入再发一遍。
	const FGuLiShipFlightInput CurrentFrameInput = ActiveInput;
	const bool bReplayed = Super::ClientUpdatePositionAfterServerUpdate();
	ActiveInput = CurrentFrameInput;
	return bReplayed;
}

void UGuLiShipMovementComponent::ServerMove_PerformMovement(const FCharacterNetworkMoveData& MoveData)
{
	RefreshServerMovementSynchronization();
	const FGuLiShipNetworkMoveData& ShipMove = static_cast<const FGuLiShipNetworkMoveData&>(MoveData);
	if (MovementSyncState.ConfigRevision == 0u || MovementSyncState.BarrierGeneration == 0u
		|| ShipMove.Input.ConfigRevision == 0u || ShipMove.Input.BarrierGeneration == 0u
		|| !ShipMove.Input.IsFiniteAndBounded()
		|| static_cast<int32>(ShipMove.Input.BarrierGeneration - MovementSyncState.BarrierGeneration) > 0
		|| (ServerPredictionBarrierFloor != 0u
			&& static_cast<int32>(ShipMove.Input.BarrierGeneration - ServerPredictionBarrierFloor) < 0))
	{
		// 零代次、非法输入、未来屏障及较早占有生命周期的 move 都不能先推进引擎预测时钟。
		// 同一生命周期内已发布的旧屏障仍可计时，下面的模拟门会阻止它继续执行旧输入。
		return;
	}
	const FGuLiShipFlightInput PreviousInput = ActiveInput;
	ActiveInput = ShipMove.Input;
	if (!ActiveInput.bSimulationEnabled)
	{
		ActiveInput.LocalThrust = FVector::ZeroVector;
		ActiveInput.Turn = ActiveInput.Strafe = 0.0f;
		ActiveInput.bBoost = false;
	}
	bProcessingRemoteMove = true;
	// 同一预测生命周期内仍接收旧配置/装配屏障的时间戳，只禁止其 Physics；
	// 这样连续 GM/换装后的新 move 不会把整段等待时间算作第一步。时钟重置前的旧 move 已在上方拒绝。
	Super::ServerMove_PerformMovement(MoveData);
	bProcessingRemoteMove = false;
	ActiveInput = PreviousInput;
}

bool UGuLiShipMovementComponent::ServerCheckClientError(const float ClientTimeStamp, const float DeltaTime,
	const FVector& Accel, const FVector& ClientWorldLocation, const FVector& RelativeClientLocation,
	UPrimitiveComponent* ClientMovementBase, const FName ClientBaseBoneName, const uint8 ClientMovementMode)
{
	const FGuLiShipNetworkMoveData* ShipMove = static_cast<const FGuLiShipNetworkMoveData*>(GetCurrentNetworkMoveData());
	if (!ShipMove || ShipMove->Input.ConfigRevision != MovementSyncState.ConfigRevision
		|| ShipMove->Input.BarrierGeneration != MovementSyncState.BarrierGeneration || !IsMovementConfigReady())
	{
		return true;
	}
	const FRotator RotationError = (CharacterOwner->GetActorRotation() - ShipMove->ClientRotation).GetNormalized();
	if (FMath::Abs(RotationError.Yaw) > 0.5 || FMath::Abs(RotationError.Pitch) > 0.5
		|| FMath::Abs(RotationError.Roll) > 0.25
		|| FMath::Abs(YawVelocity - ShipMove->ClientYawVelocity) > 0.5f
		|| FMath::Abs(CurrentBankRoll - ShipMove->ClientBankRoll) > 0.25f)
	{
		return true;
	}
	return Super::ServerCheckClientError(ClientTimeStamp, DeltaTime, Accel, ClientWorldLocation,
		RelativeClientLocation, ClientMovementBase, ClientBaseBoneName, ClientMovementMode);
}

bool UGuLiShipMovementComponent::ServerShouldUseAuthoritativePosition(const float ClientTimeStamp, const float DeltaTime,
	const FVector& Accel, const FVector& ClientWorldLocation, const FVector& RelativeClientLocation,
	UPrimitiveComponent* ClientMovementBase, const FName ClientBaseBoneName, const uint8 ClientMovementMode)
{
	// 即使全局 GameNetworkManager 打开客户端位置信任，飞船仍坚持服务器重演。
	return false;
}

void UGuLiShipMovementComponent::ServerMoveHandleClientError(const float ClientTimeStamp, const float DeltaTime,
	const FVector& Accel, const FVector& RelativeClientLocation, UPrimitiveComponent* ClientMovementBase,
	const FName ClientBaseBoneName, const uint8 ClientMovementMode)
{
	Super::ServerMoveHandleClientError(ClientTimeStamp, DeltaTime, Accel, RelativeClientLocation,
		ClientMovementBase, ClientBaseBoneName, ClientMovementMode);
	if (ServerPredictionData && ServerPredictionData->PendingAdjustment.TimeStamp == ClientTimeStamp)
	{
		FGuLiShipAngularAdjustment& Snapshot = NetworkStorage->PendingAngularAdjustment;
		Snapshot.TimeStamp = ClientTimeStamp;
		Snapshot.ConfigRevision = MovementSyncState.ConfigRevision;
		Snapshot.BarrierGeneration = MovementSyncState.BarrierGeneration;
		Snapshot.YawVelocity = YawVelocity;
		Snapshot.BankRoll = CurrentBankRoll;
		Snapshot.bValid = true;
	}
}

void UGuLiShipMovementComponent::ClientHandleMoveResponse(const FCharacterMoveResponseDataContainer& MoveResponse)
{
	const FGuLiShipMoveResponseData& ShipResponse = static_cast<const FGuLiShipMoveResponseData&>(MoveResponse);
	const FGuLiShipAngularAdjustment& Snapshot = ShipResponse.AngularAdjustment;
	FGuLiConnectionBootstrapState CurrentIdentity;
	if (!BuildCurrentConnectionIdentity(CurrentIdentity)
		|| !GuLiShipMovement::MatchesIdentity(CurrentIdentity, MovementSyncState.ConnectionIdentity)
		|| AppliedLoadoutRevision != MovementSyncState.RequiredLoadoutRevision)
	{
		return;
	}
	if (Snapshot.ConfigRevision != MovementSyncState.ConfigRevision
		|| Snapshot.BarrierGeneration != MovementSyncState.BarrierGeneration
		|| AppliedBarrierGeneration != MovementSyncState.BarrierGeneration
		|| (MoveResponse.IsCorrection() && !Snapshot.bValid))
	{
		return;
	}
	bApplyingCorrection = MoveResponse.IsCorrection();
	CorrectionTimeStamp = MoveResponse.ClientAdjustment.TimeStamp;
	CorrectionYawVelocity = Snapshot.YawVelocity;
	CorrectionBankRoll = Snapshot.BankRoll;
	Super::ClientHandleMoveResponse(MoveResponse);
	bApplyingCorrection = false;
}

void UGuLiShipMovementComponent::OnClientCorrectionReceived(FNetworkPredictionData_Client_Character& ClientData,
	const float TimeStamp, const FVector NewLocation, const FVector NewVelocity, UPrimitiveComponent* NewBase,
	const FName NewBaseBoneName, const bool bHasBase, const bool bBaseRelativePosition,
	const uint8 ServerMovementMode, const FVector ServerGravityDirection)
{
	Super::OnClientCorrectionReceived(ClientData, TimeStamp, NewLocation, NewVelocity, NewBase,
		NewBaseBoneName, bHasBase, bBaseRelativePosition, ServerMovementMode, ServerGravityDirection);
	// 引擎已确认时间戳仍在 SavedMoves 中才到此钩子；过期/无法解析 base 的响应不会污染积分状态。
	if (bApplyingCorrection && TimeStamp == CorrectionTimeStamp)
	{
		YawVelocity = CorrectionYawVelocity;
		CurrentBankRoll = CorrectionBankRoll;
	}
}
