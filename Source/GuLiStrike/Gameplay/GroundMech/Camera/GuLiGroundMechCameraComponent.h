#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GuLiGroundMechCameraComponent.generated.h"

class AGuLiGroundMechCharacter;
class USpringArmComponent;
class UCameraComponent;
struct FGuLiGroundMechConfiguration;

/** Controls the existing Character camera subobjects; never advances gameplay or owns an independent Tick. */
UCLASS(ClassGroup=(GuLiStrike))
class GULISTRIKE_API UGuLiGroundMechCameraComponent final : public UActorComponent
{
    GENERATED_BODY()
public:
    UGuLiGroundMechCameraComponent();
    void Initialize(USpringArmComponent* Boom, UCameraComponent* View);
    void AddLookInput(FVector2D Axis);
    void AddZoomInput(float Axis);
    float GetViewYaw() const;
    void Update(float DeltaSeconds);
    void AddBlinkDisplacement(const FVector& Delta, bool bFirst);
    void ResetBlink();
private:
    AGuLiGroundMechCharacter* Mech() const;
    const FGuLiGroundMechConfiguration& Configuration() const;
    UPROPERTY(Transient) TObjectPtr<USpringArmComponent> CameraBoom;
    UPROPERTY(Transient) TObjectPtr<UCameraComponent> Camera;
    float DesiredCameraDistance=1084.4236f;
    float CameraModeAlpha=0.f;
    FVector BlinkCameraOffset=FVector::ZeroVector, BlinkCameraVelocity=FVector::ZeroVector;
    float BlinkCameraHoldRemaining=0.f, BlinkCameraSettleSeconds=.45f;
    float BlinkCameraFovWeight=0.f;
    float BlinkCameraFovReturnStart=0.f, BlinkCameraFovReturnAge=0.f;
    bool bBlinkCameraWasActive=false;
    uint32 BlinkCameraDisplacementRevision=0;
};
