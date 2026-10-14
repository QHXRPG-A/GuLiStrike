#include "Network/Private/GuLiHybridComponentState.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"

namespace
{
constexpr uint32 Schema=1,MaximumFragments=64,MaximumBytes=1024*1024;
auto& Registrations()
{
    static auto* Values=new TArray<const FGuLiHybridComponentCapabilityRegistration*>;
    return *Values;
}
FString VersionKey(FName Name) {return TEXT("component/")+Name.ToString()+TEXT("/state_version");}
FString ClassKey(FName Name) {return TEXT("component/")+Name.ToString()+TEXT("/class");}
bool Capabilities(TArray<FGuLiHybridComponentCapability>& Out,FString& Error)
{
    TSet<FName> Names;
    for (const auto* Registration:Registrations())
    {
        const auto& C=Registration->Get();
        if (C.Name.IsNone() || !C.Version || !C.ComponentClass || !C.ComponentClass()
            || !C.ComponentClass()->IsChildOf(UActorComponent::StaticClass())
            || !C.Capture || !C.Validate || !C.Restore || Names.Contains(C.Name)
            || ((C.CapturePublic || C.ValidatePublic || C.ApplyPublic) && !(C.CapturePublic && C.ValidatePublic && C.ApplyPublic)))
        {Error=TEXT("Invalid or duplicate component state capability: ")+C.Name.ToString();return false;}
        Names.Add(C.Name);Out.Add(C);
    }
    Out.Sort([](const auto& A,const auto& B){return A.Name.LexicalLess(B.Name);});return true;
}
struct FBinding {FGuLiHybridComponentCapability Capability;UActorComponent* Component=nullptr;};
struct FTemplate {const UActorComponent* Component=nullptr;FName InstanceName;};
bool Templates(const UClass& Class,TArray<FTemplate>& Out)
{
    if (!Class.IsChildOf(AActor::StaticClass())) return false;
    const auto* CDO=Class.GetDefaultObject<AActor>();if (!CDO) return false;
    TSet<const UActorComponent*> Seen;TInlineComponentArray<UActorComponent*> Native;CDO->GetComponents(Native);
    for (const auto* C:Native) if (C) {Seen.Add(C);Out.Add({C,C->GetFName()});}
    auto* Actual=const_cast<UBlueprintGeneratedClass*>(Cast<UBlueprintGeneratedClass>(&Class));
    for (const UClass* Base=&Class;Base;Base=Base->GetSuperClass())
    {
        const auto* Generated=Cast<UBlueprintGeneratedClass>(Base);
        if (!Generated || !Generated->SimpleConstructionScript) continue;
        for (const auto* Node:Generated->SimpleConstructionScript->GetAllNodes())
        {
            const auto* C=Node->GetActualComponentTemplate(Actual);
            if (C && !Seen.Contains(C)) {Seen.Add(C);Out.Add({C,Node->GetVariableName()});}
        }
    }
    return true;
}
FString LayoutEntry(const FGuLiHybridComponentCapability& C,FName Instance,const UActorComponent& Component)
{return C.Name.ToString()+TEXT(":")+FString::FromInt(C.Version)+TEXT(":")+Instance.ToString()+TEXT(":")+Component.GetClass()->GetPathName();}
bool Bindings(const AActor& Actor,TArray<FBinding>& Out)
{
    TArray<FGuLiHybridComponentCapability> All;FString Error;if (!Capabilities(All,Error)) return false;
    TInlineComponentArray<UActorComponent*> Components;Actor.GetComponents(Components);
    for (const auto& C:All) for (auto* Component:Components)
        if (Component && Component->IsA(C.ComponentClass())) Out.Add({C,Component});
    Out.Sort([](const auto& A,const auto& B)
    {
        return A.Capability.Name==B.Capability.Name ? A.Component->GetFName().LexicalLess(B.Component->GetFName())
            : A.Capability.Name.LexicalLess(B.Capability.Name);
    });
    return Out.Num()<=int32(MaximumFragments);
}
struct FFragment {FName Capability,Component;uint32 Version=0;TArray<uint8> State;};
void Serialize(FGuLiHybridStateArchive& A,TArray<FFragment>& Fragments)
{
    uint32 Version=Schema;A.Value(Version);if (Version!=Schema) A.Reject();
    A.Array(Fragments,MaximumFragments,[](auto& Ar,auto& F)
    {
        Ar.Value(F.Capability);Ar.Value(F.Component);Ar.Value(F.Version);Ar.Bytes(F.State,MaximumBytes);
        if (F.Capability.IsNone() || F.Component.IsNone() || !F.Version || F.State.IsEmpty()) Ar.Reject();
    });
}
bool Decode(const AActor& Actor,TConstArrayView<uint8> Bytes,TArray<FBinding>& Bound,TArray<FFragment>& Fragments,bool Public=false)
{
    if (!Bindings(Actor,Bound)) return false;
    if (Public) Bound.RemoveAll([](const auto& B){return !B.Capability.CapturePublic;});
    FGuLiHybridStateArchive A(Bytes,MaximumBytes);Serialize(A,Fragments);
    if (!A.Done() || Bound.Num()!=Fragments.Num()) return false;
    for (int32 Index=0;Index<Bound.Num();++Index)
    {
        const auto& B=Bound[Index];const auto& F=Fragments[Index];
        if (F.Capability!=B.Capability.Name || F.Component!=B.Component->GetFName() || F.Version!=B.Capability.Version
            || !(Public?B.Capability.ValidatePublic(*B.Component,F.State):B.Capability.Validate(*B.Component,F.State))) return false;
    }
    return true;
}
}
FGuLiHybridComponentCapabilityRegistration::FGuLiHybridComponentCapabilityRegistration(FGuLiHybridComponentCapability In)
    :Capability(MoveTemp(In)) {Registrations().Add(this);}
FGuLiHybridComponentCapabilityRegistration::~FGuLiHybridComponentCapabilityRegistration() {Registrations().Remove(this);}
bool FGuLiHybridComponentState::BindContracts(FGuLiHybridManifestData& Manifest,FString& Error)
{
    TArray<FGuLiHybridComponentCapability> All;if (!Capabilities(All,Error)) return false;
    Manifest.Strings.Add(TEXT("component/contract"),FString::FromInt(FGuLiHybridComponentState::ContractVersion));
    Manifest.Numbers.Add(TEXT("network/component/motion_hz"),FGuLiHybridComponentState::PublicMotionHz);
    for (const auto& C:All)
    {
        Manifest.Numbers.Add(VersionKey(C.Name),C.Version);
        Manifest.Strings.Add(ClassKey(C.Name),C.ComponentClass()->GetPathName());
    }
    return true;
}
bool FGuLiHybridComponentState::ValidateContracts(const FGuLiHybridManifestData& Manifest,FString& Error)
{
    TArray<FGuLiHybridComponentCapability> All;if (!Capabilities(All,Error)) return false;
    if (Manifest.Strings.FindRef(TEXT("component/contract"))!=FString::FromInt(FGuLiHybridComponentState::ContractVersion)
        || Manifest.Numbers.FindRef(TEXT("network/component/motion_hz"))!=FGuLiHybridComponentState::PublicMotionHz)
    {Error=TEXT("Missing component state contract; export the current runtime manifest");return false;}
    TSet<FString> NumberKeys,StringKeys;
    for (const auto& C:All)
    {
        const auto* Version=Manifest.Numbers.Find(VersionKey(C.Name));StringKeys.Add(ClassKey(C.Name));NumberKeys.Add(VersionKey(C.Name));
        if (!Version || *Version!=C.Version || Manifest.Strings.FindRef(ClassKey(C.Name))!=C.ComponentClass()->GetPathName())
        {Error=TEXT("Incompatible component state capability: ")+C.Name.ToString();return false;}
    }
    for (const auto& Pair:Manifest.Numbers) if (Pair.Key.StartsWith(TEXT("component/")) && !NumberKeys.Contains(Pair.Key))
    {Error=TEXT("Unknown component state capability: ")+Pair.Key;return false;}
    for (const auto& Pair:Manifest.Strings) if (Pair.Key.StartsWith(TEXT("component/")) && Pair.Key!=TEXT("component/contract") && !StringKeys.Contains(Pair.Key))
    {Error=TEXT("Unknown component state capability: ")+Pair.Key;return false;}
    return true;
}
bool FGuLiHybridComponentState::Capture(const AActor& Actor,TArray<uint8>& Out)
{
    TArray<FBinding> Bound;if (!Bindings(Actor,Bound)) return false;TArray<FFragment> Fragments;
    for (const auto& B:Bound)
    {
        FFragment F;F.Capability=B.Capability.Name;F.Component=B.Component->GetFName();F.Version=B.Capability.Version;
        if (!B.Capability.Capture(*B.Component,F.State) || !B.Capability.Validate(*B.Component,F.State)) return false;
        Fragments.Add(MoveTemp(F));
    }
    FGuLiHybridStateArchive A(MaximumBytes);Serialize(A,Fragments);return A.Finish(Out);
}
bool FGuLiHybridComponentState::Validate(const AActor& Actor,TConstArrayView<uint8> Bytes)
{TArray<FBinding> Bound;TArray<FFragment> Fragments;return Decode(Actor,Bytes,Bound,Fragments);}
bool FGuLiHybridComponentState::Advance(AActor& Actor,uint64 Tick,double Delta)
{
    if (!FMath::IsFinite(Delta) || Delta<0) return false;
    TArray<FBinding> Bound;if (!Bindings(Actor,Bound)) return false;
    for (const auto& B:Bound) if (B.Capability.AdvanceLogical)
        B.Capability.AdvanceLogical(*B.Component,Tick,Delta);
    return true;
}
bool FGuLiHybridComponentState::Restore(AActor& Actor,TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage)
{
    TArray<FBinding> Bound;TArray<FFragment> Fragments;if (!Decode(Actor,Bytes,Bound,Fragments)) return false;
    for (int32 Index=0;Index<Bound.Num();++Index)
        if (!Bound[Index].Capability.Restore(*Bound[Index].Component,Fragments[Index].State,Stage)) return false;
    return true;
}
bool FGuLiHybridComponentState::ApplyLedger(AActor& Actor,const FGuLiHybridObjectState& State,uint64 Revision,bool Effects)
{
    TArray<FBinding> Bound;if (!Bindings(Actor,Bound)) return false;
    for (const auto& B:Bound) if (B.Capability.ApplyLedger && !B.Capability.ApplyLedger(*B.Component,State,Revision,Effects)) return false;
    return true;
}
bool FGuLiHybridComponentState::PrepareRestore(AActor& Actor,TConstArrayView<uint8> Bytes)
{
    TArray<FBinding> Bound;TArray<FFragment> Fragments;if (!Decode(Actor,Bytes,Bound,Fragments)) return false;
    for (int32 Index=0;Index<Bound.Num();++Index)
        if (Bound[Index].Capability.PrepareRestore && !Bound[Index].Capability.PrepareRestore(*Bound[Index].Component,Fragments[Index].State)) return false;
    return true;
}
TArray<FGuLiHybridObjectKey> FGuLiHybridComponentState::RestoreDependencies(TConstArrayView<uint8> Bytes)
{
    TArray<FGuLiHybridObjectKey> Out;TArray<FFragment> Fragments;TArray<FGuLiHybridComponentCapability> All;FString Error;
    FGuLiHybridStateArchive A(Bytes,MaximumBytes);Serialize(A,Fragments);if (!A.Done() || !Capabilities(All,Error)) return Out;
    for (const auto& F:Fragments)
    {
        const auto* C=All.FindByPredicate([&](const auto& V){return V.Name==F.Capability && V.Version==F.Version;});
        if (!C) return {};if (!C->Dependencies) continue;
        for (const auto& Key:C->Dependencies(F.State)) Out.AddUnique(Key);
    }
    return Out;
}
bool FGuLiHybridComponentState::HasPublicState(const AActor& Actor)
{
    TArray<FBinding> Bound;return Bindings(Actor,Bound) && Bound.ContainsByPredicate([](const auto& B){return B.Capability.CapturePublic!=nullptr;});
}
bool FGuLiHybridComponentState::CapturePublic(const AActor& Actor,TArray<uint8>& Out)
{
    TArray<FBinding> Bound;if (!Bindings(Actor,Bound)) return false;TArray<FFragment> Fragments;
    for (const auto& B:Bound) if (B.Capability.CapturePublic)
    {
        FFragment F;F.Capability=B.Capability.Name;F.Component=B.Component->GetFName();F.Version=B.Capability.Version;
        if (!B.Capability.CapturePublic(*B.Component,F.State) || !B.Capability.ValidatePublic(*B.Component,F.State)) return false;
        Fragments.Add(MoveTemp(F));
    }
    FGuLiHybridStateArchive A(MaximumBytes);Serialize(A,Fragments);return A.Finish(Out);
}
bool FGuLiHybridComponentState::ValidatePublic(const AActor& Actor,TConstArrayView<uint8> Bytes)
{TArray<FBinding> Bound;TArray<FFragment> Fragments;return Decode(Actor,Bytes,Bound,Fragments,true);}
bool FGuLiHybridComponentState::ApplyPublic(AActor& Actor,TConstArrayView<uint8> Bytes)
{
    TArray<FBinding> Bound;TArray<FFragment> Fragments;if (!Decode(Actor,Bytes,Bound,Fragments,true)) return false;
    for (int32 I=0;I<Bound.Num();++I) if (!Bound[I].Capability.ApplyPublic(*Bound[I].Component,Fragments[I].State)) return false;
    return true;
}
int32 FGuLiHybridComponentState::CountComponentTemplates(const UClass& ActorClass,const UClass& ComponentClass)
{
    TArray<FTemplate> All;if (!ComponentClass.IsChildOf(UActorComponent::StaticClass()) || !Templates(ActorClass,All)) return INDEX_NONE;
    int32 Count=0;for (const auto& T:All) if (T.Component->IsA(&ComponentClass)) ++Count;return Count;
}
bool FGuLiHybridComponentState::BindDefinitionLayout(FGuLiHybridManifestData& Manifest,const FString& Definition,const UClass& ActorClass,FString& Error)
{
    TArray<FTemplate> All;TArray<FGuLiHybridComponentCapability> Contracts;TArray<FString> Entries;
    if (Definition.IsEmpty() || !Templates(ActorClass,All) || !Capabilities(Contracts,Error)) return false;
    for (const auto& C:Contracts) for (const auto& T:All) if (T.Component->IsA(C.ComponentClass()))
    {
        Entries.Add(LayoutEntry(C,T.InstanceName,*T.Component));
        if (C.ExportDefinition && !C.ExportDefinition(*T.Component,Manifest,Definition,Error)) return false;
    }
    Entries.Sort();const FString Value=FString::Join(Entries,TEXT("\n"));
    if (Entries.Num()>int32(MaximumFragments) || Value.Len()>4096) {Error=TEXT("Component layout exceeds manifest capacity: ")+Definition;return false;}
    const FString Key=TEXT("component_layout/")+Definition;
    if (const auto* Old=Manifest.Strings.Find(Key);Old && *Old!=Value) {Error=TEXT("Conflicting component layout: ")+Definition;return false;}
    Manifest.Strings.Add(Key,Value);return true;
}
bool FGuLiHybridComponentState::ValidateDefinitionLayout(const FGuLiHybridManifestData& Manifest,const FString& Definition,const AActor& Actor)
{
    const auto* Expected=Manifest.Strings.Find(TEXT("component_layout/")+Definition);if (!Expected) return true;
    TArray<FBinding> Bound;if (!Bindings(Actor,Bound)) return false;TArray<FString> Entries;
    for (const auto& B:Bound) Entries.Add(LayoutEntry(B.Capability,B.Component->GetFName(),*B.Component));
    Entries.Sort();return *Expected==FString::Join(Entries,TEXT("\n"));
}
