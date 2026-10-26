#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/GuLiGroundMechMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"

void AGuLiGroundMechCharacter::ResetBlinkCamera()
{
    BlinkCameraOffset=BlinkCameraVelocity=FVector::ZeroVector;
    BlinkCameraHoldRemaining=0.f;
    BlinkCameraDisplacementRevision=CastChecked<UGuLiGroundMechMovementComponent>(GetCharacterMovement())->GetDisplacementRevision();
    UpdateMechCamera(0.f);
}

void AGuLiGroundMechCharacter::AddBlinkCameraDisplacement(const FVector& Delta,bool bFirst)
{
    if (!IsLocallyControlled() || !MobilityConfiguration.bReady) return;
    if (bFirst)
    {
        const auto& C=bShoulderAimHeld?MobilityConfiguration.AimCamera:MobilityConfiguration.FreeCamera;
        BlinkCameraHoldRemaining=C.BlinkHoldSeconds;
        BlinkCameraSettleSeconds=C.BlinkCatchupSeconds;
        BlinkCameraDisplacementRevision=CastChecked<UGuLiGroundMechMovementComponent>(GetCharacterMovement())->GetDisplacementRevision();
    }
    // Cancel only the actual swept ability displacement. Mouse orbit and ordinary
    // movement retain their existing response. Multiple fixed steps accumulate.
    BlinkCameraOffset-=Delta;
    UpdateMechCamera(0.f);
}

void AGuLiGroundMechCharacter::UpdateMechCamera(float DeltaSeconds)
{
    if (!MobilityConfiguration.bReady || !CameraBoom || !Camera) return;
    const uint32 Revision=CastChecked<UGuLiGroundMechMovementComponent>(GetCharacterMovement())->GetDisplacementRevision();
    if (Revision!=BlinkCameraDisplacementRevision || !IsLocallyControlled())
    {
        BlinkCameraOffset=BlinkCameraVelocity=FVector::ZeroVector;
        BlinkCameraHoldRemaining=0.f; BlinkCameraDisplacementRevision=Revision;
    }
    float Dt=FMath::Max(0.f,DeltaSeconds);
    const float Held=FMath::Min(Dt,BlinkCameraHoldRemaining);
    BlinkCameraHoldRemaining-=Held; Dt-=Held;
    if (Dt>0.f)
    {
        // Exact critically damped spring integration: no frame-rate dependent
        // interpolation, no overshoot. 4.744 settles a stationary target by 95%.
        const float Omega=4.744f/BlinkCameraSettleSeconds;
        const float Decay=FMath::Exp(-Omega*Dt);
        const FVector C=BlinkCameraVelocity+Omega*BlinkCameraOffset;
        BlinkCameraOffset=(BlinkCameraOffset+C*Dt)*Decay;
        BlinkCameraVelocity=(BlinkCameraVelocity-Omega*C*Dt)*Decay;
        if (BlinkState.Phase!=2 && BlinkCameraOffset.SizeSquared()<.0001 && BlinkCameraVelocity.SizeSquared()<.0001)
            BlinkCameraOffset=BlinkCameraVelocity=FVector::ZeroVector;
    }
    const auto& F=MobilityConfiguration.FreeCamera; const auto& A=MobilityConfiguration.AimCamera;
    CameraModeAlpha=FMath::FInterpConstantTo(CameraModeAlpha,bShoulderAimHeld?1.f:0.f,DeltaSeconds,
        1.f/(bShoulderAimHeld?A.TransitionSeconds:F.TransitionSeconds));
    const float Blend=FMath::SmoothStep(0.f,1.f,CameraModeAlpha);
    CameraBoom->TargetArmLength=FMath::Lerp(DesiredCameraDistance,A.ArmLength,Blend);
    CameraBoom->SocketOffset=FMath::Lerp(F.SocketOffset,A.SocketOffset,Blend)
        +CameraBoom->GetTargetRotation().UnrotateVector(BlinkCameraOffset);
    Camera->SetFieldOfView(FMath::Lerp(F.FieldOfView,A.FieldOfView,Blend));
}
