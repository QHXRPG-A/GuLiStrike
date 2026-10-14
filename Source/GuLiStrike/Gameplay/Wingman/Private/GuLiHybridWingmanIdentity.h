#pragma once
#include "Network/GuLiHybridManifest.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
class UGuLiShipBuildCatalog;

namespace GuLiHybridWingman
{
inline FString Profile(FName Domain,FName Configuration,FName Capability)
{return TEXT("wingman/")+Domain.ToString().ToLower()+TEXT("/")+Configuration.ToString().ToLower()+TEXT("/")+Capability.ToString().ToLower();}
inline FGuLiHybridObjectKey MemberKey(const FGuLiHybridManifestData& Manifest,const FGuLiHybridObjectKey& Parent,
    const FString& Definition,uint8 Slot,uint32 Generation=1)
{
    auto Key=FGuLiHybridManifest::MakeStableKey(Manifest.MapPackage,EGuLiHybridObjectKind::Wingman,
        Parent.StableId.ToString(EGuidFormats::Digits)+TEXT("/")+LexToString(Parent.LifeGeneration)+TEXT("/")+Definition+TEXT("/")+LexToString(Slot));
    Key.LifeGeneration=Generation;return Key;
}
inline FGuLiWingmanGroupHandle Group(const FGuLiHybridManifestData& Manifest,const FGuLiHybridObjectKey& Parent,const FString& Definition)
{
    FGuLiWingmanGroupHandle Value;Value.ShipInstanceId=Parent.StableId;Value.ShipGeneration=Parent.LifeGeneration;
    Value.GroupGeneration=FGuLiHybridManifest::MakeStableKey(Manifest.MapPackage,EGuLiHybridObjectKind::Wingman,Definition).StableId.A;
    if (!Value.GroupGeneration) Value.GroupGeneration=1;return Value;
}
inline bool Resolve(const FGuLiHybridManifestData& Manifest,const FGuLiHybridObjectState& Record,FGuLiWingmanHandle& Member)
{
    if (Record.Key.Kind!=EGuLiHybridObjectKind::Wingman || !Record.Parent.IsValid() || Record.Parent.Kind!=EGuLiHybridObjectKind::Ship
        || !Manifest.Strings.Contains(Record.Definition.ToString()+TEXT("/configuration"))) return false;
    for (uint8 Slot=0;Slot<GULI_WINGMAN_GROUP_SIZE;++Slot)
        if (MemberKey(Manifest,Record.Parent,Record.Definition.ToString(),Slot,Record.Key.LifeGeneration)==Record.Key)
        {Member.Flight.Group=Group(Manifest,Record.Parent,Record.Definition.ToString());Member.Flight.FlightIndex=Slot/GULI_WINGMAN_MEMBERS_PER_FLIGHT;
         Member.MemberIndex=Slot%GULI_WINGMAN_MEMBERS_PER_FLIGHT;Member.EntityGeneration=Record.Key.LifeGeneration;return Member.IsValid();}
    return false;
}
GULISTRIKE_API bool AppendCatalog(const UGuLiShipBuildCatalog& Catalog,FName Domain,FGuLiHybridManifestData& Data,FString& Error);
}
