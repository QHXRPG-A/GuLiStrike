#include "Gameplay/GroundMech/GuLiGroundMechMovementComponent.h"

#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Gameplay/GroundMech/GuLiGroundMassContactSubsystem.h"
#include "Gameplay/GroundMech/GuLiGroundMechRocketComponent.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Network/GuLiNetworkSettings.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiHybridSessionSubsystem.h"
#include "Gameplay/GroundMech/GuLiGroundMechAbilities.h"
#include "AbilitySystemComponent.h"
#include "Engine/GameInstance.h"
#include "Gameplay/Navigation/Grid/GuLiGroundGridSubsystem.h"
#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"
#include "Gameplay/Navigation/Grid/GuLiGridNetwork.h"

namespace
{
constexpr float ObstacleUpdateIntervalSeconds = 0.1f;
constexpr float SupportRetentionToleranceCentimeters = 5.0f;
FBox2D SweepBounds(const FVector &Start, const FVector &End, float Radius)
{
	return FBox2D(FVector2D(FMath::Min(Start.X, End.X) - Radius, FMath::Min(Start.Y, End.Y) - Radius),
				  FVector2D(FMath::Max(Start.X, End.X) + Radius, FMath::Max(Start.Y, End.Y) + Radius));
}
} // namespace

UGuLiGroundMechMovementComponent::UGuLiGroundMechMovementComponent(const FObjectInitializer &Initializer)
	: Super(Initializer)
{
	SetIsReplicatedByDefault(false);
	PrimaryComponentTick.bStartWithTickEnabled=false;
}
UGuLiGroundMechMovementComponent::~UGuLiGroundMechMovementComponent() = default;

// The World logical clock drives local simulation; query actors never run CMC network ticks.
void UGuLiGroundMechMovementComponent::TickComponent(float,ELevelTick,FActorComponentTickFunction*) {}
void UGuLiGroundMechMovementComponent::RefreshCollisionContext()
{
	if (auto *Contacts = GetMassContactSubsystem())
	{
		// Reconnect/match reset invalidates every retained frame, even if the epoch is reused.
		Contacts->CaptureMove(0.0f);
		const uint32 Generation = Contacts->GetCacheGeneration();
		if (PredictionCacheGeneration != 0u && PredictionCacheGeneration != Generation)
		{
			ClearMassSupport(true);
			PreparedMove = {};
			LastMoveContext = {};
			bPreparedMove = false;
		}
		PredictionCacheGeneration = Generation;
	}
}
void UGuLiGroundMechMovementComponent::AdvanceHybrid(float DeltaTime,FVector2D Axis,bool bSprint)
{
	if (!UGuLiNetworkSettings::IsHybrid(GetWorld()) || !UGuLiHybridActorComponent::HasSimulation(GetOwner()) || !HasValidData()) return;
	RefreshCollisionContext();
	const auto* Mech=CastChecked<AGuLiGroundMechCharacter>(CharacterOwner);
	const auto* Ability=Rocket();
	const bool bAirSteering=Ability && Ability->IsConfigured() && (IsFalling()
		|| (Ability->IsInputHeld() && Ability->IsAbilityActive() && Ability->GetFuel()>UE_SMALL_NUMBER));
	const float Magnitude=bAirSteering || bSprint?1.f:Mech->GetWalkSpeed()/MaxWalkSpeed;
	const FVector Intent(Axis.Y,Axis.X,0.);
	Acceleration=ScaleInputAcceleration(ConstrainInputAcceleration(Intent.GetClampedToMaxSize(1.)*Magnitude));
	AnalogInputModifier=ComputeAnalogInputModifier();
	const bool bPreviousPhysicsWithoutController=bRunPhysicsWithNoController;
    bRunPhysicsWithNoController=true;
	PerformMovement(DeltaTime);
    bRunPhysicsWithNoController=bPreviousPhysicsWithoutController;
	UpdateGroundMechObstacle(DeltaTime);
}
void UGuLiGroundMechMovementComponent::AdvanceDormantRecovery(float DeltaTime)
{
    auto* Mech=Cast<AGuLiGroundMechCharacter>(CharacterOwner);
    auto* Ability=Rocket();
    if (!Mech || !Mech->HasAuthority() || !Mech->IsCarrierDormant() || !Mech->GetCombatHealthComponent()->IsAlive()
        || !Ability || !Ability->IsConfigured()
        || !FMath::IsFinite(DeltaTime) || DeltaTime<=0) return;
    if (UGuLiNetworkSettings::IsHybrid(GetWorld()))
    {
        const auto* Binding=UGuLiHybridActorComponent::Find(Mech);
        const auto* Session=GetWorld()->GetGameInstance()->GetSubsystem<UGuLiHybridSessionSubsystem>();
        const auto* Hybrid=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
        if (!Binding || !Session || !Hybrid || Hybrid->IsBattlePaused() || !Binding->GetState().bDormant
            || Binding->GetState().LifeState!=1 || Binding->GetState().StateCustodian!=Session->GetLocalPlayerId()) return;
    }
    ClearRocketInput(); StopMovementImmediately();
    RocketState.Fuel=Ability->GetFuel();
    const auto& Config=Ability->GetConfiguration();
    if (!IsMovingOnGround() && !IsMassSupportMode()) { RocketState.RecoveryElapsed=0; return; }
    const float RecoveryTime=FMath::Max(0.f,RocketState.RecoveryElapsed+DeltaTime-Config.FuelRecoveryDelay);
    RocketState.RecoveryElapsed=FMath::Min(Config.FuelRecoveryDelay,RocketState.RecoveryElapsed+DeltaTime);
    const float NewFuel=FMath::Min(Config.MaxFuel,RocketState.Fuel+RecoveryTime*Config.FuelRecoveryPerSecond);
    auto* ASC=Mech->GetAbilitySystemComponent();
    if (ASC && NewFuel>RocketState.Fuel+1.e-6f)
    {
        auto Spec=ASC->MakeOutgoingSpec(UGuLiGE_MechFuelDelta::StaticClass(),1.f,ASC->MakeEffectContext());
        if (Spec.IsValid())
        {
            Spec.Data->SetSetByCallerMagnitude(TEXT("FuelDelta"),NewFuel-RocketState.Fuel);
            ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data); RocketState.Fuel=NewFuel;
        }
    }
}
FVector UGuLiGroundMechMovementComponent::SweepBlink(const FVector& Delta)
{
	if (!HasValidData() || bMoveContextActive || !UGuLiHybridActorComponent::HasSimulation(GetOwner()))
		return UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
	RefreshCollisionContext(); BeginMoveContext(0.f);
	ContactsThisStep=0; DepenetrationUsed=0;
	FHitResult Hit; SafeMoveUpdatedComponent(Delta,UpdatedComponent->GetComponentQuat(),true,Hit);
	EndMoveContext();
	// Keep vertical momentum/jet state. Clear only stale floor/base/contact anchors.
	ClearMassSupport(true); CharacterOwner->SetBase(nullptr); CurrentFloor.Clear();
	bForceNextFloorCheck=true; bJustTeleported=true;
	PreparedMove={}; LastMoveContext={}; bPreparedMove=false;
	Velocity.X=Velocity.Y=0;
	ObstacleUpdateAccumulator=ObstacleUpdateIntervalSeconds; UpdateGroundMechObstacle(0.f);
	return UpdatedComponent->GetComponentLocation();
}
void UGuLiGroundMechMovementComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	UnregisterGroundMechObstacle();
	PreparedMove = {};
	ActiveMove = {};
	LastMoveContext = {};
	Super::EndPlay(Reason);
}


bool UGuLiGroundMechMovementComponent::IsMassSupportMode() const
{
	return MovementMode == MOVE_Custom && CustomMovementMode == GuLiGroundMechMovement::MassSupportCustomMode;
}
UGuLiGroundMassContactSubsystem *UGuLiGroundMechMovementComponent::GetMassContactSubsystem() const
{
	return GetWorld() ? GetWorld()->GetSubsystem<UGuLiGroundMassContactSubsystem>() : nullptr;
}
void UGuLiGroundMechMovementComponent::BeginMoveContext(float Duration)
{
	check(!bMoveContextActive);
	ActiveMove = bPreparedMove ? PreparedMove : CaptureCollisionMove(Duration);
	ActiveMove.Duration = Duration;
	PreparedMove = {};
	bPreparedMove = false;
	MoveElapsedSeconds = 0;
	bMoveContextActive = true;
	bTouchedMass = false;
	if (SupportState.IsValid() && (!ActiveMove.Snapshot || SupportState.Epoch != ActiveMove.Snapshot->Epoch))
		ClearMassSupport(true);
}
float UGuLiGroundMechMovementComponent::GetMaxSpeed() const
{
	const auto* Ability = Rocket();
	const auto* Mech = Cast<AGuLiGroundMechCharacter>(CharacterOwner);
	if (IsFalling() && Mech && Ability && Ability->IsConfigured())
		return FMath::Max(Mech->GetWalkSpeed()*Ability->GetConfiguration().AirSpeedMultiplier,
			float(GetAirTargetVelocity(bRocketAllowedThisMove && RocketState.Fuel>UE_SMALL_NUMBER).Size()));
	return IsMassSupportMode() ? MaxWalkSpeed : Super::GetMaxSpeed();
}
FVector UGuLiGroundMechMovementComponent::GetAirTargetVelocity(bool bThrust) const
{
	const auto* Ability=Rocket();
	const auto* Mech=Cast<AGuLiGroundMechCharacter>(CharacterOwner);
	if (!Mech || !Ability || !Ability->IsConfigured() || MaxAcceleration<=UE_SMALL_NUMBER) return FVector::ZeroVector;
	const auto& Config=Ability->GetConfiguration();
	const FRotator Facing(0,Mech->GetActorRotation().Yaw,0);
	// Acceleration already holds the normalized world intent from the fixed input step.
	const FVector Input=Facing.UnrotateVector(ProjectToGravityFloor(Acceleration)/MaxAcceleration).GetClampedToMaxSize(1.);
	const float Base=Mech->GetWalkSpeed()*Config.AirSpeedMultiplier;
	const float Forward=bThrust && Input.X>UE_KINDA_SMALL_NUMBER ? MaxWalkSpeed*Config.JetForwardSpeedMultiplier : Base;
	return Facing.RotateVector(FVector(Input.X*Forward,Input.Y*Base,0));
}
float UGuLiGroundMechMovementComponent::GetMaxBrakingDeceleration() const
{
	return IsMassSupportMode() ? BrakingDecelerationWalking : Super::GetMaxBrakingDeceleration();
}
FGuLiGroundMassMoveContext UGuLiGroundMechMovementComponent::CaptureCollisionMove(float Duration)
{
	auto *Contacts = GetMassContactSubsystem();
	auto Move = Contacts ? Contacts->CaptureMove(Duration) : FGuLiGroundMassMoveContext{};
	if (Move.Snapshot && LastMoveContext.Snapshot && Move.Snapshot->Epoch == LastMoveContext.Snapshot->Epoch &&
		Move.Snapshot->CacheGeneration == LastMoveContext.Snapshot->CacheGeneration)
	{
		// Multiple server packets can execute in one world tick. Each consumes the next
		// interval, never the same now-minus-duration interval a second time.
		Move.StartSimulationSeconds =
			FMath::Max(Move.StartSimulationSeconds, LastMoveContext.StartSimulationSeconds + LastMoveContext.Duration);
	}
	if (SupportState.IsValid() && Move.Snapshot && SupportState.Epoch == Move.Snapshot->Epoch)
		Move.StartSimulationSeconds = FMath::Max(Move.StartSimulationSeconds, SupportState.SimulationSeconds);
	return Move;
}
void UGuLiGroundMechMovementComponent::EndMoveContext()
{
	MarkPresentationContacts();
	LastMoveContext = ActiveMove;
	bMoveContextActive = false;
	bSweepStepActive = false;
	bReplayingMove = false;
	ActiveMove = {};
}
void UGuLiGroundMechMovementComponent::BeginSweepStep(float Duration)
{
	check(bMoveContextActive && !bSweepStepActive);
	StepStartElapsed = MoveElapsedSeconds;
	StepDuration = Duration;
	SweepRemainingSeconds = Duration;
	SweepStartSeconds = ActiveMove.StartSimulationSeconds + MoveElapsedSeconds;
	ContactsThisStep = 0;
	DepenetrationUsed = 0;
	bSweepStepActive = true;
}
void UGuLiGroundMechMovementComponent::EndSweepStep()
{
	MoveElapsedSeconds = StepStartElapsed + StepDuration;
	SweepRemainingSeconds = 0;
	bSweepStepActive = false;
}
void UGuLiGroundMechMovementComponent::PerformMovement(float DeltaTime)
{
	BeginMoveContext(DeltaTime);
	auto* Ability = Rocket();
	const bool bRocketConfigured = Ability && Ability->IsConfigured();
	if (bRocketConfigured && !bReplayingMove && (CharacterOwner->IsLocallyControlled()
        || (UGuLiNetworkSettings::IsHybrid(GetWorld()) && UGuLiHybridActorComponent::HasSimulation(CharacterOwner))))
	{
		bRocketHeld = Ability->IsInputHeld();
		bRocketRequested = Ability->IsAbilityActive();
	}
	const bool bGroundedBefore = IsMovingOnGround() || IsMassSupportMode();
	if (bRocketConfigured && CharacterOwner->HasAuthority()) RocketState.Fuel = Ability->GetFuel();
	bRocketAllowedThisMove = bRocketConfigured && bRocketHeld && bRocketRequested &&
		(bReplayingMove || Ability->IsAbilityActive()) && Ability->CanUseRocketControls() &&
		(bGroundedBefore || IsFalling());
	bRocketThrusting = bRocketAllowedThisMove && RocketState.Fuel > UE_SMALL_NUMBER;
	if (bRocketThrusting && bGroundedBefore)
	{
		ResetMassSupportState();
		CharacterOwner->SetBase(nullptr);
		SetMovementMode(MOVE_Falling);
	}
	Super::PerformMovement(DeltaTime);
	// An idle walker still responds to a moving cylinder entering its space. This is a
	// zero-time constraint at the move endpoint, not another advancement of Mass time.
	MoveElapsedSeconds = DeltaTime;
	if (IsMovingOnGround() && UpdatedComponent && CharacterOwner &&
		CharacterOwner->GetLocalRole() != ROLE_SimulatedProxy)
	{
		ContactsThisStep = 0;
		DepenetrationUsed = 0;
		FHitResult Hit;
		SafeMoveUpdatedComponent(FVector::ZeroVector, UpdatedComponent->GetComponentQuat(), true, Hit);
	}
	if (bRocketConfigured)
	{
		const auto& Config = Ability->GetConfiguration();
		if (!bRocketHeld && bGroundedBefore && (IsMovingOnGround() || IsMassSupportMode()))
		{
			const float RecoveryTime = FMath::Max(0.f, RocketState.RecoveryElapsed + DeltaTime - Config.FuelRecoveryDelay);
			RocketState.RecoveryElapsed = FMath::Min(Config.FuelRecoveryDelay, RocketState.RecoveryElapsed + DeltaTime);
			RocketState.Fuel = FMath::Min(Config.MaxFuel, RocketState.Fuel + RecoveryTime * Config.FuelRecoveryPerSecond);
		}
		else RocketState.RecoveryElapsed = 0.f;
		bRocketThrusting = bRocketAllowedThisMove && RocketState.Fuel > UE_SMALL_NUMBER && IsFalling();
		Ability->FinishMovement(RocketState.Fuel, bRocketThrusting, bReplayingMove);
	}
	EndMoveContext();
}
FRotator UGuLiGroundMechMovementComponent::ComputeOrientToMovementRotation(const FRotator& CurrentRotation, float DeltaTime, FRotator& DeltaRotation) const
{
	if (IsFalling())
	{
		const FVector Horizontal(Velocity.X, Velocity.Y, 0.f);
		return Horizontal.SizeSquared() >= 25.f ? FRotator(0.f, Horizontal.Rotation().Yaw, 0.f) : CurrentRotation;
	}
	return Super::ComputeOrientToMovementRotation(CurrentRotation, DeltaTime, DeltaRotation);
}

UGuLiGroundMechRocketComponent* UGuLiGroundMechMovementComponent::Rocket() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UGuLiGroundMechRocketComponent>() : nullptr;
}

void UGuLiGroundMechMovementComponent::StartNewPhysics(float DeltaTime, int32 Iterations)
{
	const bool bOwnContext = !bMoveContextActive;
	if (bOwnContext)
		BeginMoveContext(DeltaTime);
	Super::StartNewPhysics(DeltaTime, Iterations);
	if (bOwnContext)
		EndMoveContext();
}

bool UGuLiGroundMechMovementComponent::SafeMoveUpdatedComponent(const FVector &Delta, const FQuat &Rotation,
																bool bSweep, FHitResult &OutHit, ETeleportType Teleport)
{
	PendingMassContact = {};
	if (!bSweep || Teleport != ETeleportType::None || !HasValidData() ||
		CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy || !bMoveContextActive || !ActiveMove.Snapshot)
		return Super::SafeMoveUpdatedComponent(Delta, Rotation, bSweep, OutHit, Teleport);
	const auto *Capsule = CharacterOwner->GetCapsuleComponent();
	const float Radius = Capsule->GetScaledCapsuleRadius(), HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	const float Duration = bSweepStepActive ? SweepRemainingSeconds : 0.0f;
	const double StartTime =
		bSweepStepActive ? SweepStartSeconds : ActiveMove.StartSimulationSeconds + MoveElapsedSeconds;
	auto *Contacts = GetMassContactSubsystem();
	const FVector Start = UpdatedComponent->GetComponentLocation();
	Contacts->QueryMassBodies(ActiveMove, SweepBounds(Start, Start + Delta, Radius), StartTime, Duration,
							  CandidateBodies);
	const auto Contact = GuLiGroundMassCollision::Sweep(Start, Delta, Radius, HalfHeight, Duration, CandidateBodies,
														SupportState.SoldierId, MovementMode == MOVE_Falling);
	if (Contact.IsValid() && ContactsThisStep >= GuLiGroundMassCollision::MaximumCollisionIterations)
	{
		OutHit = FHitResult(0.0f);
		OutHit.bBlockingHit = true;
		OutHit.Normal = Delta.IsNearlyZero() ? Contact.Normal : -Delta.GetSafeNormal();
		OutHit.ImpactNormal = OutHit.Normal;
		OutHit.Location = Start;
		PendingMassContact = Contact;
		PendingMassContact.Kind = EGuLiGroundMassContactKind::Side;
		PendingMassContact.Normal = OutHit.Normal;
		return false;
	}
	const float Fraction = Contact.IsValid() ? Contact.Time : 1.0f;
	bool bMoved = Super::SafeMoveUpdatedComponent(Delta * Fraction, Rotation, true, OutHit, Teleport);
	if (OutHit.bBlockingHit)
	{
		OutHit.Time *= Fraction;
	}
	else if (Contact.IsValid())
	{
		++ContactsThisStep;
		bTouchedMass = true;
		PendingMassContact = Contact;
		if (Contact.Kind == EGuLiGroundMassContactKind::Side)
		{
			Contacts->RecordSideHits(1);
			const float Push = Contact.Penetration + GuLiGroundMassCollision::ContactToleranceCentimeters;
			const float Available =
				FMath::Max(0.0f, GuLiGroundMassCollision::MaximumDepenetrationCentimeters - DepenetrationUsed);
			const float Correction = FMath::Min(Push, Available);
			if (Correction > 0.0f)
			{
				FHitResult PushHit;
				const FVector Before = UpdatedComponent->GetComponentLocation();
				Super::SafeMoveUpdatedComponent(Contact.Normal * Correction, Rotation, true, PushHit);
				DepenetrationUsed += FVector::Dist(Before, UpdatedComponent->GetComponentLocation());
			}
			// A vertical entry may already be deep inside the expanded XY circle. If a
			// world wall/budget prevents lateral separation, stop the intrusive descent.
			const float Separation = FVector::Dist2D(UpdatedComponent->GetComponentLocation(), Contact.Body.Location);
			if (Separation + 0.01f < Radius + Contact.Body.RadiusCentimeters && Delta.Z < 0.0f)
				PendingMassContact.Normal = FVector::UpVector;
		}
		OutHit = FHitResult(Fraction);
		OutHit.bBlockingHit = true;
		OutHit.TraceStart = Start;
		OutHit.TraceEnd = Start + Delta;
		OutHit.Location = UpdatedComponent->GetComponentLocation();
		OutHit.Normal = PendingMassContact.Normal;
		OutHit.ImpactNormal = OutHit.Normal;
		OutHit.ImpactPoint =
			OutHit.Location -
			(Contact.Kind == EGuLiGroundMassContactKind::Top ? FVector(0, 0, HalfHeight) : Contact.Normal * Radius);
		bMoved |= !UpdatedComponent->GetComponentLocation().Equals(Start);
	}
	if (bSweepStepActive)
	{
		const float Consumed = Duration * OutHit.Time;
		SweepStartSeconds += Consumed;
		MoveElapsedSeconds += Consumed;
		SweepRemainingSeconds -= Consumed;
	}
	return bMoved;
}

bool UGuLiGroundMechMovementComponent::IsMassHit(const FHitResult &Hit) const
{
	return PendingMassContact.IsValid() && Hit.bBlockingHit && !Hit.GetComponent() &&
		   Hit.Normal.Equals(PendingMassContact.Normal, 0.001f);
}
bool UGuLiGroundMechMovementComponent::CanStepUp(const FHitResult &Hit) const
{
	return !IsMassHit(Hit) && Super::CanStepUp(Hit);
}
bool UGuLiGroundMechMovementComponent::StepUp(const FVector &GravDir, const FVector &Delta, const FHitResult &Hit,
											  FStepDownResult *Out)
{
	if (IsMassHit(Hit))
		return false;
	const float Remaining = SweepRemainingSeconds;
	bool bStepped = false;
	{
		TGuardValue<bool> NoTemporalAdvance(bSweepStepActive, false);
		bStepped = Super::StepUp(GravDir, Delta, Hit, Out);
	}
	if (bStepped)
	{
		MoveElapsedSeconds += Remaining;
		SweepStartSeconds += Remaining;
		SweepRemainingSeconds = 0;
	}
	return bStepped;
}
void UGuLiGroundMechMovementComponent::MoveAlongFloor(const FVector &InVelocity, float DeltaSeconds,
													  FStepDownResult *Out)
{
	// Remote proxies are driven by SimulateMovement/MoveSmooth, outside prediction.
	if (CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
	{
		Super::MoveAlongFloor(InVelocity, DeltaSeconds, Out);
		return;
	}
	if (!CurrentFloor.IsWalkableFloor())
		return;
	BeginSweepStep(DeltaSeconds);
	const FVector Delta = ProjectToGravityFloor(InVelocity) * DeltaSeconds;
	FVector Ramp = ComputeGroundMovementDelta(Delta, CurrentFloor.HitResult, CurrentFloor.bLineTrace);
	FHitResult Hit;
	SafeMoveUpdatedComponent(Ramp, UpdatedComponent->GetComponentQuat(), true, Hit);
	if (Hit.IsValidBlockingHit() && !IsMassHit(Hit) && IsWalkable(Hit) && Hit.Time > 0.0f)
	{
		Ramp = ComputeGroundMovementDelta(ProjectToGravityFloor(InVelocity) * SweepRemainingSeconds, Hit, false);
		SafeMoveUpdatedComponent(Ramp, UpdatedComponent->GetComponentQuat(), true, Hit);
	}
	if (Hit.bBlockingHit && SweepRemainingSeconds > MIN_TICK_TIME)
	{
		const FVector Remaining = ProjectToGravityFloor(InVelocity) * SweepRemainingSeconds;
		if (!CanStepUp(Hit) || !StepUp(GetGravityDirection(), Remaining, Hit, Out))
		{
			HandleImpact(Hit, SweepRemainingSeconds, Remaining);
			SlideAlongSurface(Remaining, 1.0f, Hit.Normal, Hit, true);
		}
	}
	EndSweepStep();
}

void UGuLiGroundMechMovementComponent::PhysFalling(float DeltaTime, int32 Iterations)
{
	// The mech owns the falling integration so every sweep/slide receives its actual
	// time slice. World landing, impact callbacks and prediction remain CMC paths.
	float Remaining = DeltaTime;
	while (Remaining >= MIN_TICK_TIME && Iterations < MaxSimulationIterations && HasValidData() && IsFalling())
	{
		++Iterations;
		float Step = GetSimulationTimeStep(Remaining, Iterations);
		const auto* Ability = Rocket();
		const bool bThrust = bRocketAllowedThisMove && RocketState.Fuel > UE_SMALL_NUMBER && Ability;
		if (bThrust) Step = FMath::Min(Step, RocketState.Fuel / Ability->GetConfiguration().FuelDrainPerSecond);
		Remaining -= Step;
		BeginSweepStep(Step);
		RestorePreAdditiveRootMotionVelocity();
		const FVector OldVelocity = Velocity;
		if (!HasAnimRootMotion() && !CurrentRootMotion.HasOverrideVelocity())
		{
			const bool bAirSteering = Ability && Ability->IsConfigured();
			Velocity = ProjectToGravityFloor(Velocity);
			if (bAirSteering)
			{
				const auto* Mech=CastChecked<AGuLiGroundMechCharacter>(CharacterOwner);
				const float Base=Mech->GetWalkSpeed()*Ability->GetConfiguration().AirSpeedMultiplier;
				// Preserve world inertia. Releasing thrust/W changes the target, never
				// instantly clips the current velocity or rotates it with the body.
				const FVector Target=Acceleration.IsNearlyZero() ? Velocity.GetClampedToMaxSize(Base) : GetAirTargetVelocity(bThrust);
				const float Rate=Velocity.SizeSquared()>Target.SizeSquared()+UE_KINDA_SMALL_NUMBER
					? Mech->GetMobilityConfiguration().Mobility.Braking : MaxAcceleration;
				Velocity=FMath::VInterpConstantTo(Velocity,Target,Step,Rate);
			}
			else
			{
				TGuardValue<FVector> RestoreAcceleration(Acceleration,ProjectToGravityFloor(GetFallingLateralAcceleration(Step)));
				CalcVelocity(Step,FallingLateralFriction,false,GetMaxBrakingDeceleration());
			}
			Velocity += GetGravitySpaceComponentZ(OldVelocity);
		}
		// Faster descent is independent of powered ascent and its existing tuning.
		// Derive it from this substep's velocity/ability state so replay needs no extra state.
		const float FallGravity = !bThrust && GetGravitySpaceZ(Velocity) <= 0.f
			&& Ability && Ability->IsConfigured() ? Ability->GetConfiguration().FallGravityMultiplier : 1.f;
		Velocity = NewFallVelocity(Velocity, -GetGravityDirection() * GetGravityZ() * FallGravity, Step);
		if (bThrust)
		{
			const auto& Config = Ability->GetConfiguration();
			Velocity.Z = FMath::Min(Config.MaxRiseSpeed, Velocity.Z + Config.ThrustAcceleration * Step);
			RocketState.Fuel = FMath::Max(0.f, RocketState.Fuel - Config.FuelDrainPerSecond * Step);
		}
		ApplyRootMotionToVelocity(Step);
		if (bNotifyApex && GetGravitySpaceZ(Velocity) < 0)
		{
			bNotifyApex = false;
			NotifyJumpApex();
		}
		FVector Delta = (OldVelocity + Velocity) * (0.5f * Step);
		for (int32 ContactIndex = 0; ContactIndex < 3 && SweepRemainingSeconds >= MIN_TICK_TIME; ++ContactIndex)
		{
			const float SegmentSeconds = SweepRemainingSeconds;
			FHitResult Hit;
			SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
			if (!HasValidData())
			{
				bSweepStepActive = false;
				return;
			}
			if (!Hit.bBlockingHit)
				break;
			if (IsValidLandingSpot(UpdatedComponent->GetComponentLocation(), Hit))
			{
				const float Unused = SweepRemainingSeconds;
				bSweepStepActive = false;
				ProcessLanded(Hit, Remaining + Unused, Iterations);
				return;
			}
			HandleImpact(Hit, SegmentSeconds, Delta);
			if (!IsFalling())
			{
				bSweepStepActive = false;
				StartNewPhysics(Remaining + SweepRemainingSeconds, Iterations);
				return;
			}
			Delta = ComputeSlideVector(Velocity * SweepRemainingSeconds, 1.0f, Hit.Normal, Hit);
			if (SweepRemainingSeconds > MIN_TICK_TIME)
				Velocity = Delta / SweepRemainingSeconds;
		}
		EndSweepStep();
	}
}
void UGuLiGroundMechMovementComponent::PhysFlying(float DeltaTime, int32 Iterations)
{
	float Remaining = DeltaTime;
	while (Remaining >= MIN_TICK_TIME && Iterations < MaxSimulationIterations && MovementMode == MOVE_Flying)
	{
		const float Step = GetSimulationTimeStep(Remaining, ++Iterations);
		Remaining -= Step;
		BeginSweepStep(Step);
		Super::PhysFlying(Step, Iterations);
		EndSweepStep();
	}
}
bool UGuLiGroundMechMovementComponent::IsValidLandingSpot(const FVector &Location, const FHitResult &Hit) const
{
	if (IsMassHit(Hit))
		return PendingMassContact.Kind == EGuLiGroundMassContactKind::Top;
	return Super::IsValidLandingSpot(Location, Hit);
}
void UGuLiGroundMechMovementComponent::SetPostLandedPhysics(const FHitResult &Hit)
{
	if (IsMassHit(Hit) && PendingMassContact.Kind == EGuLiGroundMassContactKind::Top)
	{
		SetMassSupport(PendingMassContact);
		Velocity.Z = 0.0f;
		GetMassContactSubsystem()->RecordSupportContact();
		return;
	}
	Super::SetPostLandedPhysics(Hit);
}
void UGuLiGroundMechMovementComponent::SetMassSupport(const FGuLiGroundMassContact &Contact)
{
	SupportState.SoldierId = Contact.Body.SoldierId;
	SupportState.Epoch = ActiveMove.Snapshot->Epoch;
	SupportState.DisplacementRevision = Contact.Body.DisplacementRevision;
	SupportState.BodyLocation = Contact.Body.Location;
	SupportState.SourceLocation = Contact.Body.Location;
	SupportState.SourceTopZ = Contact.Body.TopZ;
	SupportState.bHasSourceReference = true;
	SupportState.TopZ = Contact.Body.TopZ;
	SupportState.Radius = Contact.Body.RadiusCentimeters;
	SupportState.RelativeLocation = UpdatedComponent->GetComponentLocation() - Contact.Body.Location;
	SupportState.SimulationSeconds = SweepStartSeconds;
	if (CharacterOwner->HasAuthority())
		MassSupportSoldierId = SupportState.SoldierId;
	CharacterOwner->SetBase(nullptr);
	SetMovementMode(MOVE_Custom, GuLiGroundMechMovement::MassSupportCustomMode);
}
void UGuLiGroundMechMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
	if (!IsMassSupportMode())
	{
		Super::PhysCustom(DeltaTime, Iterations);
		return;
	}
	if (!HasValidData() || DeltaTime < MIN_TICK_TIME || CharacterOwner->GetLocalRole() == ROLE_SimulatedProxy)
		return;
	float Remaining = DeltaTime;
	while (Remaining >= MIN_TICK_TIME && Iterations < MaxSimulationIterations && IsMassSupportMode())
	{
		const float Step = GetSimulationTimeStep(Remaining, ++Iterations);
		Remaining -= Step;
		const double At = ActiveMove.StartSimulationSeconds + MoveElapsedSeconds;
		FGuLiGroundMassBody Body;
		if (!SupportState.IsValid() || !ActiveMove.Snapshot ||
			!ActiveMove.Snapshot->Find(SupportState.SoldierId, At + Step, Body) ||
			Body.DisplacementRevision != SupportState.DisplacementRevision)
		{
			ClearMassSupport(true);
			StartNewPhysics(Remaining + Step, Iterations);
			return;
		}
		const FVector Start = UpdatedComponent->GetComponentLocation();
		// Compare against the last integrated platform position before applying its translation.
		if (FVector::DistSquared2D(Start, SupportState.BodyLocation) >
			FMath::Square(SupportState.Radius + GuLiGroundMassCollision::ContactToleranceCentimeters))
		{
			ClearMassSupport(true);
			StartNewPhysics(Remaining + Step, Iterations);
			return;
		}
		if (!SupportState.bHasSourceReference)
		{
			FGuLiGroundMassBody Before;
			const bool bFound = ActiveMove.Snapshot->Find(SupportState.SoldierId, At, Before);
			check(bFound); // Same immutable frame and ID already resolved at the step endpoint.
			SupportState.SourceLocation = Before.Location;
			SupportState.SourceTopZ = Before.TopZ;
			SupportState.bHasSourceReference = true;
		}
		const FVector SourceLocation = Body.Location;
		const float SourceTop = Body.TopZ;
		Body.Location = SupportState.BodyLocation + (SourceLocation - SupportState.SourceLocation);
		Body.TopZ = SupportState.TopZ + (SourceTop - SupportState.SourceTopZ);
		BeginSweepStep(Step);
		Acceleration.Z = 0.0f;
		CalcVelocity(Step, GroundFriction, false, GetMaxBrakingDeceleration());
		Velocity.Z = 0.0f;
		const float HalfHeight = CharacterOwner->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		FVector Delta = Velocity * Step + (Body.Location - SupportState.BodyLocation);
		Delta.Z = Body.TopZ + HalfHeight - Start.Z;
		FHitResult Hit;
		SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
		if (Hit.IsValidBlockingHit())
		{
			HandleImpact(Hit, Step, Delta);
			SlideAlongSurface(Delta, 1.0f - Hit.Time, Hit.Normal, Hit, true);
		}
		EndSweepStep();
		const FVector End = UpdatedComponent->GetComponentLocation();
		const bool bInside =
			FVector::DistSquared2D(End, Body.Location) <=
			FMath::Square(Body.RadiusCentimeters + GuLiGroundMassCollision::ContactToleranceCentimeters);
		const bool bAtHeight = FMath::Abs(End.Z - HalfHeight - Body.TopZ) <= SupportRetentionToleranceCentimeters;
		if (!bInside || !bAtHeight)
		{
			ClearMassSupport(true);
			StartNewPhysics(Remaining, Iterations);
			return;
		}
		SupportState.BodyLocation = Body.Location;
		SupportState.SourceLocation = SourceLocation;
		SupportState.SourceTopZ = SourceTop;
		SupportState.TopZ = Body.TopZ;
		SupportState.Radius = Body.RadiusCentimeters;
		SupportState.RelativeLocation = End - Body.Location;
		SupportState.SimulationSeconds = bReplayingMove ? SupportState.SimulationSeconds + Step : At + Step;
	}
}
void UGuLiGroundMechMovementComponent::ResetMassSupportState()
{
	SupportState = {};
	if (CharacterOwner && CharacterOwner->HasAuthority())
		MassSupportSoldierId.Reset();
}
void UGuLiGroundMechMovementComponent::ClearMassSupport(bool bEnterFalling)
{
	const bool bWasSupport = IsMassSupportMode();
	ResetMassSupportState();
	if (!bEnterFalling || !bWasSupport)
		return;
	FFindFloorResult Floor;
	if (UpdatedComponent)
		FindFloor(UpdatedComponent->GetComponentLocation(), Floor, false);
	SetMovementMode(Floor.IsWalkableFloor() ? MOVE_Walking : MOVE_Falling);
}
void UGuLiGroundMechMovementComponent::OnMovementModeChanged(EMovementMode Previous, uint8 PreviousCustom)
{
	Super::OnMovementModeChanged(Previous, PreviousCustom);
	if ((Previous == MOVE_Custom && PreviousCustom == GuLiGroundMechMovement::MassSupportCustomMode &&
		 !IsMassSupportMode()) ||
		MovementMode == MOVE_Flying)
		ResetMassSupportState();
}
void UGuLiGroundMechMovementComponent::HandleImpact(const FHitResult &Hit, float TimeSlice, const FVector &Delta)
{
	if (!IsMassHit(Hit))
		Super::HandleImpact(Hit, TimeSlice, Delta);
}
void UGuLiGroundMechMovementComponent::ApplyExternalDisplacement(const FTransform &Transform)
{
	if (!CharacterOwner || !CharacterOwner->HasAuthority())
		return;
	if (auto* Ability = Rocket()) Ability->Interrupt();
	ClearMassSupport(true);
	PreparedMove = {};
	LastMoveContext = {};
	bPreparedMove = false;
	ObstacleUpdateAccumulator = ObstacleUpdateIntervalSeconds;
	Super::ApplyExternalDisplacement(Transform);
}
void UGuLiGroundMechMovementComponent::OnTeleported()
{
    if (auto* Ability = Rocket()) Ability->Interrupt();
    ClearMassSupport(true);
    PreparedMove = {};
    bPreparedMove = false;
    ObstacleUpdateAccumulator = ObstacleUpdateIntervalSeconds;
    Super::OnTeleported();
}
void UGuLiGroundMechMovementComponent::MarkPresentationContacts()
{
	if (!CharacterOwner || !CharacterOwner->IsLocallyControlled() || !UpdatedComponent || !ActiveMove.Snapshot)
		return;
	auto *Contacts = GetMassContactSubsystem();
	const FVector Location = UpdatedComponent->GetComponentLocation();
	const auto *Capsule = CharacterOwner->GetCapsuleComponent();
	const double At = ActiveMove.StartSimulationSeconds + ActiveMove.Duration;
	Contacts->QueryMassBodies(ActiveMove, SweepBounds(Location, Location, Capsule->GetScaledCapsuleRadius() + 2.0f), At,
							  0.0f, CandidateBodies);
	for (const auto &Body : CandidateBodies)
		if (GuLiGroundMassCollision::HasVerticalOverlap(Location.Z, Capsule->GetScaledCapsuleHalfHeight(), Body.BottomZ,
														Body.TopZ) &&
			FVector::DistSquared2D(Location, Body.Location) <=
				FMath::Square(Capsule->GetScaledCapsuleRadius() + Body.RadiusCentimeters + 2.0f))
			Contacts->MarkLocalContact(Body);
	FGuLiGroundMassBody Body;
	if (SupportState.IsValid() && ActiveMove.Snapshot->Find(SupportState.SoldierId, At, Body))
	{
		Body.Location = SupportState.BodyLocation;
		Body.TopZ = SupportState.TopZ;
		Contacts->MarkLocalContact(Body);
	}
}




#if WITH_DEV_AUTOMATION_TESTS
void UGuLiGroundMechMovementComponent::TestOnly_Replay(const FGuLiGroundMassMoveContext &Context, float Duration)
{
	PreparedMove = Context;
	bPreparedMove = true;
	bReplayingMove = true;
	StartNewPhysics(Duration, 0);
}
#endif

void UGuLiGroundMechMovementComponent::UpdateGroundMechObstacle(const float DeltaTime)
{
	if (!CharacterOwner || !CharacterOwner->HasAuthority() || !GetWorld())
		return;
    if (const auto* Binding=UGuLiHybridActorComponent::Find(CharacterOwner))
        GuLiGrid::ObserveRemoteBody(*GetWorld(),Binding->GetState(),CharacterOwner->GetActorLocation(),1,IsMovingOnGround());
	const AGuLiBattlePlayerState *State = CharacterOwner->GetPlayerState<AGuLiBattlePlayerState>();
	const EGuLiTeam Team = State ? State->GetTeam() : EGuLiTeam::Unassigned;
	const bool bShouldRegister = IsMovingOnGround() && (Team == EGuLiTeam::Red || Team == EGuLiTeam::Blue);
	if (!bShouldRegister)
	{
		UnregisterGroundMechObstacle();
		return;
	}
	const UCapsuleComponent *Capsule = CharacterOwner->GetCapsuleComponent();
	UGuLiDynamicObstacleRegistrySubsystem *Registry = GetWorld()->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>();
	if (!Capsule || !Registry)
		return;
	float Radius = 0.0f;
	float HalfHeight = 0.0f;
	Capsule->GetScaledCapsuleSize(Radius, HalfHeight);
	FGuLiDynamicObstacle Obstacle;
	Obstacle.Location = CharacterOwner->GetActorLocation() - FVector(0.0f, 0.0f, HalfHeight);
	Obstacle.RadiusCentimeters = Radius;
	Obstacle.Kind = EGuLiDynamicObstacleKind::GroundMech;
	Obstacle.Team = Team;
	if (!GroundMechObstacleHandle.IsValid())
	{
		GroundMechObstacleHandle = Registry->RegisterObstacle(Obstacle);
		RegisteredObstacleTeam = Team;
		ObstacleUpdateAccumulator = 0.0f;
		return;
	}
	ObstacleUpdateAccumulator += FMath::Max(0.0f, DeltaTime);
	if (RegisteredObstacleTeam != Team || ObstacleUpdateAccumulator >= ObstacleUpdateIntervalSeconds)
	{
		if (Registry->UpdateObstacle(GroundMechObstacleHandle, Obstacle) == EGuLiObstacleUpdateResult::NotFound)
			GroundMechObstacleHandle = Registry->RegisterObstacle(Obstacle);
		RegisteredObstacleTeam = Team;
		ObstacleUpdateAccumulator = 0.0f;
	}
}

void UGuLiGroundMechMovementComponent::UnregisterGroundMechObstacle()
{
	if (!GroundMechObstacleHandle.IsValid())
		return;
	if (UWorld *World = GetWorld())
	{
		if (UGuLiDynamicObstacleRegistrySubsystem *Registry =
				World->GetSubsystem<UGuLiDynamicObstacleRegistrySubsystem>())
			Registry->UnregisterObstacle(GroundMechObstacleHandle);
	}
	GroundMechObstacleHandle.Reset();
	RegisteredObstacleTeam = EGuLiTeam::Unassigned;
	ObstacleUpdateAccumulator = 0.0f;
}
