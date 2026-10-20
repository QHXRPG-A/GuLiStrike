#include "Gameplay/Performance/GuLiHotspotOptions.h"
#include "Network/GuLiNetworkSettings.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Modules/ModuleManifest.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"

namespace
{
// A feature becomes a normal default only after the paired capture proves its benefit.
// 2026-10-17: both paired scenarios passed (three pairs each, build-08).
constexpr uint32 ValidatedMask=31;
constexpr uint32 RetiredSyncBits=64|128;
#if !UE_BUILD_SHIPPING
TAutoConsoleVariable<int32> HotspotMask(TEXT("gs.Perf.HotspotMask"),ValidatedMask,
    TEXT("Next session only: 1 HUD, 2 SceneUI, 4 auxiliary positions, 8 components, 16 teleport, 32 gameplay worker, 256 gameplay pool. Retired bits 64/128 are ignored. Synchronous optimizations are always enabled; default mask is 31."));
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
    static bool Warned=false;
    if ((Value&RetiredSyncBits) && !Warned)
    {
        Warned=true;
        UE_LOG(LogTemp,Warning,TEXT("[GuLiHybridSync] retired mask bits 64/128 ignored; unique synchronous implementation is always active"));
    }
#endif
    return Value&~RetiredSyncBits;
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
    SyncIdentity=MakeShared<FJsonObject>();
    SyncIdentity->SetStringField(TEXT("implementation"),TEXT("hybrid-sync-unique-v2"));
    SyncIdentity->SetBoolField(TEXT("reduced_work"),true);
    SyncIdentity->SetBoolField(TEXT("incremental_delivery"),true);
    SyncIdentity->SetNumberField(TEXT("hotspot_mask"),Mask);
    // Read once at world startup. Sampling never reads or hashes DLLs.
#if !IS_MONOLITHIC
    auto Modules=MakeShared<FJsonObject>();
    for (const FName Name:{FName(TEXT("GuLiStrike")),FName(TEXT("GuLiNetCore"))})
    {
        const FString Path=FPaths::ConvertRelativePathToFull(FModuleManager::Get().GetModuleFilename(Name));
        auto Module=MakeShared<FJsonObject>();Module->SetStringField(TEXT("path"),Path);
        Module->SetStringField(TEXT("file_timestamp"),IFileManager::Get().GetTimeStamp(*Path).ToIso8601());
        FModuleManifest Manifest;
        if (FModuleManifest::TryRead(FModuleManifest::GetFileName(FPaths::GetPath(Path),true),Manifest))
            Module->SetStringField(TEXT("build_id"),Manifest.BuildId);
        UE_LOG(LogTemp,Display,TEXT("[GuLiHybridSyncModule] name=%s path=%s build_id=%s"),
            *Name.ToString(),*Path,*Manifest.BuildId);
        Modules->SetObjectField(Name.ToString(),Module);
    }
    SyncIdentity->SetObjectField(TEXT("modules"),Modules);
#endif
    UE_LOG(LogTemp,Display,TEXT("[GuLiHotspots] world=%s mask=%u workers=%u"),*GetWorld()->GetPathName(),Mask,Workers);
    UE_LOG(LogTemp,Display,TEXT("[GuLiHybridSync] implementation=hybrid-sync-unique-v2 reduced=1 incremental=1"));
}
TSharedRef<FJsonObject> UGuLiHotspotOptions::GetSyncIdentity() const
{ check(SyncIdentity.IsValid());return SyncIdentity.ToSharedRef(); }
bool UGuLiHotspotOptions::Enabled(const UWorld* World,EGuLiHotspot Option)
{
    const auto* Settings=World?World->GetSubsystem<UGuLiHotspotOptions>():nullptr;
    return Settings && Settings->Has(Option);
}
