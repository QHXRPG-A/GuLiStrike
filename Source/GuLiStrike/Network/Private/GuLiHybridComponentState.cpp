#include "Network/Private/GuLiHybridComponentState.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Gameplay/Performance/GuLiHotspotOptions.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h"

namespace
{
constexpr uint32 Schema=1,MaximumFragments=64,MaximumBytes=1024*1024;
auto& Registrations()
{
    static auto* Values=new TArray<const FGuLiHybridComponentCapabilityRegistration*>;
    return *Values;
}
uint64& RegistrationRevision() { static uint64 Value=1;return Value; }
FString VersionKey(FName Name) {return TEXT("component/")+Name.ToString()+TEXT("/state_version");}
FString ClassKey(FName Name) {return TEXT("component/")+Name.ToString()+TEXT("/class");}
bool Capabilities(TArray<FGuLiHybridComponentCapability>& Out,FString& Error,bool Cached=false,bool* Rebuilt=nullptr)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiSim_ComponentContracts);
    struct FReloadWatch
    {
        FDelegateHandle Replaced, Reloaded;
        FReloadWatch()
        {
#if WITH_EDITOR
            Replaced=FCoreUObjectDelegates::OnObjectsReinstanced.AddLambda([](const auto&){++RegistrationRevision();});
#endif
            Reloaded=FCoreUObjectDelegates::ReloadCompleteDelegate.AddLambda([](EReloadCompleteReason){++RegistrationRevision();});
        }
        ~FReloadWatch()
        {
#if WITH_EDITOR
            FCoreUObjectDelegates::OnObjectsReinstanced.Remove(Replaced);
#endif
            FCoreUObjectDelegates::ReloadCompleteDelegate.Remove(Reloaded);
        }
    };
    static FReloadWatch ReloadWatch;
    if (Rebuilt) *Rebuilt=false;
    struct FContracts
    {
        uint64 Revision=0;
        TArray<FGuLiHybridComponentCapability> Values;
        TArray<TWeakObjectPtr<const UClass>> Classes;
    };
    static FContracts Cache;
    bool Current=Cached && Cache.Revision==RegistrationRevision();
    if (Current) for (int32 I=0;I<Cache.Values.Num();++I)
        if (!Cache.Classes[I].IsValid() || Cache.Classes[I].Get()!=Cache.Values[I].ComponentClass()) {Current=false;break;}
    if (Current) {Out=Cache.Values;return true;}
    if (Rebuilt) *Rebuilt=true;
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
    Out.Sort([](const auto& A,const auto& B){return A.Name.LexicalLess(B.Name);});
    if (Cached)
    {
        // A class reinstance also invalidates every per-object binding lease.
        if (Cache.Revision==RegistrationRevision()) ++RegistrationRevision();
        Cache.Revision=RegistrationRevision();Cache.Values=Out;Cache.Classes.Reset();
        for (const auto& C:Out) Cache.Classes.Add(C.ComponentClass());
    }
    return true;
}
struct FBinding {FGuLiHybridComponentCapability Capability;TWeakObjectPtr<UActorComponent> Component;};
struct FBindings
{
    TArray<FBinding> Local;
    TSharedPtr<const TArray<FBinding>> Lease;
    const TArray<FBinding>& Values() const {return Lease?*Lease:Local;}
    int32 Num() const {return Values().Num();}
    const FBinding& operator[](int32 Index) const {return Values()[Index];}
    auto begin() const {return Values().begin();}
    auto end() const {return Values().end();}
};
}
struct FGuLiHybridComponentBindingContext::FData
{
    struct FIdentity {TWeakObjectPtr<UActorComponent> Component;TWeakObjectPtr<UClass> Class;FName Name;};
    TWeakObjectPtr<const AActor> Actor;
    uint64 Registration=0;
    TArray<FIdentity> Identities;
    TSharedPtr<const TArray<FBinding>> Bound, Public, Logical;
};
FGuLiHybridComponentBindingContext::FGuLiHybridComponentBindingContext()=default;
FGuLiHybridComponentBindingContext::~FGuLiHybridComponentBindingContext()=default;
void FGuLiHybridComponentBindingContext::Reset() {Data.Reset();}
namespace
{
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
bool Bindings(const AActor& Actor,FBindings& Out,FGuLiHybridComponentBindingContext* Context=nullptr,bool Public=false,bool Logical=false)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiSim_ComponentBindings);
    auto* Metrics=Actor.GetWorld()?Actor.GetWorld()->GetSubsystem<UGuLiHotspotOptions>():nullptr;
    const bool Cached=Context && Metrics && Metrics->Has(EGuLiHotspot::ComponentCache);
    auto* Data=Cached?Context->Data.Get():nullptr;
    const auto& Components=Actor.GetComponents();
    bool Current=Data && Data->Actor==&Actor && Data->Registration==RegistrationRevision() && Data->Identities.Num()==Components.Num();
    if (Current) for (const auto& Identity:Data->Identities)
    {
        const auto* C=Identity.Component.Get();
        // Count alone misses replacement with a new component in the same slot.
        if (!C || !Components.Contains(C) || C->GetClass()!=Identity.Class.Get() || C->GetFName()!=Identity.Name)
        {Current=false;break;}
    }
    if (Current)
    {
        if (Metrics) ++Metrics->BindingHits;
        Out.Lease=Public?Data->Public:Logical?Data->Logical:Data->Bound;return true;
    }
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiSim_ComponentBindingRebuild);
    if (Metrics) ++Metrics->BindingRebuilds;
    bool Rebuilt=false;TArray<FGuLiHybridComponentCapability> All;FString Error;
    if (!Capabilities(All,Error,Cached,&Rebuilt)) return false;
    if (Metrics && Rebuilt) ++Metrics->ContractRebuilds;
    TArray<FBinding> Bound;
    for (const auto& C:All) for (auto* Component:Components)
        if (IsValid(Component) && Component->IsA(C.ComponentClass())) Bound.Add({C,Component});
    Bound.Sort([](const auto& A,const auto& B)
    {
        return A.Capability.Name==B.Capability.Name ? A.Component->GetFName().LexicalLess(B.Component->GetFName())
            : A.Capability.Name.LexicalLess(B.Capability.Name);
    });
    if (Bound.Num()>int32(MaximumFragments)) return false;
    if (!Cached)
    {
        if (Public) Bound.RemoveAll([](const auto& B){return !B.Capability.CapturePublic;});
        if (Logical) Bound.RemoveAll([](const auto& B){return !B.Capability.AdvanceLogical;});
        Out.Local=MoveTemp(Bound);return true;
    }
    auto PublicBindings=MakeShared<TArray<FBinding>>();
    auto LogicalBindings=MakeShared<TArray<FBinding>>();
    for (const auto& B:Bound) if (B.Capability.CapturePublic) PublicBindings->Add(B);
    for (const auto& B:Bound) if (B.Capability.AdvanceLogical) LogicalBindings->Add(B);
    auto Shared=MakeShared<TArray<FBinding>>(MoveTemp(Bound));
    Out.Lease=Public?PublicBindings:Logical?LogicalBindings:Shared;
    if (Cached)
    {
        auto Next=MakeUnique<FGuLiHybridComponentBindingContext::FData>();
        Next->Actor=&Actor;Next->Registration=RegistrationRevision();Next->Bound=Shared;Next->Public=PublicBindings;Next->Logical=LogicalBindings;
        for (auto* C:Components) if (C) Next->Identities.Add({C,C->GetClass(),C->GetFName()});
        Context->Data=MoveTemp(Next);
    }
    // Each operation holds its own immutable lease; callbacks may rebuild the context.
    return true;
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
bool Decode(const AActor& Actor,TConstArrayView<uint8> Bytes,FBindings& Bound,TArray<FFragment>& Fragments,bool Public=false,FGuLiHybridComponentBindingContext* Context=nullptr)
{
    if (!Bindings(Actor,Bound,Context,Public)) return false;
    FGuLiHybridStateArchive A(Bytes,MaximumBytes);Serialize(A,Fragments);
    if (!A.Done() || Bound.Num()!=Fragments.Num()) return false;
    for (int32 Index=0;Index<Bound.Num();++Index)
    {
        const auto& B=Bound[Index];const auto& F=Fragments[Index];
        if (!B.Component.IsValid() || F.Capability!=B.Capability.Name || F.Component!=B.Component->GetFName() || F.Version!=B.Capability.Version
            || !(Public?B.Capability.ValidatePublic(*B.Component,F.State):B.Capability.Validate(*B.Component,F.State))) return false;
    }
    return true;
}
}
FGuLiHybridComponentCapabilityRegistration::FGuLiHybridComponentCapabilityRegistration(FGuLiHybridComponentCapability In)
    :Capability(MoveTemp(In)) {Registrations().Add(this);++RegistrationRevision();}
FGuLiHybridComponentCapabilityRegistration::~FGuLiHybridComponentCapabilityRegistration() {Registrations().Remove(this);++RegistrationRevision();}
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
bool FGuLiHybridComponentState::Capture(const AActor& Actor,TArray<uint8>& Out,FGuLiHybridComponentBindingContext* Context)
{
    FBindings Bound;if (!Bindings(Actor,Bound,Context)) return false;TArray<FFragment> Fragments;
    for (const auto& B:Bound)
    {
        if (!B.Component.IsValid()) return false;
        FFragment F;F.Capability=B.Capability.Name;F.Component=B.Component->GetFName();F.Version=B.Capability.Version;
        if (!B.Capability.Capture(*B.Component,F.State) || !B.Capability.Validate(*B.Component,F.State)) return false;
        Fragments.Add(MoveTemp(F));
    }
    FGuLiHybridStateArchive A(MaximumBytes);Serialize(A,Fragments);return A.Finish(Out);
}
bool FGuLiHybridComponentState::Validate(const AActor& Actor,TConstArrayView<uint8> Bytes,FGuLiHybridComponentBindingContext* Context)
{FBindings Bound;TArray<FFragment> Fragments;return Decode(Actor,Bytes,Bound,Fragments,false,Context);}
bool FGuLiHybridComponentState::Advance(AActor& Actor,uint64 Tick,double Delta,FGuLiHybridComponentBindingContext* Context)
{
    if (!FMath::IsFinite(Delta) || Delta<0) return false;
    FBindings Bound;if (!Bindings(Actor,Bound,Context,false,true)) return false;
    {
        TRACE_CPUPROFILER_EVENT_SCOPE(GuLiSim_ComponentLogicalCallbacks);
        for (const auto& B:Bound)
        {
            if (!B.Component.IsValid()) return false;
            if (B.Capability.AdvanceLogical && (!B.Capability.NeedsLogicalAdvance
                || B.Capability.NeedsLogicalAdvance(*B.Component,Tick))) B.Capability.AdvanceLogical(*B.Component,Tick,Delta);
        }
    }
    return true;
}
void FGuLiHybridComponentState::ResetResourceRequests(AActor& Actor,uint64 Epoch,FGuLiHybridComponentBindingContext* Context)
{
    FBindings Bound;if (!Bindings(Actor,Bound,Context)) return;
    for (const auto& B:Bound) if (B.Component.IsValid()&&B.Capability.ResetResourceRequests) B.Capability.ResetResourceRequests(*B.Component.Get(),Epoch);
}
bool FGuLiHybridComponentState::Restore(AActor& Actor,TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage,FGuLiHybridComponentBindingContext* Context)
{
    FBindings Bound;TArray<FFragment> Fragments;if (!Decode(Actor,Bytes,Bound,Fragments,false,Context)) return false;
    for (int32 Index=0;Index<Bound.Num();++Index)
        if (!Bound[Index].Component.IsValid() || !Bound[Index].Capability.Restore(*Bound[Index].Component,Fragments[Index].State,Stage)) return false;
    return true;
}
bool FGuLiHybridComponentState::ApplyLedger(AActor& Actor,const FGuLiHybridObjectState& State,uint64 Revision,bool Effects,FGuLiHybridComponentBindingContext* Context)
{
    FBindings Bound;if (!Bindings(Actor,Bound,Context)) return false;
    for (const auto& B:Bound) if (!B.Component.IsValid() || (B.Capability.ApplyLedger && !B.Capability.ApplyLedger(*B.Component,State,Revision,Effects))) return false;
    return true;
}
bool FGuLiHybridComponentState::PrepareRestore(AActor& Actor,TConstArrayView<uint8> Bytes,FGuLiHybridComponentBindingContext* Context)
{
    FBindings Bound;TArray<FFragment> Fragments;if (!Decode(Actor,Bytes,Bound,Fragments,false,Context)) return false;
    for (int32 Index=0;Index<Bound.Num();++Index)
        if (!Bound[Index].Component.IsValid() || (Bound[Index].Capability.PrepareRestore && !Bound[Index].Capability.PrepareRestore(*Bound[Index].Component,Fragments[Index].State))) return false;
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
bool FGuLiHybridComponentState::HasPublicState(const AActor& Actor,FGuLiHybridComponentBindingContext* Context)
{
    FBindings Bound;return Bindings(Actor,Bound,Context) && Bound.Values().ContainsByPredicate([](const auto& B){return B.Capability.CapturePublic!=nullptr;});
}
EGuLiHybridPublicCapture FGuLiHybridComponentState::TryCapturePublic(const AActor& Actor,TArray<uint8>& Out,FGuLiHybridComponentBindingContext* Context)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(GuLiSim_ComponentPublicCapture);
    Out.Reset();FBindings Bound;if (!Bindings(Actor,Bound,Context,true)) return EGuLiHybridPublicCapture::Invalid;
    auto* Metrics=Actor.GetWorld()?Actor.GetWorld()->GetSubsystem<UGuLiHotspotOptions>():nullptr;
    if (!Bound.Num()) {if (Metrics) ++Metrics->PublicAbsent;return EGuLiHybridPublicCapture::Absent;}
    if (Metrics) ++Metrics->PublicCaptures;
    TArray<FFragment> Fragments;
    for (const auto& B:Bound)
    {
        if (!B.Component.IsValid()) return EGuLiHybridPublicCapture::Invalid;
        FFragment F;F.Capability=B.Capability.Name;F.Component=B.Component->GetFName();F.Version=B.Capability.Version;
        if (!B.Capability.CapturePublic(*B.Component,F.State) || !B.Capability.ValidatePublic(*B.Component,F.State)) return EGuLiHybridPublicCapture::Invalid;
        Fragments.Add(MoveTemp(F));
    }
    FGuLiHybridStateArchive A(MaximumBytes);Serialize(A,Fragments);
    return A.Finish(Out)?EGuLiHybridPublicCapture::Captured:EGuLiHybridPublicCapture::Invalid;
}
bool FGuLiHybridComponentState::CapturePublic(const AActor& Actor,TArray<uint8>& Out,FGuLiHybridComponentBindingContext* Context)
{
    const auto Result=TryCapturePublic(Actor,Out,Context);
    if (Result==EGuLiHybridPublicCapture::Invalid) return false;
    if (Result==EGuLiHybridPublicCapture::Absent)
    {TArray<FFragment> Empty;FGuLiHybridStateArchive A(MaximumBytes);Serialize(A,Empty);return A.Finish(Out);}
    return true;
}
bool FGuLiHybridComponentState::ValidatePublic(const AActor& Actor,TConstArrayView<uint8> Bytes,FGuLiHybridComponentBindingContext* Context)
{FBindings Bound;TArray<FFragment> Fragments;return Decode(Actor,Bytes,Bound,Fragments,true,Context);}
bool FGuLiHybridComponentState::ApplyPublic(AActor& Actor,TConstArrayView<uint8> Bytes,FGuLiHybridComponentBindingContext* Context)
{
    FBindings Bound;TArray<FFragment> Fragments;if (!Decode(Actor,Bytes,Bound,Fragments,true,Context)) return false;
    for (int32 I=0;I<Bound.Num();++I) if (!Bound[I].Component.IsValid() || !Bound[I].Capability.ApplyPublic(*Bound[I].Component,Fragments[I].State)) return false;
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
bool FGuLiHybridComponentState::ValidateDefinitionLayout(const FGuLiHybridManifestData& Manifest,const FString& Definition,const AActor& Actor,FGuLiHybridComponentBindingContext* Context)
{
    const auto* Expected=Manifest.Strings.Find(TEXT("component_layout/")+Definition);if (!Expected) return true;
    FBindings Bound;if (!Bindings(Actor,Bound,Context)) return false;TArray<FString> Entries;
    for (const auto& B:Bound) Entries.Add(LayoutEntry(B.Capability,B.Component->GetFName(),*B.Component));
    Entries.Sort();return *Expected==FString::Join(Entries,TEXT("\n"));
}
