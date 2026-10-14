#include "Development/GuLiHybridNetworkReviewActor.h"
#include "Network/GuLiHybridSessionSubsystem.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/GuLiHybridManifest.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Components/SceneComponent.h"
#include "Misc/Paths.h"

AGuLiHybridNetworkReviewActor::AGuLiHybridNetworkReviewActor()
{
    PrimaryActorTick.bCanEverTick=false;SetReplicates(false);
    RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("ReviewRoot"));
    Tags.Add(TEXT("HybridNetworkReview"));
}
void AGuLiHybridNetworkReviewActor::PrepareLaunchArguments()
{
    BuildLaunchArguments(LaunchArguments,PreparationError);
}
bool AGuLiHybridNetworkReviewActor::BuildLaunchArguments(FString& OutArguments,FString& OutError) const
{
    OutArguments.Reset();OutError.Reset();
    const auto Safe=[](const FString& Value){return !Value.Contains(TEXT("\"")) && !Value.Contains(TEXT("\r")) && !Value.Contains(TEXT("\n"));};
    if (Mode!=EGuLiNetworkMode::Hybrid || (Units!=200 && Units!=600) || AdditionalFlights!=(Units==600?500:0) || !Room.IsValid()
        || (Transport!=EGuLiNetworkTransport::LocalUdp && Transport!=EGuLiNetworkTransport::EOS)
        || (RunMode!=EGuLiClientRunMode::Rendered && RunMode!=EGuLiClientRunMode::LogicOnly)
        || (Seat!=EGuLiCommanderRole::Commander && Seat!=EGuLiCommanderRole::Ground && Seat!=EGuLiCommanderRole::Air)
        || (Transport==EGuLiNetworkTransport::LocalUdp && !DevelopmentIdentity.IsValid())
        || (Team!=EGuLiTeam::Red && Team!=EGuLiTeam::Blue) || UdpPort<0 || UdpPort>65535
        || Manifest.FilePath.IsEmpty() || !Safe(Manifest.FilePath) || !Safe(CoordinatorUrl))
    {OutError=TEXT("Select a valid Hybrid transport/seat/run mode, 200/0 or 600/500, a room, a development identity for LocalUdp, and a manifest.");return false;}
    FString ConfigHash,MapHash,Map,Error;
    if (!FGuLiHybridManifest::ValidateFile(Manifest.FilePath,ConfigHash,MapHash,Map,Error)) {OutError=Error;return false;}
    if (Map!=TEXT("/Game/Maps/LVL_CommanderMassPrototype")) {OutError=TEXT("The manifest belongs to a different map.");return false;}
    FGuLiHybridManifestData Data;
    if (!FGuLiHybridManifest::Read(Manifest.FilePath,Data,Error)) {OutError=Error;return false;}
    int32 ActualUnits=0;for (const auto& O:Data.Objects) if (O.Key.Kind==EGuLiHybridObjectKind::Soldier) ++ActualUnits;
    if (ActualUnits!=Units) {OutError=TEXT("Selected population differs from the manifest.");return false;}
    const FString SeatName=StaticEnum<EGuLiCommanderRole>()->GetNameStringByValue(int64(Seat));
    const FString ModeName=RunMode==EGuLiClientRunMode::LogicOnly?TEXT("LogicOnly"):TEXT("Rendered");
    OutArguments=FString::Printf(TEXT("%s -game -GuLiNetMode=Hybrid -GuLiTransport=%s -GuLiRoom=%s -GuLiDevPlayerId=%s -GuLiTeam=%s -GuLiRole=%s -GuLiClientRunMode=%s -GuLiUdpPort=%d -GuLiCoordinatorUrl=\"%s\" -GuLiManifest=\"%s\"%s"),
        *Map,Transport==EGuLiNetworkTransport::EOS?TEXT("EOS"):TEXT("LocalUdp"),*Room.ToString(EGuidFormats::Digits),
        *DevelopmentIdentity.ToString(EGuidFormats::Digits),Team==EGuLiTeam::Red?TEXT("Red"):TEXT("Blue"),*SeatName,*ModeName,
        UdpPort,*CoordinatorUrl,*FPaths::ConvertRelativePathToFull(Manifest.FilePath),RunMode==EGuLiClientRunMode::LogicOnly?TEXT(" -NullRHI"):TEXT(""));
    OutArguments+=FString::Printf(TEXT(" -GuLiAcceptanceRun=review-%s -GuLiAcceptancePopulation=%d -GuLiAcceptanceExtraFlights=%d -GuLiAcceptanceSeed=20261011"),
        *Room.ToString(EGuidFormats::Digits),Units,AdditionalFlights);
    return true;
}
void AGuLiHybridNetworkReviewActor::SelectObjects(const TArray<FGuLiHybridObjectKey>& Objects)
{
    auto* W=GetWorld();if (auto* H=W && W->IsGameWorld()?W->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr) H->SetCommanderSelection(Objects);
}
FString AGuLiHybridNetworkReviewActor::GetOwnershipStatus() const
{
    auto* W=GetWorld();auto* H=W && W->IsGameWorld()?W->GetSubsystem<UGuLiHybridWorldSubsystem>():nullptr;
    return H?H->GetReviewEvidenceJson():TEXT("{}");
}
bool AGuLiHybridNetworkReviewActor::LeaveRoom()
{
    auto* W=GetWorld();auto* GI=W && W->IsGameWorld()?W->GetGameInstance():nullptr;
    auto* S=GI?GI->GetSubsystem<UGuLiHybridSessionSubsystem>():nullptr;return S && S->RequestLeave();
}
