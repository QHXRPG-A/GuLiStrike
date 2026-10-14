#include "Network/GuLiHybridManifestRegistry.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Building/GuLiHybridBuildingAdapter.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Gameplay/Data/GuLiCommanderSoldierResolver.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "NavigationSystem.h"

static bool AppendResourceStartup(UWorld& World,FGuLiHybridManifestData& Data,FString& Error)
{
    auto* Table=GetDefault<UGuLiUnitDataSettings>()->SoldierDataTable.LoadSynchronous();
    const auto* Map=LoadObject<UGuLiResourceMapDefinition>(nullptr,TEXT("/Game/GuLiStrike/Data/Resources/DA_CommanderResourceMap.DA_CommanderResourceMap"));
    const auto* Catalog=UGuLiBuildingCatalog::LoadDefaultCatalog();const auto* Factory=Catalog?Catalog->FindDefinition(EGuLiBuildingType::Factory):nullptr;
    const uint16 Type=static_cast<uint16>(Data.Numbers.FindRef(TEXT("economy/MiningVehicleUnitTypeId")));
    const int32 Initial=static_cast<int32>(Data.Numbers.FindRef(TEXT("economy/InitialMiningVehiclesPerTeam")));
    FGuLiSoldierDefinition Miner;bool Found=false;
    if (Table) for (const auto Row:Table->GetRowNames())
    {
        bool Valid=false;const auto D=FGuLiCommanderSoldierResolver::Resolve(Table,Row,FGuLiCommanderSoldierResolver::MakeFallbackDefinition(),Valid);
        if (Valid && D.UnitTypeId==Type) {Miner=D;Found=true;break;}
    }
    auto* Nav=FNavigationSystem::GetCurrent<UNavigationSystemV1>(&World);const auto* Prototype=Found && Miner.ActorClass?Cast<ACharacter>(Miner.ActorClass->GetDefaultObject()):nullptr;
    const auto* NavData=Nav && Prototype?Nav->GetNavDataForProps(Prototype->GetNavAgentPropertiesRef()):nullptr;
    if (!Map || !Factory || !Found || !Miner.ActorClass->IsChildOf(AGuLiMiningVehiclePawn::StaticClass()) || !NavData || Initial<0 || Initial>1024)
    {Error=TEXT("Missing formal miner/factory startup configuration or navigation");return false;}
    uint32 Ordinal=0;for (const auto& O:Data.Objects) if (O.Key.Kind==EGuLiHybridObjectKind::Engineer || O.Key.Kind==EGuLiHybridObjectKind::Miner) Ordinal=FMath::Max(Ordinal,O.Ordinal);
    for (const auto Team:{EGuLiTeam::Red,EGuLiTeam::Blue})
    {
        const FVector Anchor=Team==EGuLiTeam::Red?Map->SpawnAnchors.RedFactory:Map->SpawnAnchors.BlueFactory;FNavLocation Ground;
        if (!Nav->ProjectPointToNavigation(Anchor,Ground,FVector(100,100,100000),NavData) || FVector::DistSquared2D(Ground.Location,Anchor)>FMath::Square(100.))
        {Error=TEXT("Formal factory anchor has no navigable ground");return false;}
        auto& F=Data.Objects.AddDefaulted_GetRef();F.Team=Team;F.Ordinal=Team==EGuLiTeam::Red?101:102;F.Definition=FString::Printf(TEXT("building/%d"),Factory->DefinitionId);
        F.Key=FGuLiHybridManifest::MakeStableKey(Data.MapPackage,EGuLiHybridObjectKind::Building,FString::Printf(TEXT("initial/factory/%u"),uint8(Team)));
        F.Location=Ground.Location;F.FacingYawDegrees=Team==EGuLiTeam::Red?-90:90;F.Health=Factory->MaxHealth;F.Shield=Factory->MaxShield;F.EstimatedCost=.2;
        const FVector Assembly=Team==EGuLiTeam::Red?Map->SpawnAnchors.RedAssembly:Map->SpawnAnchors.BlueAssembly;
        for (int32 Index=0;Index<Initial;++Index)
        {
            const FVector Desired=Assembly+GuLiResources::InitialEngineeringVehicleOffset(Team,Index,false);
            if (!Nav->ProjectPointToNavigation(Desired,Ground,FVector(100,100,100000),NavData) || FVector::DistSquared2D(Ground.Location,Desired)>FMath::Square(100.))
            {Error=FString::Printf(TEXT("Formal miner has no navigable startup ground: team=%u index=%d"),uint8(Team),Index);return false;}
            auto& O=Data.Objects.AddDefaulted_GetRef();O.Team=Team;O.Ordinal=++Ordinal;O.Definition=FString::Printf(TEXT("unit/%u"),Type);
            O.Key=FGuLiHybridManifest::MakeStableKey(Data.MapPackage,EGuLiHybridObjectKind::Miner,FString::Printf(TEXT("initial/miner/%u/%d"),uint8(Team),Index));
            O.Location=Ground.Location+FVector(0,0,130);O.FacingYawDegrees=Team==EGuLiTeam::Red?-90:90;O.Health=Miner.MaxHealth;O.EstimatedCost=.5;
        }
    }
    auto Add=[&](const FString& Name,EGuLiHybridRuleKind Kind,const FString& Source)
    {auto& R=Data.Rules.AddDefaulted_GetRef();R.Id=Name;R.Kind=Kind;R.SourceDefinition=Source;};
    const double MiningRate=Data.Numbers.FindRef(TEXT("economy/MiningRatePerSecond"));
    const double ProcessingRate=Data.Numbers.FindRef(TEXT("economy/FactoryProcessingRatePerSecond"));
    const double AuthoredCapacity=Data.Numbers.FindRef(TEXT("economy/CargoCapacity"));
    if (!FMath::IsFinite(MiningRate) || MiningRate<1./86400. || MiningRate>60.
        || !FMath::IsFinite(ProcessingRate) || ProcessingRate<1./86400. || ProcessingRate>60.
        || !FMath::IsFinite(AuthoredCapacity) || AuthoredCapacity<1 || AuthoredCapacity>1000000 || FMath::FloorToDouble(AuthoredCapacity)!=AuthoredCapacity)
    {Error=TEXT("Invalid authored resource capability rates/capacity");return false;}
    for (const auto Row:Table->GetRowNames())
    {
        bool Valid=false;const auto D=FGuLiCommanderSoldierResolver::Resolve(Table,Row,FGuLiCommanderSoldierResolver::MakeFallbackDefinition(),Valid);
        if (!Valid) {Error=TEXT("Invalid resource unit catalog row: ")+Row.ToString();return false;}
        if (!D.ActorClass || !D.ActorClass->IsChildOf(AGuLiMiningVehiclePawn::StaticClass())) continue;
        const FString Definition=FString::Printf(TEXT("unit/%u"),D.UnitTypeId);
        if (!FGuLiHybridAdapterRegistry::BindDefinition(Data,Definition,TEXT("Miner"),Error)) return false;
        Data.Numbers.Add(FString::Printf(TEXT("miner_adapter/%u/health"),D.UnitTypeId),D.MaxHealth);
        Add(FString::Printf(TEXT("mining/position/%u"),D.UnitTypeId),EGuLiHybridRuleKind::ClaimMiningPosition,Definition);
        Add(FString::Printf(TEXT("mining/node/%u"),D.UnitTypeId),EGuLiHybridRuleKind::ClaimMiningNode,Definition);
        Add(FString::Printf(TEXT("mining/release/%u"),D.UnitTypeId),EGuLiHybridRuleKind::ReleaseMiningPosition,Definition);
    }
    Data.Numbers.Add(TEXT("miner_adapter/health"),Miner.MaxHealth);Data.Numbers.Add(TEXT("startup/miner_count"),Initial*2);Data.Numbers.Add(TEXT("startup/factory_count"),2);
    Data.Strings.Add(TEXT("startup/mining_slots_adapter"),TEXT("2"));
    Data.Strings.FindOrAdd(TEXT("startup/coverage"))+=TEXT(",Miner,Factory");
    Error.Reset();return true;
}

static const FGuLiHybridManifestRegistration ResourcesManifestProvider({TEXT("Resources"),EGuLiHybridManifestPhase::Startup,{TEXT("Buildings")},AppendResourceStartup});
