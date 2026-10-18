#include "Gameplay/Performance/GuLiHotspotOptions.h"
#include "Network/GuLiNetworkSettings.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
// A feature becomes a normal default only after the paired capture proves its benefit.
// 2026-10-17: both paired scenarios passed (three pairs each, build-08).
constexpr uint32 ValidatedMask=31;
#if !UE_BUILD_SHIPPING
TAutoConsoleVariable<int32> HotspotMask(TEXT("gs.Perf.HotspotMask"),ValidatedMask,
    TEXT("Next session only: 1 HUD, 2 SceneUI, 4 auxiliary positions, 8 components, 16 teleport, 32 gameplay worker, 64 reduced work, 128 incremental delivery, 256 gameplay pool. Current validated control is 31."));
TAutoConsoleVariable<int32> GameplayWorkers(TEXT("gs.Perf.GameplayWorkers"),2,
    TEXT("Next session only: process-shared gameplay pool concurrency, 1..4."));
#endif
}
uint32 UGuLiHotspotOptions::ResolveLaunchMask()
{
    uint32 Value=ValidatedMask;
#if !UE_BUILD_SHIPPING
    int32 Requested=HotspotMask.GetValueOnGameThread();
    FParse::Value(FCommandLine::Get(),TEXT("GuLiHotspotMask="),Requested);
    Value=uint32(FMath::Clamp(Requested,0,511));
#endif
    return Value;
}
uint32 UGuLiHotspotOptions::ResolveGameplayWorkers()
{
    int32 Requested=2;
#if !UE_BUILD_SHIPPING
    Requested=GameplayWorkers.GetValueOnGameThread();
    FParse::Value(FCommandLine::Get(),TEXT("GuLiGameplayWorkers="),Requested);
#endif
    return uint32(FMath::Clamp(Requested,1,4));
}
bool UGuLiHotspotOptions::ShouldCreateSubsystem(UObject* Outer) const
{
    const auto* World=Cast<UWorld>(Outer);
    return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld();
}
void UGuLiHotspotOptions::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    const auto Config=UGuLiNetworkSettings::Resolve(GetWorld());Mask=Config.HotspotMask;Workers=Config.GameplayWorkers;
    UE_LOG(LogTemp,Display,TEXT("[GuLiHotspots] world=%s mask=%u workers=%u"),*GetWorld()->GetPathName(),Mask,Workers);
}
bool UGuLiHotspotOptions::Enabled(const UWorld* World,EGuLiHotspot Option)
{
    const auto* Settings=World?World->GetSubsystem<UGuLiHotspotOptions>():nullptr;
    return Settings && Settings->Has(Option);
}
