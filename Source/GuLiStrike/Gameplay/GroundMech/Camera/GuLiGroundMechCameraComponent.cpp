#include "Gameplay/GroundMech/Camera/GuLiGroundMechCameraComponent.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/Movement/GuLiGroundMechMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"

UGuLiGroundMechCameraComponent::UGuLiGroundMechCameraComponent()
{
    PrimaryComponentTick.bCanEverTick=false;
    SetIsReplicatedByDefault(false);
}
AGuLiGroundMechCharacter* UGuLiGroundMechCameraComponent::Mech() const
{ return Cast<AGuLiGroundMechCharacter>(GetOwner()); }
const FGuLiGroundMechConfiguration& UGuLiGroundMechCameraComponent::Configuration() const
{ return CastChecked<AGuLiGroundMechCharacter>(GetOwner())->GetMobilityConfiguration(); }

void UGuLiGroundMechCameraComponent::ResetBlink()
{
    BlinkCameraOffset=BlinkCameraVelocity=FVector::ZeroVector;
    BlinkCameraHoldRemaining=0.f;
    BlinkCameraFovWeight=0.f; bBlinkCameraWasActive=false;
    BlinkCameraFovReturnStart=BlinkCameraFovReturnAge=0.f;
    BlinkCameraDisplacementRevision=CastChecked<UGuLiGroundMechMovementComponent>(Mech()->GetCharacterMovement())->GetDisplacementRevision();
    Update(0.f);
}

void UGuLiGroundMechCameraComponent::AddBlinkDisplacement(const FVector& Delta,bool bFirst)
{
    if (!Mech()->IsLocallyControlled() || !Configuration().bReady) return;
    if (bFirst)
    {
        const auto& C=Mech()->IsShoulderAimHeld()?Configuration().AimCamera:Configuration().FreeCamera;
        BlinkCameraHoldRemaining=C.BlinkHoldSeconds;
        BlinkCameraSettleSeconds=C.BlinkCatchupSeconds;
        BlinkCameraDisplacementRevision=CastChecked<UGuLiGroundMechMovementComponent>(Mech()->GetCharacterMovement())->GetDisplacementRevision();
    }
    // Cancel only the actual swept ability displacement. Mouse orbit and ordinary
    // movement retain their existing response. Multiple fixed steps accumulate.
    BlinkCameraOffset-=Delta;
    Update(0.f);
}

void UGuLiGroundMechCameraComponent::Update(float DeltaSeconds)
{
    if (!Configuration().bReady || !CameraBoom || !Camera) return;
    const uint32 Revision=CastChecked<UGuLiGroundMechMovementComponent>(Mech()->GetCharacterMovement())->GetDisplacementRevision();
    if (Revision!=BlinkCameraDisplacementRevision || !Mech()->IsLocallyControlled())
    {
        BlinkCameraOffset=BlinkCameraVelocity=FVector::ZeroVector;
        BlinkCameraHoldRemaining=0.f; BlinkCameraDisplacementRevision=Revision;
        BlinkCameraFovWeight=0.f; bBlinkCameraWasActive=false;
        BlinkCameraFovReturnStart=BlinkCameraFovReturnAge=0.f;
    }
    const auto& Pulse=Mech()->IsShoulderAimHeld()?Configuration().AimCamera:Configuration().FreeCamera;
    const bool bActive=Mech()->IsLocallyControlled() && (Mech()->GetBlinkState().Phase==1 || Mech()->GetBlinkState().Phase==2);
    float Dt=FMath::Max(0.f,DeltaSeconds);
    if (bActive)
    {
        // Keep cancelling the complete swept displacement throughout Begin/Loop.
        // The chase spring must not consume the accumulated offset mid-dash.
        Dt=0.f; BlinkCameraVelocity=FVector::ZeroVector;
        // Start the cosmetic ramp on the first observed activation. A received
        // snapshot can skip the 20ms windup; that must not jump FOV to its peak.
        if (!bBlinkCameraWasActive) BlinkCameraFovWeight=0.f;
        BlinkCameraFovWeight=FMath::Min(1.f,BlinkCameraFovWeight+FMath::Max(0.f,DeltaSeconds)/Pulse.BlinkFovRiseSeconds);
    }
    else
    {
        if (bBlinkCameraWasActive)
        {
            BlinkCameraHoldRemaining=Pulse.BlinkHoldSeconds;
            BlinkCameraFovReturnStart=BlinkCameraFovWeight; BlinkCameraFovReturnAge=0.f;
        }
        BlinkCameraFovReturnAge=FMath::Min(Pulse.BlinkFovReturnSeconds,BlinkCameraFovReturnAge+FMath::Max(0.f,DeltaSeconds));
        BlinkCameraFovWeight=BlinkCameraFovReturnStart*(1.f-BlinkCameraFovReturnAge/Pulse.BlinkFovReturnSeconds);
    }
    bBlinkCameraWasActive=bActive;
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
        if (Mech()->GetBlinkState().Phase!=2 && BlinkCameraOffset.SizeSquared()<.0001 && BlinkCameraVelocity.SizeSquared()<.0001)
            BlinkCameraOffset=BlinkCameraVelocity=FVector::ZeroVector;
    }
    const auto& F=Configuration().FreeCamera; const auto& A=Configuration().AimCamera;
    CameraModeAlpha=FMath::FInterpConstantTo(CameraModeAlpha,Mech()->IsShoulderAimHeld()?1.f:0.f,DeltaSeconds,
        1.f/(Mech()->IsShoulderAimHeld()?A.TransitionSeconds:F.TransitionSeconds));
    const float Blend=FMath::SmoothStep(0.f,1.f,CameraModeAlpha);
    const float Framing=FMath::SmoothStep(0.f,1.f,BlinkCameraFovWeight);
    const float BaseArm=FMath::Lerp(DesiredCameraDistance,A.ArmLength,Blend);
    CameraBoom->TargetArmLength=FMath::Lerp(BaseArm,FMath::Min(BaseArm,FMath::Lerp(F.BlinkArmLength,A.BlinkArmLength,Blend)),Framing);
    // Move the whole boom anchor so its collision query remains at the held
    // camera origin. A 12m socket offset traced from the teleported pawn was
    // colliding with terrain and dragging the supposedly held camera forward.
    CameraBoom->SetWorldLocation(Mech()->GetActorLocation()+BlinkCameraOffset);
    CameraBoom->SocketOffset=FMath::Lerp(F.SocketOffset,A.SocketOffset,Blend);
    CameraBoom->SocketOffset.Y=FMath::Lerp(CameraBoom->SocketOffset.Y,FMath::Lerp(F.BlinkSideOffsetCm,A.BlinkSideOffsetCm,Blend),Framing);
    // The anchor stays at the cast origin; only the flash composition dollies
    // inward. Chase begins after arrival, with the ordinary shoulder view restored.
    Camera->SetRelativeRotation(FRotator(FMath::Lerp(F.BlinkPitchOffsetDegrees,A.BlinkPitchOffsetDegrees,Blend)*Framing,0,0));
    Camera->SetFieldOfView(FMath::Lerp(F.FieldOfView,A.FieldOfView,Blend)
        +FMath::Lerp(F.BlinkFovBoostDegrees,A.BlinkFovBoostDegrees,Blend)*Framing);
}


void UGuLiGroundMechCameraComponent::Initialize(USpringArmComponent* Boom,UCameraComponent* View)
{
    CameraBoom=Boom; Camera=View;
    if (Configuration().bReady)
    {
        const auto& C=Configuration().FreeCamera;
        DesiredCameraDistance=C.ArmLength; CameraBoom->SocketOffset=C.SocketOffset;
        CameraBoom->SetWorldRotation(FRotator(C.PitchDegrees,C.YawDegrees,0));
        Camera->SetFieldOfView(C.FieldOfView);
        Camera->SetRelativeLocationAndRotation(FVector::ZeroVector,FRotator::ZeroRotator);
        CameraBoom->bDoCollisionTest=true;
    }
    CameraBoom->TargetArmLength=DesiredCameraDistance;
    CameraBoom->AddWorldRotation(FRotator(0,Mech()->GetActorRotation().Yaw,0));
    ResetBlink();
}
void UGuLiGroundMechCameraComponent::AddLookInput(FVector2D Axis)
{
    if (!CameraBoom || !Configuration().bReady) return;
    const auto& C=Mech()->IsShoulderAimHeld()?Configuration().AimCamera:Configuration().FreeCamera;
    const FVector2D Delta=Axis*C.Sensitivity;
    FRotator Rotation=CameraBoom->GetComponentRotation();
    Rotation.Yaw=FRotator::NormalizeAxis(Rotation.Yaw+Delta.X);
    Rotation.Pitch=FMath::Clamp(Rotation.Pitch+Delta.Y,C.MinPitchDegrees,C.MaxPitchDegrees);
    Rotation.Roll=0.f; CameraBoom->SetWorldRotation(Rotation);
}
void UGuLiGroundMechCameraComponent::AddZoomInput(float Axis)
{
    if (!Configuration().bReady || Mech()->IsShoulderAimHeld()) return;
    const auto& C=Configuration().FreeCamera;
    DesiredCameraDistance=FMath::Clamp(DesiredCameraDistance-Axis*C.ZoomStep,C.MinDistance,C.MaxDistance);
}
float UGuLiGroundMechCameraComponent::GetViewYaw() const
{ return CameraBoom?CameraBoom->GetTargetRotation().Yaw:0.f; }
