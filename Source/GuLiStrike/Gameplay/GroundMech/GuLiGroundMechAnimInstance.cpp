#include "Gameplay/GroundMech/GuLiGroundMechAnimInstance.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/GuLiGroundMechMovementComponent.h"
#include "Gameplay/GroundMech/GuLiGroundMechRocketComponent.h"
#include "Components/SkeletalMeshComponent.h"

void UGuLiGroundMechAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	PreviousPawn.Reset();
	PreviousController.Reset();
	++AnimationStateRevision;
}

void UGuLiGroundMechAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	auto* Pawn = Cast<AGuLiGroundMechCharacter>(TryGetPawnOwner());
	const auto* Move = Pawn ? Cast<UGuLiGroundMechMovementComponent>(Pawn->GetCharacterMovement()) : nullptr;
	if (!Pawn || !Move)
	{
		Speed = TurnRate = 0.f;
		HorizontalVelocity = LocalHorizontalVelocity = FVector::ZeroVector;
		bIsGrounded = true; bIsAirborne = bIsThrusting = false;
		return;
	}
	const float Yaw = Pawn->GetActorRotation().Yaw;
	const bool bReset = PreviousPawn.Get() != Pawn || PreviousController.Get() != Pawn->GetController()
		|| PreviousDisplacementRevision != Move->GetDisplacementRevision();
	if (bReset)
	{
		++AnimationStateRevision;
		TurnRate = 0.f;
		PreviousYaw = Yaw;
	}
	PreviousPawn = Pawn;
	PreviousController = Pawn->GetController();
	PreviousDisplacementRevision = Move->GetDisplacementRevision();
	HorizontalVelocity = FVector(Pawn->GetVelocity().X, Pawn->GetVelocity().Y, 0.f);
	LocalHorizontalVelocity = GetSkelMeshComponent()->GetComponentTransform().InverseTransformVectorNoScale(HorizontalVelocity);
	Speed = HorizontalVelocity.Size();
	if (DeltaSeconds > UE_SMALL_NUMBER)
		TurnRate = FMath::FInterpTo(TurnRate, FMath::FindDeltaAngleDegrees(PreviousYaw, Yaw) / DeltaSeconds, DeltaSeconds, 8.f);
	PreviousYaw = Yaw;
	bIsGrounded = Move->IsGroundedForAnimation();
	bIsAirborne = Move->IsFalling();
	bIsThrusting = Pawn->GetRocketJump()->IsThrusting();
}
