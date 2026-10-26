#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/GuLiGroundMechMovementComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Presentation/GuLiLocalTeamColors.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiNetworkSettings.h"
#include "AI/BlinkVFX.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "GameFramework/SpringArmComponent.h"

namespace
{
float BlinkEase(float T)
{
    T=FMath::Clamp(T,0.f,1.f);
    return T*T*T*(T*(T*6.f-15.f)+10.f);
}
}

bool AGuLiGroundMechCharacter::RequestBlink()
{
    if (!CanUseControls() || !IsShoulderLookActive() || !MobilityConfiguration.bReady) return false;
    HybridInput.BlinkYaw=FRotator::CompressAxisToShort(CameraBoom->GetTargetRotation().Yaw);
    ++HybridInput.BlinkRequest;
    if (!HybridInput.BlinkRequest) ++HybridInput.BlinkRequest;
    HybridInput.bSprint=false; bSprintHeld=false;
    return true;
}

float AGuLiGroundMechCharacter::GetBlinkPoseAlpha() const
{
    if (!MobilityConfiguration.bReady || !BlinkState.Phase) return 0.f;
    const auto& C=MobilityConfiguration.Mobility;
    if (BlinkState.Phase==1) return BlinkEase(BlinkState.Elapsed/C.BlinkWindupSeconds);
    if (BlinkState.Phase==2) return 1.f;
    return 1.f-BlinkEase(BlinkState.Elapsed/C.BlinkRecoverySeconds);
}

void AGuLiGroundMechCharacter::AdvanceBlink(float Dt)
{
    auto* Move=CastChecked<UGuLiGroundMechMovementComponent>(GetCharacterMovement());
    auto& B=BlinkState; const auto& C=MobilityConfiguration.Mobility;
    B.Cooldown=FMath::Max(0.f,B.Cooldown-Dt);
    if (B.Phase && BlinkSimulationDisplacementRevision!=Move->GetDisplacementRevision())
    {
        // An external teleport invalidates the captured path. Do not drag the
        // new view back to an old location or continue the remaining dash there.
        B.Phase=0; B.Elapsed=0; B.LastRequest=SimulationInput.BlinkRequest;
        ResetBlinkCamera(); return;
    }
    if (bCarrierDormant || UGuLiExternalUnitControlComponent::AreActorActionsLocked(this))
    { B.Phase=0; B.Elapsed=0; B.LastRequest=SimulationInput.BlinkRequest; return; }
    // Consume each request even when rejected: holding Shift cannot queue another blink.
    if (SimulationInput.BlinkRequest!=B.LastRequest)
    {
        B.LastRequest=SimulationInput.BlinkRequest;
        if (SimulationInput.BlinkRequest && !B.Phase && B.Cooldown<=0 && !bCarrierDormant
            && !UGuLiExternalUnitControlComponent::AreActorActionsLocked(this))
        {
            // The request owns its heading. Movement keys, camera pitch and
            // later camera motion must not redirect an accepted blink.
            B.Direction=FRotator(0,FRotator::DecompressAxisFromShort(SimulationInput.BlinkYaw),0).Vector();
            B.Start=B.End=GetActorLocation(); B.Yaw=GetActorRotation().Yaw;
            B.Phase=1; B.Elapsed=0; B.Cooldown=C.BlinkCooldownSeconds; ++B.Serial;
            BlinkSimulationDisplacementRevision=Move->GetDisplacementRevision();
            TurnState.Direction=0; ++TurnState.Serial;
            Move->Velocity.X=Move->Velocity.Y=0;
            return;
        }
    }
    // Carry excess fixed-step time across the boundaries: every stage receives
    // its configured duration, without a reset/replay or a one-frame teleport.
    float Remaining=FMath::Max(0.f,Dt);
    while (B.Phase && Remaining>UE_SMALL_NUMBER)
    {
        const float Duration=B.Phase==1?C.BlinkWindupSeconds:B.Phase==2?C.BlinkLoopSeconds:C.BlinkRecoverySeconds;
        const float Step=FMath::Min(Remaining,FMath::Max(0.f,Duration-B.Elapsed));
        const float OldTime=B.Elapsed;
        B.Elapsed=FMath::Min(Duration,B.Elapsed+Step); Remaining-=Step;
        if (B.Phase==2 && Step>0.f)
        {
            const float Distance=C.BlinkDistance*(BlinkEase(B.Elapsed/Duration)-BlinkEase(OldTime/Duration));
            B.End=Move->SweepBlink(B.Direction*Distance);
        }
        if (B.Elapsed<Duration-UE_SMALL_NUMBER) break;
        if (B.Phase==1) B.Start=B.End=GetActorLocation();
        B.Phase=B.Phase==3?0:B.Phase+1; B.Elapsed=0.f;
    }
}

void AGuLiGroundMechCharacter::PresentBlink()
{
    if ((BlinkState.Phase!=2 && BlinkState.Phase!=3)
        || !UGuLiNetworkSettings::CanPresent(GetWorld()) || !MobilityConfiguration.bReady) return;
    const auto& C=MobilityConfiguration.Mobility;
    const bool First=PresentedBlinkSerial!=BlinkState.Serial;
    if (First)
    {
        PresentedBlinkSerial=BlinkState.Serial; PresentedBlinkAfterimages=0;
        PresentedBlinkEnd=BlinkState.Start;
        // Late restored endpoints do not replay the already completed trail.
        if (BlinkState.Phase==3 && BlinkState.Elapsed>C.BlinkAfterimageSeconds)
        { PresentedBlinkAfterimages=C.BlinkAfterimageCount; PresentedBlinkEnd=BlinkState.End; return; }
    }
    const FVector LastEnd=PresentedBlinkEnd;
    AddBlinkCameraDisplacement(BlinkState.End-LastEnd,First);
    PresentedBlinkEnd=BlinkState.End;
    const float Travel=FMath::Max(0.f,float(FVector::DotProduct(BlinkState.End-BlinkState.Start,BlinkState.Direction)));
    const float PreviousTravel=FMath::Max(0.f,float(FVector::DotProduct(LastEnd-BlinkState.Start,BlinkState.Direction)));
    if (Travel<1.f || Travel<=PreviousTravel) return;
    UMaterialInterface* Material=GuLiVfx::Load<UMaterialInterface>(this,GuLiVfxIds::GroundMechAfterimage);
    if (!Material) return;
    const bool Enemy=GuLiLocalTeamColors::IsEnemy(GuLiLocalTeamColors::GetActorTeam(this),
        GuLiLocalTeamColors::GetViewTeam(GetWorld()->GetFirstPlayerController()));
    const FLinearColor Tint(FColor::FromHex(Enemy?TEXT("A34053"):TEXT("6AA4BE")));
    FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    Params.ObjectFlags|=RF_Transient;
    while (PresentedBlinkAfterimages<C.BlinkAfterimageCount)
    {
        const float T=float(PresentedBlinkAfterimages)/C.BlinkAfterimageCount;
        const float Distance=T*C.BlinkDistance;
        if (Travel<Distance) break;
        ++PresentedBlinkAfterimages;
        // Each afterimage stays at the traversed world point; it never follows
        // the pawn and cannot be spawned beyond a blocking sweep hit.
        const float Segment=FMath::Clamp((Distance-PreviousTravel)/(Travel-PreviousTravel),0.f,1.f);
        const FTransform At(FRotator(0,BlinkState.Yaw,0),FMath::Lerp(LastEnd,BlinkState.End,Segment),GetActorScale3D());
        if (auto* Ghost=GetWorld()->SpawnActor<ABlinkVFX>(ABlinkVFX::StaticClass(),At,Params))
            Ghost->InitializeFromCharacter(this,true,false,C.BlinkAfterimageOpacity*FMath::Lerp(.45f,1.f,T),
                C.BlinkAfterimageSeconds,Material,Tint);
    }
}
