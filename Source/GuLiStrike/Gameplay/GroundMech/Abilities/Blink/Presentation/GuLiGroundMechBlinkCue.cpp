#include "Gameplay/GroundMech/Abilities/Blink/Presentation/GuLiGroundMechBlinkCue.h"
#include "Gameplay/GroundMech/Abilities/Blink/GuLiGroundMechBlinkTags.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/GroundMech/Abilities/Blink/GuLiGroundMechBlinkComponent.h"
#include "Network/GuLiNetworkSettings.h"
#include "AbilitySystemGlobals.h"
#include "GameplayCueManager.h"
#include "GameplayCueSet.h"
#include "Components/SkeletalMeshComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"

AGuLiGCN_GroundMechBlink::AGuLiGCN_GroundMechBlink()
{
    SetReplicates(false);
    PrimaryActorTick.bCanEverTick=true;
    PrimaryActorTick.TickGroup=TG_PostUpdateWork;
    PrimaryActorTick.bStartWithTickEnabled=false;
    bAutoAttachToOwner=false;
    bAutoDestroyOnRemove=false;
    bAllowMultipleOnActiveEvents=false;
    bAllowMultipleWhileActiveEvents=true;
    GameplayCueTag=GuLiBlinkTags::Cue;
    Trail=CreateDefaultSubobject<UNiagaraComponent>(TEXT("BlinkTrail"));
    SetRootComponent(Trail);
    Trail->SetAutoActivate(false);
    Trail->SetAutoDestroy(false);
    Trail->SetForceSolo(true);
    Trail->SetTickBehavior(ENiagaraTickBehavior::UsePrereqs);
    Trail->AddTickPrerequisiteActor(this);
    EnergyBody=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("EnergyBody"));
    EnergyWeapon=CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("EnergyWeapon"));
    for (auto* C:{EnergyBody.Get(),EnergyWeapon.Get()})
    {
        C->SetupAttachment(Trail); C->SetAbsolute(true,true,true);
        C->SetCollisionEnabled(ECollisionEnabled::NoCollision); C->SetCastShadow(false);
        C->SetHiddenInGame(true); C->SetRenderCustomDepth(false);
        C->bUseBoundsFromLeaderPoseComponent=false; C->bUseAttachParentBound=false;
    }
}

void AGuLiGCN_GroundMechBlink::RegisterNativeCue()
{
    // Native class references are cooked with this module; no broad /Game cue scan or BP shell is needed.
    auto* Manager=UAbilitySystemGlobals::Get().GetGameplayCueManager();
    if (Manager && !Manager->GetRuntimeCueSet()) Manager->InitializeRuntimeObjectLibrary();
    if (auto* Set=Manager ? Manager->GetRuntimeCueSet() : nullptr)
    {
        if (!Set->GameplayCueDataMap.Contains(GuLiBlinkTags::Cue))
        {
            TArray<FGameplayCueReferencePair> Cues;
            Cues.Emplace(GuLiBlinkTags::Cue,FSoftObjectPath(StaticClass()));
            Set->AddCues(Cues);
        }
    }
}

bool AGuLiGCN_GroundMechBlink::OnActive_Implementation(AActor* Target, const FGameplayCueParameters& Parameters)
{
    auto* Pawn=Cast<AGuLiGroundMechCharacter>(Target);
    if (!Pawn || !Pawn->GetBlinkComponent()->HasPresentationResources() || !UGuLiNetworkSettings::CanPresent(GetWorld()))
    { Finish(); return false; }
    bFinishing=false; bTrailStarted=false; bRemoving=false; bShapeFinished=false;
    bHistorical=Parameters.RawMagnitude>0.f;
    Mech=Pawn; Pawn->GetBlinkComponent()->RegisterCue(this); AddTickPrerequisiteActor(Pawn);
    BodyMesh=Pawn->GetMesh(); WeaponMesh=Pawn->GetSeparateWeaponMesh();
    bBodyWasHidden=BodyMesh.IsValid() && BodyMesh->bHiddenInGame;
    bWeaponWasHidden=WeaponMesh.IsValid() && WeaponMesh->bHiddenInGame;
    CreateEnergyShape(*Pawn);
    bOwnsVisibility=!EnergyMaterials.IsEmpty();
    if (bOwnsVisibility && BodyMesh.IsValid()) BodyMesh->SetHiddenInGame(true);
    if (bOwnsVisibility && WeaponMesh.IsValid()) WeaponMesh->SetHiddenInGame(true);
    SetActorTickEnabled(true); UpdateEnergyShape();
    if (Parameters.NormalizedMagnitude>=2.f && !bHistorical) StartTrail();
    return true;
}

bool AGuLiGCN_GroundMechBlink::WhileActive_Implementation(AActor*,const FGameplayCueParameters& Parameters)
{
    if (Parameters.NormalizedMagnitude>=2.f && !bHistorical) StartTrail();
    return true;
}

void AGuLiGCN_GroundMechBlink::CreateEnergyShape(AGuLiGroundMechCharacter& Pawn)
{
    EnergyMaterials.Reset();
    const auto& C=Pawn.GetMobilityConfiguration().Blink;
    UMaterialInterface* Fallback=nullptr;
    for (const auto& Parent:Pawn.GetBlinkComponent()->GetShapeMaterials()) if (Parent) { Fallback=Parent; break; }
    auto Copy=[&](USkeletalMeshComponent* Proxy,USkeletalMeshComponent* Source,bool Body)
    {
        if (!Source || !Source->GetSkeletalMeshAsset() || !Fallback) return;
        // Integrated A4B2 already contains its guns. Do not reveal the hidden
        // legacy weapon component as an extra part of the energy proxy.
        if (!Body && (!Source->IsVisible() || Source->bHiddenInGame)) return;
        Proxy->SetSkeletalMeshAsset(Source->GetSkeletalMeshAsset());
        Proxy->SetLeaderPoseComponent(Source,true,false);
        const auto& Data=Source->GetCustomPrimitiveData().Data;
        for (int32 I=0;I<Data.Num();++I) Proxy->SetCustomPrimitiveDataFloat(I,Data[I]);
        for (int32 Slot=0;Slot<Source->GetNumMaterials();++Slot)
        {
            auto* Parent=Body && Pawn.GetBlinkComponent()->GetShapeMaterials().IsValidIndex(Slot)?Pawn.GetBlinkComponent()->GetShapeMaterials()[Slot].Get():Fallback;
            if (!Parent) Parent=Fallback;
            auto* MID=UMaterialInstanceDynamic::Create(Parent,this);
            if (auto* Original=Source->GetMaterial(Slot)) MID->CopyMaterialUniformParameters(Original);
            MID->SetScalarParameterValue(TEXT("BlinkStrength"),1.f);
            MID->SetScalarParameterValue(TEXT("BlinkProxyFade"),1.f);
            MID->SetScalarParameterValue(TEXT("BlinkStartTime"),GetWorld()->GetTimeSeconds());
            MID->SetScalarParameterValue(TEXT("BlinkFlickerHz"),C.BlinkShapeFlickerHz);
            MID->SetScalarParameterValue(TEXT("BlinkFragmentOffsetCm"),C.BlinkShapeFragmentOffsetCm);
            MID->SetScalarParameterValue(TEXT("BlinkBodyEmission"),C.BlinkEnergyEmission);
            const FVector D=Pawn.GetBlinkState().Direction;
            MID->SetVectorParameterValue(TEXT("BlinkDirection"),FLinearColor(D.X,D.Y,D.Z,0));
            EnergyMaterials.Add(MID); Proxy->SetMaterial(Slot,MID);
        }
        Proxy->SetHiddenInGame(false);
    };
    Copy(EnergyBody,Pawn.GetMesh(),true); Copy(EnergyWeapon,Pawn.GetSeparateWeaponMesh(),false);
}

void AGuLiGCN_GroundMechBlink::UpdateEnergyShape()
{
    auto* Pawn=Mech.Get(); if (!Pawn || bShapeFinished) return;
    const auto& B=Pawn->GetBlinkState(); const auto& C=Pawn->GetMobilityConfiguration().Blink;
    float Progress=B.Phase==1?0.f:B.Phase==2?FMath::Clamp(B.Elapsed/C.BlinkConcealSeconds,0.f,1.f):1.f;
    Progress=Progress*Progress*(3.f-2.f*Progress);
    const FVector Location=bHistorical || bRemoving?Pawn->GetActorLocation():FMath::Lerp(B.Start,B.End,Progress);
    auto Place=[&](USkeletalMeshComponent* Proxy,USkeletalMeshComponent* Source)
    {
        if (!Source) return;
        FTransform T=Source->GetComponentTransform(); T.AddToTranslation(Location-Pawn->GetActorLocation()); Proxy->SetWorldTransform(T);
    };
    Place(EnergyBody,Pawn->GetMesh()); Place(EnergyWeapon,Pawn->GetSeparateWeaponMesh());
    if (!bRemoving)
    {
        Trail->SetWorldLocation(Location);
        const FVector Actual=Location-B.Start;
        const float Fraction=FMath::Clamp(float(Actual.Size2D()/FMath::Max(1.,FVector::Dist2D(B.Start,B.End))),0.f,1.f);
        Trail->SetVariableVec3(TEXT("User.BlinkOffset"),-B.Direction*C.BlinkTrailLengthCm*Fraction);
    }
    const float T=B.Phase==3?FMath::Clamp(B.Elapsed/C.BlinkRecoverySeconds,0.f,1.f):B.Phase==0?1.f:0.f;
    const float Fade=1.f-T*T*T*(T*(T*6.f-15.f)+10.f);
    for (const auto& MID:EnergyMaterials) if (MID) MID->SetScalarParameterValue(TEXT("BlinkProxyFade"),Fade);
    if (T>=1.f)
    {
        bShapeFinished=true; EnergyBody->SetHiddenInGame(true); EnergyWeapon->SetHiddenInGame(true);
        SetActorTickEnabled(false);
        if (bRemoving && (!bTrailStarted || Trail->IsComplete())) Finish();
    }
}

void AGuLiGCN_GroundMechBlink::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds); UpdateEnergyShape();
}

void AGuLiGCN_GroundMechBlink::StartTrail()
{
    auto* Pawn=Mech.Get();
    if (bTrailStarted || !Pawn || !Pawn->GetBlinkComponent()->GetSystem()) return;
    bTrailStarted=true; const auto& C=Pawn->GetMobilityConfiguration().Blink;
    Trail->SetAsset(Pawn->GetBlinkComponent()->GetSystem()); Trail->SetWorldScale3D(Pawn->GetBlinkComponent()->GetVfxScale());
    Trail->SetVariableFloat(TEXT("User.Lifetime"),C.BlinkRibbonLifetimeSeconds);
    Trail->SetVariableFloat(TEXT("User.Emission"),C.BlinkEnergyEmission);
    Trail->SetVariableFloat(TEXT("User.Width"),C.BlinkRibbonWidthCm);
    Trail->SetVariableFloat(TEXT("User.ConcealDuration"),C.BlinkConcealSeconds);
    Trail->SetVariableFloat(TEXT("User.ShardDuration"),C.BlinkShardDuration);
    Trail->SetVariableFloat(TEXT("User.ShardSpeed"),C.BlinkShardSpeed);
    Trail->SetVariableVec3(TEXT("User.Forward"),Pawn->GetBlinkState().Direction);
    UpdateEnergyShape();
    Trail->OnSystemFinished.AddUniqueDynamic(this,&ThisClass::SystemFinished); Trail->Activate(true);
}

void AGuLiGCN_GroundMechBlink::RestoreModel()
{
    if (!bOwnsVisibility) return;
    bOwnsVisibility=false;
    if (BodyMesh.IsValid()) BodyMesh->SetHiddenInGame(bBodyWasHidden);
    if (WeaponMesh.IsValid()) WeaponMesh->SetHiddenInGame(bWeaponWasHidden);
    BodyMesh.Reset(); WeaponMesh.Reset();
}

bool AGuLiGCN_GroundMechBlink::OnRemove_Implementation(AActor*,const FGameplayCueParameters& Parameters)
{
    if (Parameters.RawMagnitude>0.f) ForceStop();
    else
    {
        // Snap the effect's head to the actual arrival before freezing its tail.
        UpdateEnergyShape(); bRemoving=true; RestoreModel(); UpdateEnergyShape();
        if (bShapeFinished && (!bTrailStarted || Trail->IsComplete())) Finish();
    }
    return true;
}

void AGuLiGCN_GroundMechBlink::SystemFinished(UNiagaraComponent*)
{
    if (bRemoving && bShapeFinished) Finish();
}

void AGuLiGCN_GroundMechBlink::Finish()
{
    if (bFinishing) return;
    bFinishing=true; RestoreModel(); SetActorTickEnabled(false);
    EnergyBody->SetHiddenInGame(true); EnergyWeapon->SetHiddenInGame(true);
    if (auto* Pawn=Mech.Get()) { RemoveTickPrerequisiteActor(Pawn); Pawn->GetBlinkComponent()->UnregisterCue(this); }
    Mech.Reset(); K2_EndGameplayCue();
}

void AGuLiGCN_GroundMechBlink::ForceStop()
{
    Trail->OnSystemFinished.RemoveDynamic(this,&ThisClass::SystemFinished); Trail->DeactivateImmediate();
    Finish();
}

void AGuLiGCN_GroundMechBlink::OnOwnerDestroyed(AActor*) { ForceStop(); }

bool AGuLiGCN_GroundMechBlink::Recycle()
{
    RestoreModel(); SetActorTickEnabled(false);
    if (auto* Pawn=Mech.Get()) { RemoveTickPrerequisiteActor(Pawn); Pawn->GetBlinkComponent()->UnregisterCue(this); }
    Mech.Reset();
    Trail->OnSystemFinished.RemoveDynamic(this,&ThisClass::SystemFinished); Trail->DeactivateImmediate(); Trail->SetAsset(nullptr);
    for (auto* C:{EnergyBody.Get(),EnergyWeapon.Get()})
    { C->SetHiddenInGame(true); C->SetLeaderPoseComponent(nullptr); C->EmptyOverrideMaterials(); C->SetSkeletalMeshAsset(nullptr); }
    EnergyMaterials.Reset(); return Super::Recycle();
}

void AGuLiGCN_GroundMechBlink::EndPlay(EEndPlayReason::Type Reason)
{
    RestoreModel(); SetActorTickEnabled(false);
    if (auto* Pawn=Mech.Get()) { RemoveTickPrerequisiteActor(Pawn); Pawn->GetBlinkComponent()->UnregisterCue(this); }
    Mech.Reset();
    Trail->OnSystemFinished.RemoveDynamic(this,&ThisClass::SystemFinished); Trail->DeactivateImmediate();
    for (auto* C:{EnergyBody.Get(),EnergyWeapon.Get()}) { C->SetLeaderPoseComponent(nullptr); C->SetHiddenInGame(true); }
    EnergyMaterials.Reset(); Super::EndPlay(Reason);
}
