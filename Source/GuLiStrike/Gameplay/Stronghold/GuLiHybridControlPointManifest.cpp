#include "Network/GuLiHybridManifestRegistry.h"
#include "GuLiNetControlPoint.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Building/GuLiHybridBuildingAdapter.h"
#include "HAL/IConsoleManager.h"

static bool AppendControlPointStartup(UWorld&,FGuLiHybridManifestData& Data,FString& Error)
{
    const auto* Map=LoadObject<UGuLiResourceMapDefinition>(nullptr,TEXT("/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap.DA_CommanderResourceMap"));
    const auto* Catalog=UGuLiBuildingCatalog::LoadDefaultCatalog();
    if (!Map || !Catalog) {Error=TEXT("Missing authored stronghold catalog");return false;}
    const auto* Definition=Catalog->Definitions.FindByPredicate([](const auto& D){return D.Category==EGuLiBuildingCategory::Stronghold;});
    if (!Definition || !Definition->IsUsable()) {Error=TEXT("Missing stronghold definition");return false;}
    const FString Name=FString::Printf(TEXT("territory/%d"),Definition->DefinitionId);
    if (!FGuLiHybridAdapterRegistry::BindDefinition(Data,Name,TEXT("ControlPoint"),Error)) return false;
    Data.Strings.Add(TEXT("control_point/definition/")+Name,UTF8_TO_TCHAR(GuLiNet::ControlPointCapability));
    for (const auto& Pair:TArray<TPair<const TCHAR*,const TCHAR*>>{{TEXT("sample_ticks"),TEXT("guli.stronghold.SampleSeconds")},
        {TEXT("capture_ticks"),TEXT("guli.stronghold.CaptureSeconds")},{TEXT("upkeep_ticks"),TEXT("guli.stronghold.UpkeepSeconds")},
        {TEXT("upkeep_blue"),TEXT("guli.stronghold.UpkeepBlue")},{TEXT("upkeep_red"),TEXT("guli.stronghold.UpkeepRed")}})
    {
        const auto* Variable=IConsoleManager::Get().FindConsoleVariable(Pair.Value);
        if (!Variable) {Error=TEXT("Missing stronghold tuning variable");return false;}
        const bool Time=FString(Pair.Key).EndsWith(TEXT("ticks"));
        Data.Numbers.Add(TEXT("control_point/")+FString(Pair.Key),Time?FMath::RoundToInt64(double(Variable->GetFloat())*60.):Variable->GetInt());
    }
    TArray<int32> UpkeepOrder;for (int32 Index=0;Index<Map->Territories.Num();++Index) UpkeepOrder.Add(Index);
    UpkeepOrder.Sort([Map](int32 A,int32 B) {return Map->Territories[A].TerritoryId.LexicalLess(Map->Territories[B].TerritoryId);});
    for (int32 Rank=0;Rank<UpkeepOrder.Num();++Rank)
        Data.Numbers.Add(FString::Printf(TEXT("control_point/%d/upkeep_rank"),UpkeepOrder[Rank]),Rank);
    for (int32 Index=0;Index<Map->Territories.Num();++Index)
    {
        const auto& T=Map->Territories[Index];auto& O=Data.Objects.AddDefaulted_GetRef();
        O.Key=FGuLiHybridManifest::MakeStableKey(Data.MapPackage,EGuLiHybridObjectKind::Territory,T.TerritoryId.ToString());
        O.Ordinal=uint32(Index+1);O.Definition=Name;O.Location=T.OutpostGroundLocation;O.EstimatedCost=.1;O.bSupplied=true;
    }
    Data.Numbers.Add(TEXT("capability/control_point/version"),GuLiNet::ControlPointCapabilityVersion);
    for (const TCHAR* Operation:{TEXT("capture")})
    {
        auto& R=Data.Rules.AddDefaulted_GetRef();R.Kind=EGuLiHybridRuleKind::Capability;R.SourceDefinition=R.TargetDefinition=Name;
        R.Id=FString(Operation)+TEXT("/")+Name;R.Capability=UTF8_TO_TCHAR(GuLiNet::ControlPointCapability);
        R.CapabilityVersion=GuLiNet::ControlPointCapabilityVersion;R.CapabilityOperation=Operation;
    }
    TMap<int32,uint32> Gifts;for (const auto Id:Definition->FirstCaptureGiftIds) ++Gifts.FindOrAdd(Id);
    for (const auto& [Id,Count]:Gifts)
    {
        const auto* D=Catalog->FindById(Id);const FName Family=D?GetGuLiHybridBuildingFamily(*D):NAME_None;
        if (!D || Family.IsNone()) {Error=TEXT("Gift definition has no registered gameplay capability family");return false;}
        const FString Target=FString::Printf(TEXT("building/%d"),Id);
        if (!FGuLiHybridAdapterRegistry::BindDefinition(Data,Target,Family,Error)) return false;
        auto& R=Data.Rules.AddDefaulted_GetRef();R.Id=TEXT("gift/")+Name+TEXT("/")+Target;R.Kind=EGuLiHybridRuleKind::Spawn;
        R.SourceDefinition=Name;R.TargetDefinition=Target;R.SpawnKind=EGuLiHybridObjectKind::Building;
        R.SpawnHealth=D->MaxHealth;R.SpawnShield=D->MaxShield;R.SpawnShieldRadius=D->ShieldRadius;R.SpawnCost=.2;R.SpawnControllerSeat=0;
        R.MaximumLiveChildren=Count;R.bSpawnCompleted=true;
    }
    Data.Numbers.Add(TEXT("startup/control_point_count"),Map->Territories.Num());
    Data.Strings.FindOrAdd(TEXT("startup/coverage"))+=TEXT(",ControlPoint");Error.Reset();return true;
}

static const FGuLiHybridManifestRegistration ControlPointsManifestProvider({TEXT("ControlPoints"),EGuLiHybridManifestPhase::Startup,{TEXT("Resources")},AppendControlPointStartup});
