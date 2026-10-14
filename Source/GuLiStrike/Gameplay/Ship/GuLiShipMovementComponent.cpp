// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Ship/GuLiShipMovementComponent.h"
#include "Gameplay/Ship/GuLiStrikeShip.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"

#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"

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

UGuLiShipMovementComponent::UGuLiShipMovementComponent(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsReplicatedByDefault(false);
	PrimaryComponentTick.bStartWithTickEnabled=false;
	GravityScale = 0.0f;
	NetworkMaxSmoothUpdateDistance = 51.2f;
	NetworkNoSmoothUpdateDistance = 76.8f;
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

void UGuLiShipMovementComponent::BeginPlay()
{
	Super::BeginPlay();
	SetMovementMode(MOVE_Flying);
	SetComponentTickEnabled(false);
	if (UGuLiHybridActorComponent::HasSimulation(GetOwner()))
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
	return UGuLiHybridActorComponent::HasSimulation(GetOwner()) ? CanonicalMoveHistory.GetEpoch() : 0u;
}

uint32 UGuLiShipMovementComponent::GetCanonicalMoveRevision() const
{
	return UGuLiHybridActorComponent::HasSimulation(GetOwner()) ? CanonicalMoveHistory.GetLatestRevision() : 0u;
}

EGuLiShipCanonicalMoveLookupResult UGuLiShipMovementComponent::LookupCanonicalMove(
	const uint32 CanonicalEpoch, const uint32 MoveRevision, FGuLiShipCanonicalMoveState& OutState) const
{
	if (!UGuLiHybridActorComponent::HasSimulation(GetOwner()))
	{
		OutState = FGuLiShipCanonicalMoveState();
		return EGuLiShipCanonicalMoveLookupResult::Unavailable;
	}
	return CanonicalMoveHistory.Lookup(CanonicalEpoch, MoveRevision, OutState);
}

bool UGuLiShipMovementComponent::GetLatestCanonicalMove(FGuLiShipCanonicalMoveState& OutState) const
{
	OutState = FGuLiShipCanonicalMoveState();
	if (!UGuLiHybridActorComponent::HasSimulation(GetOwner()))
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
	if (!UGuLiHybridActorComponent::HasSimulation(GetOwner()) || !CharacterOwner || !GetWorld()
		|| MovementSyncState.ConfigRevision == 0u || MovementSyncState.BarrierGeneration == 0u)
	{
		return;
	}
	const FTransform Transform = CharacterOwner->GetActorTransform();
	if (Transform.ContainsNaN() || Velocity.ContainsNaN())
	{
		return;
	}
	const double ServerWorldTimeSeconds = UGuLiHybridActorComponent::BattleSeconds(GetOwner());
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

void UGuLiShipMovementComponent::FreezeCarrierMotion()
{ ClearFlightInput(); StopMovementImmediately(); YawVelocity=0; }

void UGuLiShipMovementComponent::ClearFlightInput()
{
	PendingInput = FGuLiShipFlightInput();
	ActiveInput = FGuLiShipFlightInput();
	if (CharacterOwner)
	{
		CharacterOwner->ConsumeMovementInputVector();
	}
}

FGuLiShipFlightInput UGuLiShipMovementComponent::ConsumeHybridControllerInput()
{
	auto Input=PendingInput; Input.LocalThrust=Input.LocalThrust.BoundToCube(1.0);
	Input.Turn=FMath::Clamp(Input.Turn,-1.f,1.f); Input.Strafe=FMath::Clamp(Input.Strafe,-1.f,1.f);
	PendingInput={}; PendingInput.bBoost=Input.bBoost;
	if (CharacterOwner) CharacterOwner->ConsumeMovementInputVector();
	return Input;
}

void UGuLiShipMovementComponent::AdvanceHybridFlight(float DeltaSeconds,const FGuLiHybridShipInput& Input)
{
	if (!CharacterOwner || !UGuLiHybridActorComponent::HasSimulation(CharacterOwner)) return;
	ActiveInput.LocalThrust=Input.Thrust; ActiveInput.Turn=Input.Turn; ActiveInput.Strafe=Input.Strafe; ActiveInput.bBoost=Input.bBoost;
	ActiveInput.ConfigRevision=MovementSyncState.ConfigRevision; ActiveInput.BarrierGeneration=MovementSyncState.BarrierGeneration;
	ActiveInput.bSimulationEnabled=IsMovementConfigReady(); bRunPhysicsWithNoController=true;
	PerformMovement(DeltaSeconds);
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

// Only the World logical clock invokes AdvanceHybridFlight. Engine render ticks
// and inherited movement RPCs never advance a query or an ungranted carrier.
void UGuLiShipMovementComponent::TickComponent(float,ELevelTick,FActorComponentTickFunction*) {}

bool UGuLiShipMovementComponent::CommitServerMovementConfig(const FGuLiShipMovementConfig& Config,uint32 RequiredLoadoutRevision)
{
    if (!GetOwner() || bEndingPlay || !Config.IsValid() || !RequiredLoadoutRevision) return false;
    if (MovementSyncState.ConfigRevision && MovementSyncState.Config.Equals(Config)
        && MovementSyncState.RequiredLoadoutRevision==RequiredLoadoutRevision) return true;
    MovementSyncState.Config=Config;
    MovementSyncState.RequiredLoadoutRevision=RequiredLoadoutRevision;
    MovementSyncState.ConfigRevision=GuLiShipMovement::NextRevision(MovementSyncState.ConfigRevision);
    MovementSyncState.BarrierGeneration=GuLiShipMovement::NextRevision(MovementSyncState.BarrierGeneration);
    ApplyMovementParameters(Config);
    return true;
}

void UGuLiShipMovementComponent::SetAppliedLoadoutRevision(uint32 Revision)
{ AppliedLoadoutRevision=Revision; }

bool UGuLiShipMovementComponent::IsMovementConfigReady() const
{
    return !bEndingPlay && CharacterOwner && UGuLiHybridActorComponent::Find(CharacterOwner)
        && MovementSyncState.ConfigRevision && MovementSyncState.BarrierGeneration && EffectiveConfig.IsValid()
        && AppliedLoadoutRevision && AppliedLoadoutRevision==MovementSyncState.RequiredLoadoutRevision;
}

void UGuLiShipMovementComponent::ApplyExternalDisplacement(const FTransform& Transform)
{
    if (!CharacterOwner || !CharacterOwner->HasAuthority() || Transform.ContainsNaN()) return;
    if (UGuLiNetworkSettings::IsHybrid(GetWorld()))
    {
        const auto* Binding=UGuLiHybridActorComponent::Find(CharacterOwner);
        const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        // Applying a committed displacement is also allowed while battle time is
        // paused. It does not advance movement or create another network event.
        if (!Binding || !Hybrid || !Hybrid->HasSimulation(Binding->GetObjectKey())) return;
    }
    CharacterOwner->SetActorTransform(Transform,false,nullptr,ETeleportType::TeleportPhysics);
    CharacterOwner->SetBase(nullptr); StopMovementImmediately(); ClearFlightInput();
    YawVelocity=0; CurrentBankRoll=Transform.Rotator().Roll; bJustTeleported=true;
    MovementSyncState.BarrierGeneration=GuLiShipMovement::NextRevision(MovementSyncState.BarrierGeneration);
    ResetCanonicalMoveHistory(); RecordCanonicalMoveAfterAuthoritySimulation();
}

bool UGuLiShipMovementComponent::CanSimulateCurrentMove() const
{
    return !UGuLiExternalUnitControlComponent::AreActorActionsLocked(GetOwner())
        && UGuLiHybridActorComponent::HasSimulation(CharacterOwner)
        && HasValidData() && IsMovementConfigReady() && ActiveInput.bSimulationEnabled;
}

void UGuLiShipMovementComponent::ResetPredictionData_Client()
{
    // ACharacter invokes this on possession/restart even without a NetDriver.
    Super::ResetPredictionData_Client(); ClearFlightInput();
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
		// 原转向在 CMC 位移之后执行；现在放进同一次 SavedMove，仍只积分一次。
		IntegrateFlightRotation(DeltaTime, WorldThrustIntent);
		RecordCanonicalMoveAfterAuthoritySimulation();
	}
}
