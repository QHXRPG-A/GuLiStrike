#include "Gameplay/GroundMech/Abilities/Blink/GuLiGroundMechBlinkComponent.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/Camera/GuLiGroundMechCameraComponent.h"
#include "Gameplay/GroundMech/Abilities/Blink/Presentation/GuLiGroundMechBlinkVfxDefinition.h"
#include "Gameplay/GroundMech/Abilities/Blink/GuLiGA_Blink.h"
#include "Gameplay/GroundMech/Abilities/Blink/GuLiGE_BlinkCooldown.h"
#include "Gameplay/GroundMech/Abilities/Blink/Presentation/GuLiGroundMechBlinkCue.h"
#include "Gameplay/GroundMech/Abilities/Blink/GuLiGroundMechBlinkTags.h"
#include "Gameplay/GroundMech/Movement/GuLiGroundMechMovementComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiNetworkSettings.h"
#include "AbilitySystemComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "NiagaraSystem.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"


UGuLiGroundMechBlinkComponent::UGuLiGroundMechBlinkComponent()
{
    PrimaryComponentTick.bCanEverTick=false;
    SetIsReplicatedByDefault(false);
}

AGuLiGroundMechCharacter* UGuLiGroundMechBlinkComponent::Mech() const
{ return Cast<AGuLiGroundMechCharacter>(GetOwner()); }
UAbilitySystemComponent* UGuLiGroundMechBlinkComponent::ASC() const
{ const auto* Pawn=Mech(); return Pawn?Pawn->GetAbilitySystemComponent():nullptr; }
const FGuLiGroundMechConfiguration& UGuLiGroundMechBlinkComponent::Configuration() const
{ return CastChecked<AGuLiGroundMechCharacter>(GetOwner())->GetMobilityConfiguration(); }
float UGuLiGroundMechBlinkComponent::GetCooldownDuration() const
{ return Mech()?Configuration().Blink.BlinkCooldownSeconds:0.f; }
bool UGuLiGroundMechBlinkComponent::HasPresentationResources() const
{ return BlinkSystem || BlinkShapeMaterials.Num()>0; }

namespace
{
float BlinkEase(float T)
{
    T=FMath::Clamp(T,0.f,1.f);
    return T*T*T*(T*(T*6.f-15.f)+10.f);
}
}

bool UGuLiGroundMechBlinkComponent::QueueRequest(FGuLiHybridGroundInput& Input,uint16 Yaw)
{
    Input.BlinkYaw=Yaw;
    ++Input.BlinkRequest;
    if (!Input.BlinkRequest) ++Input.BlinkRequest;
    Input.bSprint=false;
    return true;
}

float UGuLiGroundMechBlinkComponent::GetPoseAlpha() const
{
    if (!Configuration().bReady || !BlinkState.Phase) return 0.f;
    const auto& C=Configuration().Blink;
    if (BlinkState.Phase==1) return BlinkEase(BlinkState.Elapsed/C.BlinkWindupSeconds);
    if (BlinkState.Phase==2) return 1.f;
    return 1.f-BlinkEase(BlinkState.Elapsed/C.BlinkRecoverySeconds);
}

void UGuLiGroundMechBlinkComponent::Initialize()
{
    if (!Mech() || !Configuration().bReady || !ASC()) return;
    bShuttingDown=false;
    if (!BlinkAbilityHandle.IsValid())
    {
        UClass* Class=Configuration().Blink.AbilityClass.LoadSynchronous();
        if (!Class || !Class->IsChildOf(UGuLiGA_Blink::StaticClass()))
        {
            UE_LOG(LogTemp,Error,TEXT("Ground Blink: invalid Skills/Blink AbilityClass; blink disabled"));
            return;
        }
        BlinkAbilityHandle=ASC()->GiveAbility(FGameplayAbilitySpec(Class,1,INDEX_NONE,Mech()));
    }
    if (UGuLiNetworkSettings::CanPresent(GetWorld()))
    {
        AGuLiGCN_GroundMechBlink::RegisterNativeCue();
        BlinkSystem=GuLiVfx::Load<UNiagaraSystem>(Mech(),Configuration().Blink.BlinkVfxId);
        BlinkShapeMaterials.Reset(); BlinkShapeMaterials.SetNumZeroed(Mech()->GetMesh()->GetNumMaterials());
        if (const auto* Visual=GuLiVfx::Load<UGuLiGroundMechBlinkVfxDefinition>(Mech(),Configuration().Blink.BlinkShapeVfxId))
            for (int32 Slot=0;Slot<BlinkShapeMaterials.Num();++Slot)
                for (const auto& Variant:Visual->BodyMaterials)
                    if (const auto* Source=Variant.Source.LoadSynchronous(); Source && Mech()->GetMesh()->GetMaterial(Slot)
                        && Mech()->GetMesh()->GetMaterial(Slot)->GetMaterial()==Source->GetMaterial())
                    { BlinkShapeMaterials[Slot]=Variant.Phase.LoadSynchronous(); break; }
        BlinkVfxScale=GuLiVfx::Scale(Mech(),Configuration().Blink.BlinkVfxId);
    }
    SyncGASState();
}

bool UGuLiGroundMechBlinkComponent::CanRunSimulation() const
{
    return Mech() && Configuration().bReady && BlinkAbilityHandle.IsValid()
        && UGuLiHybridActorComponent::HasSimulation(Mech()) && !Mech()->IsCarrierDormant() && !Mech()->IsActorBeingDestroyed()
        && Mech()->GetCharacterMovement()->MovementMode!=MOVE_None
        && !UGuLiExternalUnitControlComponent::AreActorActionsLocked(Mech());
}

bool UGuLiGroundMechBlinkComponent::CanActivate() const
{
    return CanRunSimulation() && !BlinkState.Phase && BlinkState.Cooldown<=0.f;
}

void UGuLiGroundMechBlinkComponent::CommitCooldown()
{
    if (bRestoringBlinkAbility) return;
    BlinkState.Cooldown=Configuration().Blink.BlinkCooldownSeconds;
    SyncGASState();
}

bool UGuLiGroundMechBlinkComponent::PrepareDestination()
{
    const auto& C=Configuration().Blink;
    const FVector Direction=FRotator(0,FRotator::DecompressAxisFromShort(Mech()->GetHybridSimulationInput().BlinkYaw),0).Vector();
    return CastChecked<UGuLiGroundMechMovementComponent>(Mech()->GetCharacterMovement())->FindBlinkDestination(
        Direction,C.BlinkDistance,C.BlinkLandingSearchStepCm,C.BlinkLandingProbeCm,PreparedBlinkDestination);
}

void UGuLiGroundMechBlinkComponent::StartFromAbility()
{
    auto& B=BlinkState;
    B.Direction=FRotator(0,FRotator::DecompressAxisFromShort(Mech()->GetHybridSimulationInput().BlinkYaw),0).Vector();
    B.Start=Mech()->GetActorLocation(); B.End=PreparedBlinkDestination; B.Yaw=B.Direction.Rotation().Yaw;
    // An accepted blink always becomes a forward dash. Its locked camera yaw
    // is also the body's yaw before the windup pose and first swept movement.
    Mech()->SetActorRotation(FRotator(0,B.Yaw,0));
    B.Phase=1; B.Elapsed=0.f; ++B.Serial;
    auto* Move=CastChecked<UGuLiGroundMechMovementComponent>(Mech()->GetCharacterMovement());
    BlinkSimulationDisplacementRevision=Move->GetDisplacementRevision();
    Mech()->ResetFacingForBlink(B.Yaw);
    NotifyStateChanged();
    SendEvent();
}

void UGuLiGroundMechBlinkComponent::SyncGASState()
{
    if (bShuttingDown || !ASC()) return;
    const bool Cooling=BlinkState.Cooldown>0.f;
    if (Cooling && !ASC()->GetActiveGameplayEffect(BlinkCooldownEffect))
    {
        const auto Spec=ASC()->MakeOutgoingSpec(UGuLiGE_BlinkCooldown::StaticClass(),1.f,ASC()->MakeEffectContext());
        if (Spec.IsValid()) BlinkCooldownEffect=ASC()->ApplyGameplayEffectSpecToSelf(*Spec.Data);
    }
    else if (!Cooling && BlinkCooldownEffect.IsValid())
    {
        ASC()->RemoveActiveGameplayEffect(BlinkCooldownEffect);
        BlinkCooldownEffect=FActiveGameplayEffectHandle();
    }
    // Simulators get the active tag from their GA; query pawns project the received phase once.
    const bool QueryActive=!UGuLiHybridActorComponent::HasSimulation(Mech()) && BlinkState.Phase!=0;
    if (QueryActive!=bBlinkQueryActiveTag)
    {
        if (QueryActive) ASC()->AddLooseGameplayTag(GuLiBlinkTags::Active);
        else ASC()->RemoveLooseGameplayTag(GuLiBlinkTags::Active);
        bBlinkQueryActiveTag=QueryActive;
    }
    NotifyStateChanged();
}

void UGuLiGroundMechBlinkComponent::NotifyStateChanged()
{
    const bool Cooling=BlinkState.Cooldown>0.f;
    if (NotifiedBlinkPhase==BlinkState.Phase && bNotifiedBlinkCooldown==Cooling) return;
    NotifiedBlinkPhase=BlinkState.Phase; bNotifiedBlinkCooldown=Cooling;
    Mech()->OnBlinkStateChanged.Broadcast(BlinkState.Phase,Cooling);
}

void UGuLiGroundMechBlinkComponent::SendEvent(bool bInterrupted)
{
    if (!ASC()) return;
    FGameplayEventData Payload;
    Payload.EventTag=bInterrupted ? GuLiBlinkTags::Interrupted.GetTag() :
        BlinkState.Phase ? GuLiBlinkTags::Phase.GetTag() : GuLiBlinkTags::Completed.GetTag();
    Payload.Instigator=Mech(); Payload.Target=Mech(); Payload.EventMagnitude=BlinkState.Phase;
    ASC()->HandleGameplayEvent(Payload.EventTag,&Payload);
}

void UGuLiGroundMechBlinkComponent::AbilityEnded(bool bCancelled)
{
    if (bRestoringBlinkAbility) return;
    if (bCancelled)
    {
        BlinkState.Phase=0; BlinkState.Elapsed=0.f; BlinkState.LastRequest=Mech()->GetHybridSimulationInput().BlinkRequest;
        StopPresentation(true); Mech()->GetCameraController()->ResetBlink();
    }
    SyncGASState();
}

void UGuLiGroundMechBlinkComponent::Interrupt()
{
    const bool WasActive=BlinkState.Phase!=0;
    BlinkState.Phase=0; BlinkState.Elapsed=0.f; BlinkState.LastRequest=Mech()->GetHybridSimulationInput().BlinkRequest;
    if (WasActive) SendEvent(true);
    if (ASC())
        if (const auto* Spec=ASC()->FindAbilitySpecFromHandle(BlinkAbilityHandle); Spec && Spec->IsActive())
            ASC()->CancelAbilityHandle(BlinkAbilityHandle);
    StopPresentation(true); Mech()->GetCameraController()->ResetBlink(); SyncGASState();
}

bool UGuLiGroundMechBlinkComponent::RestoreAbility()
{
    bBlinkAbilityRestorePending=false;
    if (!ASC() || !BlinkAbilityHandle.IsValid()) return false;
    bRestoringBlinkAbility=true;
    ASC()->CancelAbilityHandle(BlinkAbilityHandle);
    SyncGASState();
    const bool Success=!BlinkState.Phase || ASC()->TryActivateAbility(BlinkAbilityHandle,false);
    bRestoringBlinkAbility=false;
    return Success;
}

void UGuLiGroundMechBlinkComponent::AdvanceSimulation(float Dt)
{
    auto* Move=CastChecked<UGuLiGroundMechMovementComponent>(Mech()->GetCharacterMovement());
    auto& B=BlinkState; const auto& C=Configuration().Blink;
    B.Cooldown=FMath::Max(0.f,B.Cooldown-Dt);
    SyncGASState();
    if (B.Phase && BlinkSimulationDisplacementRevision!=Move->GetDisplacementRevision())
    {
        Interrupt(); return;
    }
    if (Mech()->IsCarrierDormant() || UGuLiExternalUnitControlComponent::AreActorActionsLocked(Mech()))
    {
        Interrupt(); return;
    }
    // A rejected request is consumed, never buffered until cooldown completion.
    if (Mech()->GetHybridSimulationInput().BlinkRequest!=B.LastRequest)
    {
        B.LastRequest=Mech()->GetHybridSimulationInput().BlinkRequest;
        if (Mech()->GetHybridSimulationInput().BlinkRequest && !B.Phase
            && ASC()->TryActivateAbility(BlinkAbilityHandle,false) && B.Phase) return;
    }
    float Remaining=FMath::Max(0.f,Dt);
    while (B.Phase && Remaining>UE_SMALL_NUMBER)
    {
        const float Duration=B.Phase==1?C.BlinkWindupSeconds:B.Phase==2?C.BlinkConcealSeconds:C.BlinkRecoverySeconds;
        const float Step=FMath::Min(Remaining,FMath::Max(0.f,Duration-B.Elapsed));
        B.Elapsed=FMath::Min(Duration,B.Elapsed+Step); Remaining-=Step;
        if (B.Elapsed<Duration-UE_SMALL_NUMBER) break;
        if (B.Phase==1)
        {
            // Recheck the endpoint at the actual teleport tick, including any
            // vertical jet/fall movement since input. Intervening space is ignored.
            FVector Landing;
            if (!Move->FindBlinkDestination(B.Direction,C.BlinkDistance,C.BlinkLandingSearchStepCm,C.BlinkLandingProbeCm,Landing))
            { Interrupt(); return; }
            B.Start=Mech()->GetActorLocation(); B.End=Move->TeleportBlink(Landing);
        }
        B.Phase=B.Phase==3?0:B.Phase+1; B.Elapsed=0.f;
        NotifyStateChanged();
        SendEvent();
    }
}

void UGuLiGroundMechBlinkComponent::RegisterCue(AGuLiGCN_GroundMechBlink* Cue)
{
    BlinkCues.RemoveAll([](const auto& Entry){ return !Entry.IsValid(); });
    BlinkCues.AddUnique(Cue);
}

void UGuLiGroundMechBlinkComponent::UnregisterCue(AGuLiGCN_GroundMechBlink* Cue)
{
    BlinkCues.RemoveAll([Cue](const auto& Entry){ return !Entry.IsValid() || Entry.Get()==Cue; });
}

void UGuLiGroundMechBlinkComponent::StopPresentation(bool bImmediate)
{
    if (bBlinkCueActive && ASC() && UGuLiNetworkSettings::CanPresent(GetWorld()))
    {
        bBlinkCueActive=false;
        FGameplayCueParameters Params;
        Params.Instigator=Mech(); Params.SourceObject=Mech(); Params.RawMagnitude=bImmediate?1.f:0.f;
        ASC()->InvokeGameplayCueEvent(GuLiBlinkTags::Cue,EGameplayCueEvent::Removed,Params);
    }
    if (bImmediate)
    {
        const auto Carriers=BlinkCues;
        for (const auto& Entry:Carriers) if (auto* Cue=Entry.Get()) Cue->ForceStop();
        BlinkCues.Reset();
    }
}

void UGuLiGroundMechBlinkComponent::ResetPresentation()
{
    StopPresentation(true);
    bBlinkPresentationPrimed=false;
    PresentedBlinkSerial=BlinkState.Serial; PresentedBlinkPhase=BlinkState.Phase; PresentedBlinkEnd=BlinkState.End;
    Mech()->GetCameraController()->ResetBlink();
}

void UGuLiGroundMechBlinkComponent::Present()
{
    if (!UGuLiNetworkSettings::CanPresent(GetWorld()) || !Configuration().bReady) return;
    const bool NewSerial=!bBlinkPresentationPrimed || PresentedBlinkSerial!=BlinkState.Serial;
    const bool bHistoricalActivation=!bBlinkPresentationPrimed;
    const bool bNewLocalActivation=bBlinkPresentationPrimed && Mech()->IsLocallyControlled() && PresentedBlinkSerial!=BlinkState.Serial;
    if (NewSerial)
    {
        StopPresentation(true);
        PresentedBlinkSerial=BlinkState.Serial;
        // A local activation may already have swept before its first render frame.
        // Cancel that first displacement too. Restores/late joins still start at
        // the observed endpoint and never rebuild an old path/camera offset.
        PresentedBlinkEnd=bNewLocalActivation?BlinkState.Start:Mech()->GetActorLocation();
        PresentedBlinkPhase=bNewLocalActivation?0:BlinkState.Phase;
        bBlinkPresentationPrimed=true;
    }
    if ((BlinkState.Phase==2 || BlinkState.Phase==3) && (bNewLocalActivation || PresentedBlinkPhase==1))
        Mech()->GetCameraController()->AddBlinkDisplacement(BlinkState.End-BlinkState.Start,true);
    const bool bVisualActive=BlinkState.Phase==1 || BlinkState.Phase==2;
    if (bVisualActive && !bBlinkCueActive && (BlinkSystem || BlinkShapeMaterials.Num()>0))
    {
        AGuLiGCN_GroundMechBlink::RegisterNativeCue();
        bBlinkCueActive=true;
        FGameplayCueParameters Params;
        Params.Instigator=Mech(); Params.SourceObject=Mech();
        Params.NormalizedMagnitude=BlinkState.Phase;
        Params.RawMagnitude=bHistoricalActivation?1.f:0.f;
        ASC()->InvokeGameplayCueEvent(GuLiBlinkTags::Cue,EGameplayCueEvent::OnActive,Params);
    }
    else if (bVisualActive && bBlinkCueActive && PresentedBlinkPhase!=BlinkState.Phase)
    {
        FGameplayCueParameters Params; Params.Instigator=Mech(); Params.SourceObject=Mech();
        Params.NormalizedMagnitude=BlinkState.Phase;
        ASC()->InvokeGameplayCueEvent(GuLiBlinkTags::Cue,EGameplayCueEvent::WhileActive,Params);
    }
    else if (!bVisualActive) StopPresentation(false);
    // End is the planned destination even during windup; only account for the
    // actual one-time teleport after the concealed phase has begun.
    PresentedBlinkEnd=Mech()->GetActorLocation(); PresentedBlinkPhase=BlinkState.Phase;
}


void UGuLiGroundMechBlinkComponent::ApplySnapshot(const FGuLiGroundMechBlinkState& State)
{ BlinkState=State; SyncGASState(); }
void UGuLiGroundMechBlinkComponent::PrepareRestore()
{
    ResetPresentation();
    bBlinkAbilityRestorePending=true;
    BlinkSimulationDisplacementRevision=CastChecked<UGuLiGroundMechMovementComponent>(Mech()->GetCharacterMovement())->GetDisplacementRevision();
}
bool UGuLiGroundMechBlinkComponent::CompletePendingRestore()
{ return !bBlinkAbilityRestorePending || RestoreAbility(); }
void UGuLiGroundMechBlinkComponent::ResetControl()
{
    Interrupt(); bBlinkAbilityRestorePending=false;
    BlinkState.Phase=0; BlinkState.Elapsed=0.f; BlinkState.LastRequest=0;
}
void UGuLiGroundMechBlinkComponent::Shutdown()
{
    if (bShuttingDown) return;
    bShuttingDown=true;
    StopPresentation(true);
    if (auto* AbilitySystem=ASC())
    {
        AbilitySystem->ClearAbility(BlinkAbilityHandle);
        AbilitySystem->RemoveActiveGameplayEffect(BlinkCooldownEffect);
        if (bBlinkQueryActiveTag) AbilitySystem->RemoveLooseGameplayTag(GuLiBlinkTags::Active);
    }
    bBlinkQueryActiveTag=false;
    BlinkAbilityHandle=FGameplayAbilitySpecHandle(); BlinkCooldownEffect=FActiveGameplayEffectHandle();
    BlinkSystem=nullptr; BlinkShapeMaterials.Reset();
}
void UGuLiGroundMechBlinkComponent::EndPlay(EEndPlayReason::Type Reason)
{ Shutdown(); Super::EndPlay(Reason); }
