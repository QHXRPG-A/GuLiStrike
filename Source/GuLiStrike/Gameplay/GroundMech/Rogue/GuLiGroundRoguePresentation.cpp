#include "Gameplay/GroundMech/Rogue/GuLiGroundRoguePresentation.h"
#include "Gameplay/GroundMech/Rogue/GuLiGroundRogueRuntime.h"
#include "Gameplay/GroundMech/GuLiGroundMechCharacter.h"
#include "Gameplay/Cards/GuLiRogueCardSettings.h"
#include "Gameplay/Cards/GuLiRogueCardPresentation.h"
#include "GameFramework/PlayerController.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/GuLiNetworkSettings.h"
#include "Commander/Presentation/GuLiCommanderOverviewSubsystem.h"
#include "Animation/AnimSequence.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/AssetManager.h"
#include "Engine/DataTable.h"
#include "Engine/StreamableManager.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"

DEFINE_LOG_CATEGORY_STATIC(LogGroundRogueCues, Log, All);

namespace
{
    FVector UpgradeScale(float RadiusScale, float HeightMultiplier)
    {
        // Older serialized card rows use zero for newly added multipliers.
        return FVector(RadiusScale, RadiusScale, RadiusScale * (HeightMultiplier > 0.f ? HeightMultiplier : 1.f));
    }
}

UGuLiGroundRoguePresentation::UGuLiGroundRoguePresentation()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

void UGuLiGroundRoguePresentation::BeginPlay()
{
    Super::BeginPlay();
    const auto* Mech = Cast<AGuLiGroundMechCharacter>(GetOwner());
    bCanPresent = Mech && Mech->ModelId == 4001 && UGuLiNetworkSettings::CanPresent(GetWorld());
    if (!bCanPresent) return;
    const auto* Table = GetDefault<UGuLiRogueCardSettings>()->Cards.LoadSynchronous();
    if (!Table) return;
    TArray<FSoftObjectPath> Paths;
    Table->ForeachRow<FGuLiStrikeRogueCardsCardsRow>(TEXT("Ranger card cues"), [&](const FName&, const FGuLiStrikeRogueCardsCardsRow& Row)
    {
        if (Row.Type != 3) return;
        Rows.Add(Row.Id, Row);
        for (const auto* Asset : {&Row.UpgradeVfx, &Row.MaxRankUpgradeVfx, &Row.CastPose, &Row.CastFlashVfx, &Row.CastLightningVfx})
            if (!Asset->IsNull()) Paths.AddUnique(Asset->ToSoftObjectPath());
    });
    if (const auto* Row = Rows.Find(GuLiGroundRogue::BarrageCardId); Row && !Row->CastCamera.IsNull())
    {
        if (!CameraPath.Load(*Row))
            UE_LOG(LogGroundRogueCues, Warning, TEXT("Invalid barrage camera table %s"), *Row->CastCamera.ToString());
    }
    if (!Paths.IsEmpty()) Assets = UAssetManager::GetStreamableManager().RequestAsyncLoad(Paths);
}

bool UGuLiGroundRoguePresentation::CanShowBody() const
{
    const auto* Mech = CastChecked<AGuLiGroundMechCharacter>(GetOwner());
    return !Mech->IsCarrierDormant() && !Mech->IsHidden() && Mech->GetMesh()->IsVisible() && !Mech->GetMesh()->bHiddenInGame;
}

UNiagaraComponent* UGuLiGroundRoguePresentation::Spawn(const TSoftObjectPtr<UObject>& Asset, float Size, bool bAtFeet,
    float HeightMultiplier, float PlaybackRate)
{
    auto* System = Cast<UNiagaraSystem>(Asset.Get());
    if (!System)
    {
        UE_LOG(LogGroundRogueCues, Warning, TEXT("Cannot play %s on %s"), *Asset.ToString(), *GetNameSafe(GetOwner()));
        return nullptr;
    }
    const auto* Mech = CastChecked<AGuLiGroundMechCharacter>(GetOwner());
    auto* Capsule = Mech->GetCapsuleComponent();
    const double Height = Capsule->GetUnscaledCapsuleHalfHeight();
    const FVector Offset(0., 0., bAtFeet ? -Height : .28 * Height);
    const FVector RelativeScale = bAtFeet ? UpgradeScale(Size, HeightMultiplier) : FVector::OneVector;
    auto* FX = UNiagaraFunctionLibrary::SpawnSystemAttached(System, Capsule, NAME_None, Offset,
        FRotator::ZeroRotator, RelativeScale, EAttachLocation::KeepRelativeOffset,
        false, ENCPoolMethod::ManualRelease, false, false);
    if (FX)
    {
        FX->SetCustomTimeDilation(PlaybackRate > 0.f ? PlaybackRate : 1.f);
        if (!bAtFeet) FX->SetVariableFloat(TEXT("User.Scale"), Size);
        FX->SetForceLocalPlayerEffect(Mech->IsLocallyControlled());
        FX->SetCastShadow(false);
        if (auto* Overview = GetWorld()->GetSubsystem<UGuLiCommanderOverviewSubsystem>()) Overview->RegisterVisual(FX);
        FX->Activate(true);
        UE_LOG(LogGroundRogueCues, Verbose, TEXT("Started %s on %s at %s"), *System->GetName(), *GetNameSafe(GetOwner()), *FX->GetComponentLocation().ToCompactString());
    }
    return FX;
}

void UGuLiGroundRoguePresentation::Release(TWeakObjectPtr<UNiagaraComponent>& Component)
{
    if (auto* FX = Component.Get())
    {
        UGuLiCommanderOverviewSubsystem::ForgetVisual(FX);
        FX->DeactivateImmediate();
        // Niagara's component pool resets the transform but retains custom time dilation.
        FX->SetCustomTimeDilation(1.f);
        FX->ReleaseToPool();
    }
    Component.Reset();
}

float UGuLiGroundRoguePresentation::CastAge(const FGuLiStrikeRogueCardsCardsRow& Row) const
{
    return UGuLiHybridActorComponent::BattleSeconds(GetOwner()) - double(CastFirstShotTick) / 60.
        + GuLiGroundRogue::BarrageWindupSeconds(Row);
}

void UGuLiGroundRoguePresentation::SetCast(uint64 FirstShotTick)
{
    if (!bCanPresent || CastFirstShotTick == FirstShotTick) return;
    Release(Flash);
    Release(Lightning);
    CastFirstShotTick = FirstShotTick;
    LastChargeArcScale = 0.f;
    bFlashPassed = false;
    if (FirstShotTick)
    {
        ClearShotRecoil();
        const auto* Row = Rows.Find(GuLiGroundRogue::BarrageCardId);
        // A late observer enters the current phase without replaying the charge flash.
        bFlashPassed = Row && CastAge(*Row) >= GuLiGroundRogue::BarrageWindupSeconds(*Row) - KINDA_SMALL_NUMBER;
        if (CanShowBody()) UpdateCast();
    }
    SetComponentTickEnabled(FirstShotTick || !Upgrades.IsEmpty());
}

void UGuLiGroundRoguePresentation::RecordShot(uint64 Tick, bool bLeft)
{
    uint64& Last = bLeft ? LeftShotTick : RightShotTick;
    Last = FMath::Max(Last, Tick);
}

void UGuLiGroundRoguePresentation::ReadShotRecoil(FVector& Left, FVector& Right) const
{
    Left = Right = FVector::ZeroVector;
    if (!bCanPresent || !LatestShotTick()) return;
    const auto* Row = Rows.Find(GuLiGroundRogue::BarrageCardId);
    if (!Row || Row->CastShotRecoilSeconds <= 0.f || Row->CastShotRecoilCm <= 0.f) return;
    const double Now = UGuLiHybridActorComponent::BattleSeconds(GetOwner());
    auto Offset = [&](uint64 ShotTick) -> FVector
    {
        if (!ShotTick) return FVector::ZeroVector;
        const float Phase = (Now - double(ShotTick) / 60.) / Row->CastShotRecoilSeconds;
        if (Phase <= 0.f || Phase >= 1.f) return FVector::ZeroVector;
        // Two ticks to peak with the authored 0.15 s return. Mesh forward is +Y.
        constexpr float Peak = 2.f/9.f;
        const float Strength = Phase < Peak ? FMath::SmoothStep(0.f, Peak, Phase)
                                            : 1.f - FMath::SmoothStep(Peak, 1.f, Phase);
        return FVector(0., -.96, .28) * (Row->CastShotRecoilCm * Strength);
    };
    Left = Offset(LeftShotTick);
    Right = Offset(RightShotTick);
}

void UGuLiGroundRoguePresentation::ReadPose(UAnimSequence*& Sequence, float& Time, float& Weight, float Delta) const
{
    if (!CastFirstShotTick && Weight <= 0.f) return;
    const auto* Row = Rows.Find(GuLiGroundRogue::BarrageCardId);
    if (!Row || !bCanPresent) { Weight = 0; return; }
    Sequence = Cast<UAnimSequence>(Row->CastPose.Get());
    if (CastFirstShotTick && Sequence)
    {
        const float Age = FMath::Max(0.f, CastAge(*Row));
        const float DeployedAt = GuLiGroundRogue::BarrageDeployedAtSeconds(*Row);
        const float HoverLength = Sequence->GetPlayLength() - DeployedAt;
        Time = Age > DeployedAt && HoverLength > UE_KINDA_SMALL_NUMBER
            ? DeployedAt + FMath::Fmod(Age-DeployedAt, HoverLength) : FMath::Min(Age,Sequence->GetPlayLength());
        Weight = FMath::Clamp(Age / FMath::Max(Row->CastFoldSeconds * .4f, UE_SMALL_NUMBER), 0.f, 1.f);
    }
    else Weight = FMath::Max(0.f, Weight - Delta / FMath::Max(Row->CastRecoverySeconds, UE_SMALL_NUMBER));
}

bool UGuLiGroundRoguePresentation::ReadCastCamera(FGuLiGroundBarrageCameraShot& Shot) const
{
    if (!bCanPresent || !CastFirstShotTick) return false;
    const auto* Row = Rows.Find(GuLiGroundRogue::BarrageCardId);
    return Row && CameraPath.Sample(CastAge(*Row),Shot);
}

void UGuLiGroundRoguePresentation::UpdateCast()
{
    if ((bFlashPassed && Lightning.IsValid()) || (Assets && !Assets->HasLoadCompleted())) return;
    const auto* Row = Rows.Find(GuLiGroundRogue::BarrageCardId);
    if (!Row) return;
    const float Age = CastAge(*Row);
    const float FlashAt = GuLiGroundRogue::BarrageDeployedAtSeconds(*Row);
    const float LightningSize = Row->CastBodyScale * (Row->CastLightningScaleMultiplier > 0.f ? Row->CastLightningScaleMultiplier : 1.f);
    if (Age + KINDA_SMALL_NUMBER < FlashAt)
    {
        const float Ratio = FMath::GridSnap(CameraPath.ChargeArcScale(Age), 1.f/32.f);
        if (Ratio <= 0.f) return;
        if (!Lightning.IsValid()) Lightning = Spawn(Row->CastLightningVfx,LightningSize*Ratio,false);
        else if (Ratio != LastChargeArcScale) Lightning->SetVariableFloat(TEXT("User.Scale"),LightningSize*Ratio);
        LastChargeArcScale = Ratio;
        return;
    }
    const bool bFiring = Age + KINDA_SMALL_NUMBER >= FlashAt + Row->CastFlashSeconds;
    if (!bFlashPassed)
    {
        Release(Lightning);
        Flash = Spawn(Row->CastFlashVfx, Row->CastBodyScale, false);
        bFlashPassed = true;
        if (Flash.IsValid()) return;
    }
    if (bFiring)
    {
        Release(Flash);
        if (!Lightning.IsValid()) Lightning = Spawn(Row->CastLightningVfx, LightningSize, false);
    }
}

void UGuLiGroundRoguePresentation::PlayUpgrade(const FString& CardId, int32 Rank)
{
    const auto* Row = Rows.Find(CardId);
    if (!bCanPresent || !Row) return;
    auto& Cue = Upgrades.AddDefaulted_GetRef();
    Cue.CardId = CardId;
    Cue.bUseMaxRankVfx = Rank == Row->MaxAcquisitions && !Row->MaxRankUpgradeVfx.IsNull();
    const auto* Mech=CastChecked<AGuLiGroundMechCharacter>(GetOwner());
    if (const auto* PC=Cast<APlayerController>(Mech->GetController()); PC && PC->IsLocalController())
        Cue.LocalSelection=PC->FindComponentByClass<UGuLiRogueCardPresentation>();
    UE_LOG(LogGroundRogueCues, Verbose, TEXT("Queued %s rank %d for %s (local selection %d)"),
        *CardId, Rank, *Mech->GetName(), Cue.LocalSelection.IsValid());
    SetComponentTickEnabled(true);
}

bool UGuLiGroundRoguePresentation::UpdateUpgrade(FUpgrade& Cue, double Now)
{
    const auto& Row = Rows.FindChecked(Cue.CardId);
    if (Cue.Component.IsExplicitlyNull())
    {
        if (Assets && !Assets->HasLoadCompleted()) return true;

        UNiagaraComponent* FX = Cue.bUseMaxRankVfx
            ? Spawn(Row.MaxRankUpgradeVfx, Row.MaxRankUpgradeScale * Row.MaxRankStartRatio, true,
                Row.UpgradeVfxHeightMultiplier)
            : Spawn(Row.UpgradeVfx, Row.UpgradeVfxScale, true, Row.UpgradeVfxHeightMultiplier,
                Row.UpgradeVfxPlaybackRate);
        Cue.Component = FX;
        if (FX && FX->IsActive() && !FX->IsComplete()) Cue.Started = Now;
        return FX != nullptr;
    }

    UNiagaraComponent* FX = Cue.Component.Get();
    if (!FX) return false;
    if (!FX->IsActive() || FX->IsComplete())
    {
        // Niagara may defer first activation while its system is being prepared.
        return Cue.Started < 0. && FX->IsComponentTickEnabled() && !FX->GetSystemInstanceController().IsValid();
    }
    if (Cue.Started < 0.) Cue.Started = Now;
    if (!Cue.bUseMaxRankVfx) return true;

    const double Age = Now - Cue.Started;
    if (Age >= Row.MaxRankDurationSeconds) return false;
    const float Alpha = FMath::Clamp(static_cast<float>(Age) / Row.MaxRankGrowthSeconds, 0.f, 1.f);
    const float RadiusScale = Row.MaxRankUpgradeScale * FMath::Lerp(Row.MaxRankStartRatio, 1.f, Alpha);
    const FVector Scale = UpgradeScale(RadiusScale, Row.UpgradeVfxHeightMultiplier);
    if (!FX->GetRelativeScale3D().Equals(Scale)) FX->SetRelativeScale3D(Scale);
    return true;
}

void UGuLiGroundRoguePresentation::TickComponent(float Delta, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
    Super::TickComponent(Delta, TickType, TickFunction);
    const bool bVisible=CanShowBody();
    if (!bVisible)
    {
        Release(Flash);
        Release(Lightning);
    }
    else if (CastFirstShotTick) UpdateCast();
    const double Now = GetWorld()->GetTimeSeconds();
    for (int32 Index = Upgrades.Num()-1; Index >= 0; --Index)
    {
        auto& Cue = Upgrades[Index];
        if (Cue.LocalSelection.IsValid() && Cue.LocalSelection->IsOpen()) continue;
        Cue.LocalSelection.Reset();
        if (bVisible && UpdateUpgrade(Cue, Now)) continue;
        Release(Cue.Component);
        Upgrades.RemoveAtSwap(Index);
    }
    SetComponentTickEnabled(CastFirstShotTick || !Upgrades.IsEmpty());
}

void UGuLiGroundRoguePresentation::ResetEffects()
{
    SetCast(0);
    ClearShotRecoil();
    for (auto& Cue : Upgrades) Release(Cue.Component);
    Upgrades.Reset();
    SetComponentTickEnabled(false);
}

void UGuLiGroundRoguePresentation::EndPlay(EEndPlayReason::Type Reason)
{
    ResetEffects();
    Assets.Reset();
    Super::EndPlay(Reason);
}
