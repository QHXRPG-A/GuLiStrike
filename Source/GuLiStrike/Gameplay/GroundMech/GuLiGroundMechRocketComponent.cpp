#include "Gameplay/GroundMech/GuLiGroundMechRocketComponent.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Gameplay/GroundMech/GuLiGroundMechAbilities.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/GuLiGroundMechMovementComponent.h"
#include "Gameplay/Building/GuLiBuildingPlacementComponent.h"
#include "Gameplay/Teleport/GuLiTeleportInputComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Battle/Framework/GuLiBattlePlayerState.h"
#include "Battle/Network/GuLiPlayerNetSyncComponent.h"
#include "Commander/Framework/GuLiCommanderPlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

namespace { constexpr int32 RocketInputId = 1; }
UGuLiGroundMechRocketComponent::UGuLiGroundMechRocketComponent()
{
    SetIsReplicatedByDefault(true);
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
    SkillTable = TSoftObjectPtr<UDataTable>(FSoftObjectPath(TEXT("/Game/GuLiStrike/Data/DT_GuLiStrikeMech_Skills.DT_GuLiStrikeMech_Skills")));
}
UAbilitySystemComponent* UGuLiGroundMechRocketComponent::ASC() const
{
    return GetOwner()->FindComponentByClass<UAbilitySystemComponent>();
}
UGuLiGroundMechMovementComponent* UGuLiGroundMechRocketComponent::Movement() const
{
    return GetOwner()->FindComponentByClass<UGuLiGroundMechMovementComponent>();
}
void UGuLiGroundMechRocketComponent::BeginPlay()
{
    Super::BeginPlay();
    auto* Table = SkillTable.LoadSynchronous();
    const auto* Row = Table && Table->GetRowStruct() == FGuLiStrikeMechSkillsRow::StaticStruct()
        ? Table->FindRow<FGuLiStrikeMechSkillsRow>(SkillRow, TEXT("RocketJump"), false) : nullptr;
    auto Positive = [](float V) { return FMath::IsFinite(V) && V > 0.f; };
    auto* AbilityClass = Row ? Row->AbilityClass.LoadSynchronous() : nullptr;
    if (!Row || Row->ExecutionType != TEXT("GAS") || !AbilityClass || !AbilityClass->IsChildOf(UGuLiGA_RocketJump::StaticClass())
        || !Positive(Row->MaxFuel) || !FMath::IsFinite(Row->InitialFuel) || Row->InitialFuel < 0.f || Row->InitialFuel > Row->MaxFuel
        || !Positive(Row->FuelDrainPerSecond) || !Positive(Row->FuelRecoveryPerSecond)
        || !FMath::IsFinite(Row->FuelRecoveryDelay) || Row->FuelRecoveryDelay < 0.f
        || !Positive(Row->ThrustAcceleration) || !Positive(Row->MaxRiseSpeed) || !Positive(Row->FuelBarFadeSeconds)
        || !Positive(Row->FuelBarHeight) || !Positive(Row->FuelBarRightOffset)
        || !Positive(Row->AirSpeedMultiplier) || !Positive(Row->FallGravityMultiplier) || !FMath::IsFinite(Row->JetMaxTiltDegrees)
        || Row->JetMaxTiltDegrees < 0.f || Row->JetMaxTiltDegrees > 15.f
        || !FMath::IsFinite(Row->JetPitchDegrees) || Row->JetSocketLeft.IsEmpty() || Row->JetSocketRight.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("RocketJump: invalid/missing Excel skill row %s; ability disabled."), *SkillRow.ToString());
        SetComponentTickEnabled(false);
        return;
    }
    Configuration = *Row;
    bConfigured = true;
    RefreshActorInfo();
    if (!bFuelBaselineReceived) Movement()->InitializeRocketFuel(Configuration.InitialFuel);
    if (GetOwner()->HasAuthority())
    {
        auto Spec = ASC()->MakeOutgoingSpec(UGuLiGE_MechFuelInitialize::StaticClass(), 1.f, ASC()->MakeEffectContext());
        Spec.Data->SetSetByCallerMagnitude(TEXT("MaxFuel"), Configuration.MaxFuel);
        Spec.Data->SetSetByCallerMagnitude(TEXT("Fuel"), Configuration.InitialFuel);
        ASC()->ApplyGameplayEffectSpecToSelf(*Spec.Data);
        AbilityHandle = ASC()->GiveAbility(FGameplayAbilitySpec(AbilityClass, 1, RocketInputId, this));
    }
    if (GetNetMode() != NM_DedicatedServer)
    {
        JetSystem = GuLiVfx::Load<UNiagaraSystem>(this, Configuration.JetVfxId);
        FuelMaterial = GuLiVfx::Load<UMaterialInterface>(this, Configuration.FuelBarVfxId);
        if (!JetSystem || !FuelMaterial) UE_LOG(LogTemp, Error, TEXT("RocketJump: missing presentation asset; gameplay remains available."));
        CreatePresentation();
    }
}
void UGuLiGroundMechRocketComponent::RefreshActorInfo()
{
    if (ASC()) ASC()->InitAbilityActorInfo(GetOwner(), GetOwner());
    if (bConfigured && GetNetMode() != NM_DedicatedServer) CreatePresentation();
}
bool UGuLiGroundMechRocketComponent::CanUseRocketControls() const
{
    const auto* Pawn = Cast<APawn>(GetOwner());
    const auto* State = Pawn ? Pawn->GetPlayerState<AGuLiBattlePlayerState>() : nullptr;
    const auto* PC = Pawn ? Cast<AGuLiCommanderPlayerController>(Pawn->GetController()) : nullptr;
    const auto* Building = PC ? PC->GetBuildingPlacementComponent() : nullptr;
    const auto* Teleport = PC ? PC->FindComponentByClass<UGuLiTeleportInputComponent>() : nullptr;
    return bConfigured && Pawn && State && PC && State->IsBattleReady() && State->GetBattleRole() == EGuLiCommanderRole::Ground
        && !PC->IsMoveInputIgnored() && !UGuLiExternalUnitControlComponent::AreActorActionsLocked(Pawn)
        && (!Pawn->IsLocallyControlled() || (PC->GetPlayerNetSyncComponent()->IsConnectionReady()
            && (!Building || !Building->IsBuildModeActive()) && (!Teleport || !Teleport->IsAiming())));
}
bool UGuLiGroundMechRocketComponent::CanActivateRocket() const
{
    const auto* Move = Movement();
    return CanUseRocketControls() && GetFuel() > UE_SMALL_NUMBER && Move &&
        (Move->IsMovingOnGround() || Move->IsFalling() || Move->GetMassSupportSoldierId().IsValid());
}
void UGuLiGroundMechRocketComponent::SetRocketJumpInput(bool bHeld)
{
    const auto* Pawn = Cast<APawn>(GetOwner());
    if (!Pawn || !Pawn->IsLocallyControlled()) { if (!bHeld) Interrupt(); return; }
    if (bHeld && !CanUseRocketControls()) return;
    if (bInputHeld == bHeld) return;
    bInputHeld = bHeld;
    if (bHeld)
    {
        bShown = true;
        FuelBarOpacity = 1.f;
        ASC()->AbilityLocalInputPressed(RocketInputId);
    }
    else ASC()->AbilityLocalInputReleased(RocketInputId);
    RefreshJetState();
}
void UGuLiGroundMechRocketComponent::SetAbilityActive(bool bActive)
{
    bAbilityActive = bActive;
    if (bActive) { bShown = true; FuelBarOpacity = 1.f; }
    else if (GetOwner()->HasAuthority()) bReplicatedThrusting = false;
    RefreshJetState();
}
void UGuLiGroundMechRocketComponent::Interrupt()
{
    bInputHeld = false;
    if (ASC())
    {
        ASC()->AbilityLocalInputReleased(RocketInputId);
        if (AbilityHandle.IsValid()) ASC()->CancelAbilityHandle(AbilityHandle);
        else if (bAbilityActive) ASC()->CancelAbility(GetMutableDefault<UGuLiGA_RocketJump>());
    }
    bAbilityActive = false;
    bReplicatedThrusting = false;
    if (Movement()) Movement()->ClearRocketInput();
    RefreshJetState();
}
void UGuLiGroundMechRocketComponent::ApplyFuelDelta(float Delta)
{
    if (!GetOwner()->HasAuthority() || FMath::IsNearlyZero(Delta, 1.e-6f)) return;
    auto Spec = ASC()->MakeOutgoingSpec(UGuLiGE_MechFuelDelta::StaticClass(), 1.f, ASC()->MakeEffectContext());
    Spec.Data->SetSetByCallerMagnitude(TEXT("FuelDelta"), Delta);
    ASC()->ApplyGameplayEffectSpecToSelf(*Spec.Data);
}
void UGuLiGroundMechRocketComponent::FinishMovement(float Fuel, bool bThrusting, bool bReplay)
{
    if (!bConfigured || bReplay) return;
    if (GetOwner()->HasAuthority())
    {
        ApplyFuelDelta(Fuel - ASC()->GetNumericAttribute(UGuLiGroundMechAttributeSet::GetFuelAttribute()));
        if (bReplicatedThrusting != bThrusting) { bReplicatedThrusting = bThrusting; GetOwner()->ForceNetUpdate(); }
    }
    if (GetOwner()->HasAuthority() && Fuel <= UE_SMALL_NUMBER && bAbilityActive)
    {
        // Keep held input latched: depletion never counts as releasing Space.
        // The autonomous proxy stops applying thrust at predicted zero but must not
        // cancel the server's ability ahead of its final saved movement interval.
        ASC()->CancelAbilityHandle(AbilityHandle);
    }
    RefreshJetState();
}
void UGuLiGroundMechRocketComponent::ReceiveInitialFuel(float Value)
{
    if (!bFuelBaselineReceived && Movement()) { Movement()->InitializeRocketFuel(Value); bFuelBaselineReceived = true; }
}
float UGuLiGroundMechRocketComponent::GetFuel() const
{
    if (!bConfigured) return 0.f;
    if (GetOwner()->HasAuthority()) return ASC()->GetNumericAttribute(UGuLiGroundMechAttributeSet::GetFuelAttribute());
    return Movement()->GetPredictedRocketFuel();
}
float UGuLiGroundMechRocketComponent::GetFuelRatio() const
{
    return bConfigured ? FMath::Clamp(GetFuel() / Configuration.MaxFuel, 0.f, 1.f) : 0.f;
}
bool UGuLiGroundMechRocketComponent::IsThrusting() const
{
    const auto* Pawn = Cast<APawn>(GetOwner());
    return Pawn && Pawn->IsLocallyControlled() ? bAbilityActive && Movement()->IsRocketThrusting() : bReplicatedThrusting;
}
bool UGuLiGroundMechRocketComponent::IsJetActive() const
{
    const auto* Pawn = Cast<APawn>(GetOwner());
    if (!Pawn) return false;
    if (Pawn->IsLocallyControlled())
        return bInputHeld && bAbilityActive && GetFuel() > UE_SMALL_NUMBER && CanUseRocketControls();
    return bReplicatedJetActive;
}
void UGuLiGroundMechRocketComponent::ApplyJetPresentation()
{
    if (GetNetMode() == NM_DedicatedServer) return;
    const bool bActive = IsJetActive();
    if (bJetsActive == bActive) return;
    // Activation is called from the locally predicted ability in the input frame.
    // A missing movement sample must not delay ignition until the character lifts off.
    if (bActive && (!LeftJet || !RightJet)) CreatePresentation();
    if (bActive && (!LeftJet || !RightJet)) return;
    bJetsActive = bActive;
    for (auto* Jet : {LeftJet.Get(), RightJet.Get()})
        if (Jet) { if (bActive) Jet->Activate(true); else Jet->Deactivate(); }
}
void UGuLiGroundMechRocketComponent::RefreshJetState()
{
    if (GetOwner()->HasAuthority())
    {
        const bool bActive = bAbilityActive && GetFuel() > UE_SMALL_NUMBER && CanUseRocketControls();
        if (bReplicatedJetActive != bActive)
        {
            bReplicatedJetActive = bActive;
            GetOwner()->ForceNetUpdate();
        }
    }
    ApplyJetPresentation();
}
void UGuLiGroundMechRocketComponent::OnRep_JetActive() { ApplyJetPresentation(); }
void UGuLiGroundMechRocketComponent::CreatePresentation()
{
    if (GetNetMode() == NM_DedicatedServer || !bConfigured) return;
    auto* Mech = CastChecked<AGuLiGroundMechCharacter>(GetOwner());
    auto* Armor = Mech->GetArmor();
    if (!bJetsCreated && JetSystem)
    {
        bJetsCreated = true;
        auto CreateJet = [&](FName Name, FName Socket)
        {
            if (!Armor->DoesSocketExist(Socket)) { UE_LOG(LogTemp, Error, TEXT("RocketJump: missing Armor socket %s"), *Socket.ToString()); return static_cast<UNiagaraComponent*>(nullptr); }
            auto* Jet = NewObject<UNiagaraComponent>(Mech, Name);
            Mech->AddInstanceComponent(Jet);
            Jet->SetupAttachment(Armor, Socket);
            Jet->SetUsingAbsoluteScale(true);
            Jet->SetRelativeScale3D(GuLiVfx::Scale(this, Configuration.JetVfxId));
            Jet->SetRelativeRotation(FRotator(Configuration.JetPitchDegrees, 0, 0));
            Jet->SetAutoActivate(false);
            Jet->SetAutoDestroy(false);
            Jet->SetAsset(JetSystem);
            Jet->SetCanEverAffectNavigation(false);
            Jet->SetCastShadow(false);
            Jet->RegisterComponent();
            return Jet;
        };
        LeftJet = CreateJet(TEXT("RocketJetLeft"), FName(Configuration.JetSocketLeft));
        RightJet = CreateJet(TEXT("RocketJetRight"), FName(Configuration.JetSocketRight));
    }
    if (Mech->IsLocallyControlled() && !FuelBar && FuelMaterial)
    {
        FuelBar = NewObject<UStaticMeshComponent>(Mech, TEXT("RocketFuelBar"));
        Mech->AddInstanceComponent(FuelBar);
        FuelBar->SetupAttachment(Mech->GetRootComponent());
        FuelBar->SetAbsolute(true, true, true);
        FuelBar->SetStaticMesh(GuLiVfx::Load<UStaticMesh>(this, GuLiVfxIds::FuelBarPlane));
        FuelBar->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        FuelBar->SetGenerateOverlapEvents(false);
        FuelBar->SetCanEverAffectNavigation(false);
        FuelBar->SetCastShadow(false);
        FuelBar->SetReceivesDecals(false);
        FuelBar->SetOnlyOwnerSee(true);
        FuelBar->SetVisibility(false);
        FuelBar->RegisterComponent();
        FuelMID = UMaterialInstanceDynamic::Create(FuelMaterial, this);
        FuelMID->SetScalarParameterValue(TEXT("Opacity"), 0.f);
        FuelBar->SetMaterial(0, FuelMID);
    }
    if (!Mech->IsLocallyControlled() && FuelBar)
    {
        FuelBar->DestroyComponent(); FuelBar = nullptr; FuelMID = nullptr;
        bShown = false; FuelBarOpacity = 0.f;
    }
}
void UGuLiGroundMechRocketComponent::UpdatePresentation(float Delta)
{
    const bool bThrusting = IsThrusting();
    const auto* Mech = CastChecked<AGuLiGroundMechCharacter>(GetOwner());
    const FVector LateralVelocity = FVector(Mech->GetVelocity().X, Mech->GetVelocity().Y, 0.f);
    const float AirSpeed = Mech->GetWalkSpeed() * Configuration.AirSpeedMultiplier;
    const float Tilt = Configuration.JetMaxTiltDegrees * FMath::Clamp(LateralVelocity.Size() / AirSpeed, 0.f, 1.f);
    const FVector ExhaustDirection = (FVector::DownVector - LateralVelocity.GetSafeNormal()
        * FMath::Tan(FMath::DegreesToRadians(Tilt))).GetSafeNormal();
    const FQuat Steering = FQuat::FindBetweenNormals(FVector::DownVector, ExhaustDirection);
    for (auto* Jet : {LeftJet.Get(), RightJet.Get()})
    {
        if (!Jet) continue;
        const FQuat BaseRotation = Mech->GetArmor()->GetSocketQuaternion(Jet->GetAttachSocketName())
            * FRotator(Configuration.JetPitchDegrees, 0.f, 0.f).Quaternion();
        Jet->SetWorldRotation(Steering * BaseRotation);
    }
    RefreshJetState();
    auto* Pawn = CastChecked<APawn>(GetOwner());
    if (!Pawn->IsLocallyControlled() || !FuelBar) return;
    const float Ratio = GetFuelRatio();
    if (bThrusting || bAbilityActive) { bShown = true; FuelBarOpacity = 1.f; }
    else if (Ratio < 1.f) { bShown = true; FuelBarOpacity = 1.f; }
    else FuelBarOpacity = FMath::Max(0.f, FuelBarOpacity - Delta / Configuration.FuelBarFadeSeconds);
    const auto* PC = Cast<APlayerController>(Pawn->GetController());
    if (PC && PC->PlayerCameraManager)
    {
        const FRotator CameraRotation = PC->PlayerCameraManager->GetCameraRotation();
        const FRotationMatrix CameraAxes(CameraRotation);
        FuelBar->SetWorldLocation(Pawn->GetActorLocation() + CameraAxes.GetUnitAxis(EAxis::Y) * Configuration.FuelBarRightOffset);
        // Plane local X points screen-right, local Y screen-down, local Z toward camera.
        FuelBar->SetWorldRotation(FRotationMatrix::MakeFromXY(CameraAxes.GetUnitAxis(EAxis::Y), -CameraAxes.GetUnitAxis(EAxis::Z)).Rotator());
        FuelBar->SetWorldScale3D(GuLiVfx::Scale(this, GuLiVfxIds::FuelBarPlane, GuLiVfx::Scale(this, Configuration.FuelBarVfxId, FVector(Configuration.FuelBarHeight / 100.f))));
    }
    const FLinearColor Color = Ratio >= .5f ? FLinearColor(.18f, 1.f, .08f) : Ratio >= .2f ? FLinearColor(1.f,.7f,.03f) : FLinearColor(1.f,.035f,.015f);
    FuelMID->SetScalarParameterValue(TEXT("FuelRatio"), Ratio);
    FuelMID->SetScalarParameterValue(TEXT("Opacity"), FuelBarOpacity);
    FuelMID->SetVectorParameterValue(TEXT("Color"), Color);
    FuelBar->SetVisibility(bShown && FuelBarOpacity > 0.f);
    if (Ratio != LastRatio || FuelBarOpacity != LastOpacity || bThrusting != bLastThrusting)
    {
        LastRatio = Ratio; LastOpacity = FuelBarOpacity; bLastThrusting = bThrusting;
        OnPresentationChanged.Broadcast(Ratio, bThrusting, FuelBarOpacity);
    }
}
void UGuLiGroundMechRocketComponent::TickComponent(float Delta, ELevelTick Type, FActorComponentTickFunction* Function)
{
    Super::TickComponent(Delta, Type, Function);
    const auto* Pawn = CastChecked<APawn>(GetOwner());
    if ((Pawn->HasAuthority() || Pawn->IsLocallyControlled()) && (bAbilityActive || bInputHeld) && !CanUseRocketControls()) Interrupt();
    if (GetNetMode() != NM_DedicatedServer) { CreatePresentation(); UpdatePresentation(Delta); }
}
void UGuLiGroundMechRocketComponent::DestroyPresentation()
{
    for (auto* Jet : {LeftJet.Get(), RightJet.Get()}) if (Jet) { Jet->DeactivateImmediate(); Jet->DestroyComponent(); }
    LeftJet = nullptr; RightJet = nullptr; bJetsCreated = false; bJetsActive = false;
    if (FuelBar) FuelBar->DestroyComponent();
    FuelBar = nullptr; FuelMID = nullptr;
}
void UGuLiGroundMechRocketComponent::EndPlay(EEndPlayReason::Type Reason)
{
    Interrupt();
    if (GetOwner()->HasAuthority() && AbilityHandle.IsValid()) ASC()->ClearAbility(AbilityHandle);
    DestroyPresentation();
    OnPresentationChanged.Clear();
    Super::EndPlay(Reason);
}
void UGuLiGroundMechRocketComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME_CONDITION(UGuLiGroundMechRocketComponent, bReplicatedThrusting, COND_SkipOwner);
    DOREPLIFETIME_CONDITION(UGuLiGroundMechRocketComponent, bReplicatedJetActive, COND_SkipOwner);
}
