#include "Gameplay/Building/GuLiHybridBuildingAdapter.h"
#include "Battle/Combat/GuLiHybridTargetBinding.h"
#include "Network/GuLiHybridManifestRegistry.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiBuildingProductionComponent.h"
#include "Gameplay/Building/GuLiConstructedUnitComponent.h"
#include "GuLiNetReplacement.h"
#include "Gameplay/Building/GuLiBuildingRegistrySubsystem.h"
#include "Gameplay/Building/GuLiPlacedBuilding.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Gameplay/Data/GuLiCommanderDataSubsystem.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Gameplay/Data/GuLiCommanderSoldierResolver.h"
#include "Engine/DataTable.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Network/Private/GuLiHybridComponentState.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

namespace GuLiHybridBuilding
{
constexpr uint32 StateVersion=5;
struct FPrivate
{
    FGuLiHybridObjectKey Key;
    int32 Definition=0,Territory=INDEX_NONE;
    EGuLiBuildingOrigin Origin=EGuLiBuildingOrigin::Manual;
    FGuid Builder;
};
bool Supported(const FGuLiBuildingDefinition& D)
{ return GetGuLiHybridBuildingFamily(D)==TEXT("PlacedBuilding"); }
int32 Definition(const FName Name)
{
    const FString Text=Name.ToString(); if (!Text.StartsWith(TEXT("building/"))) return 0;
    const FString Suffix=Text.RightChop(9); return Suffix.IsNumeric()?FCString::Atoi(*Suffix):0;
}
void Private(FGuLiHybridStateArchive& A,FPrivate& V)
{
    uint32 Version=StateVersion; A.Value(Version); if (Version!=StateVersion) A.Reject();
    A.Value(V.Key); A.Value(V.Definition); A.Value(V.Territory); A.Enum(V.Origin,2);
    A.Value(V.Builder);
    if (!V.Key.IsValid() || V.Key.Kind!=EGuLiHybridObjectKind::Building || V.Definition<1
        || (V.Origin==EGuLiBuildingOrigin::Manual && !V.Builder.IsValid())) A.Reject();
}
bool Decode(TConstArrayView<uint8> Bytes,FPrivate& V)
{ FGuLiHybridStateArchive A(Bytes); Private(A,V); return A.Done(); }
}

/** Native placed-building simulation; query copies use the same collision and target registry. */
class FGuLiHybridBuildingAdapter final : public IGuLiHybridSimulationAdapter
{
    TWeakObjectPtr<UGuLiHybridActorWorldSubsystem> Owner;
    TWeakObjectPtr<AGuLiPlacedBuilding> Actor;
    FGuLiHybridObjectState Record;
    bool bLocal=false,bSpawnNotificationPending=false;
    uint64 LastBusinessRevision=0;
    UWorld* World() const { return Owner.IsValid()?Owner->GetWorld():nullptr; }
    bool Create(const FGuid& RestoredBuilder=FGuid())
    {
        if (Actor.IsValid()) return true;
        auto* W=World(); auto* Catalog=UGuLiBuildingCatalog::LoadDefaultCatalog();
        const auto* D=Catalog?Catalog->FindById(GuLiHybridBuilding::Definition(Record.Definition)):nullptr;
        if (!W || !D || !GuLiHybridBuilding::Supported(*D) || Record.Key.Kind!=EGuLiHybridObjectKind::Building
            || !Record.Ordinal || (Record.Parent.Kind!=EGuLiHybridObjectKind::Territory && !RestoredBuilder.IsValid() && !Record.Controller.IsValid())) return false;
        const auto* HashHealth=Owner->GetManifest().Numbers.Find(FString::Printf(TEXT("building_adapter/%d/health"),D->DefinitionId));
        if (!HashHealth || *HashHealth!=D->MaxHealth || Record.Health.MaxHealth!=D->MaxHealth) return false;
        const FTransform Transform(FRotator(0,Record.FacingYawDegrees,0),Record.Location+FVector(0,0,D->CollisionExtent.Z));
        FGuid Builder=RestoredBuilder.IsValid()?RestoredBuilder:Record.Controller;
        if (!RestoredBuilder.IsValid() && Record.Parent.Kind==EGuLiHybridObjectKind::BusinessWork)
        {
            FGuLiHybridObjectState Work;
            // Placement work may retire before an observer receives this
            // committed building. Its admitted controller already identifies
            // the builder; a query copy must not depend on transient work.
            if (W->GetSubsystem<UGuLiHybridWorldSubsystem>()->GetObject(Record.Parent,Work))
            {
                if (Work.Team!=Record.Team || !Work.Controller.IsValid()) return false;
                Builder=Work.Controller;
            }
            if (!Builder.IsValid()) return false;
        }
        auto* Class=GetGuLiHybridBuildingClass(*D);
        if (!Class || !Class->IsChildOf(AGuLiPlacedBuilding::StaticClass())
            || Owner->GetManifest().Strings.FindRef(FString::Printf(TEXT("building_adapter/%d/class"),D->DefinitionId))!=Class->GetPathName()) return false;
        auto* P=W->SpawnActorDeferred<AGuLiPlacedBuilding>(Class,Transform,nullptr,nullptr,
            ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!P) return false; Actor=P;
        auto* Binding=NewObject<UGuLiHybridActorComponent>(P); P->AddInstanceComponent(Binding); Binding->RegisterComponent(); Binding->Install(Record,bLocal);
        const auto* Resources=W->GetSubsystem<UGuLiResourceWorldSubsystem>();
        if (!P->InitializeFromDefinition(D->DefinitionId,Record.Team,Builder,
            Resources?Resources->FindTerritoryIndex(Record.Location):INDEX_NONE,
            Record.Parent.Kind==EGuLiHybridObjectKind::Territory?EGuLiBuildingOrigin::Gift:EGuLiBuildingOrigin::Manual,Record.bCompleted))
        { Release(); return false; }
        P->FinishSpawning(Transform); P->SetReplicates(false); P->SetActorTickEnabled(false);
        ApplyLedger(Record,0,false); return true;
    }
    bool RestoreImpl(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage)
    {
        GuLiHybridBuilding::FPrivate V; if (!GuLiHybridBuilding::Decode(Bytes,V) || V.Key!=Record.Key
            || V.Definition!=GuLiHybridBuilding::Definition(Record.Definition)) return false;
        if (Stage==EGuLiHybridRestoreStage::Detach) { Release(); return true; }
        if (Stage==EGuLiHybridRestoreStage::Create) return Create(V.Builder);
        auto* P=Actor.Get(); if (!P) return false;
        if (Stage==EGuLiHybridRestoreStage::Link)
        {
            auto* Life=P->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
            Life->State.Origin=V.Origin; Life->State.TerritoryIndex=V.Territory; Life->State.BuilderGuid=V.Builder;
        }
        if (Stage==EGuLiHybridRestoreStage::Activate)
        {
            P->FindComponentByClass<UGuLiHybridActorComponent>()->Install(Record,bLocal);
            auto* Life=P->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
            if (bLocal && Life->bSlotsPending && Life->ConstructionPrototype.IsValid())
                Life->PrepareConstructionSlots(*Life->ConstructionPrototype.Get());
        }
        return true;
    }
public:
    explicit FGuLiHybridBuildingAdapter(UGuLiHybridActorWorldSubsystem& InOwner):Owner(&InOwner) {}
    UObject* LifetimeOwner() const override { return Owner.Get(); }
    uint32 Version() const override { return GuLiHybridBuilding::StateVersion; }
    bool Prepare(const FGuLiHybridObjectState& State) override
    { Record=State; bSpawnNotificationPending=State.LifeState==0 && State.SpawnEvent.IsValid(); return Create(); }
    bool PrepareRestore(const FGuLiHybridObjectState& State,TConstArrayView<uint8> Bytes) override
    {
        Record=State;GuLiHybridBuilding::FPrivate V;
        if (!GuLiHybridBuilding::Decode(Bytes,V) || V.Key!=State.Key || V.Definition!=GuLiHybridBuilding::Definition(State.Definition)) return false;
        bSpawnNotificationPending=State.LifeState==0 && State.SpawnEvent.IsValid();
        return Create(V.Builder);
    }
    void SetAuthority(const FGuLiHybridObjectState& State,bool Local) override
    {
        Record=State; bLocal=Local; if (auto* P=Actor.Get()) P->FindComponentByClass<UGuLiHybridActorComponent>()->Install(State,Local);
    }
    // Component capabilities own construction/production progression and their
    // public samples. A new combination does not add another family payload.
    void Advance(uint64,double) override {}
    bool Execute(const FGuLiHybridCommand&,const FGuLiHybridCommandMember&) override { return false; }
    bool ApplyMotion(TConstArrayView<uint8> Bytes,uint64) override {return Bytes.IsEmpty();}
    void ApplyLedger(const FGuLiHybridObjectState& State,uint64 Revision,bool Effects) override
    {
        Record=State; auto* P=Actor.Get(); if (!P) return;P->ApplyHybridTeam(State.Team);
        P->FindComponentByClass<UGuLiHybridActorComponent>()->Install(State,bLocal);
        auto* Life=P->FindComponentByClass<UGuLiBuildingLifecycleComponent>(); Life->ApplyHybridState(State,Effects);
        FGuLiTargetHandle Target; Target.Kind=EGuLiTargetKind::GroundActor; Target.AuthorityId=State.Key.StableId;
        Target.Generation=State.Key.LifeGeneration; Target.LocalId=State.Ordinal;
        const auto* Battle=World()->GetGameState<AGuLiBattleGameState>();
        P->FindComponentByClass<UGuLiCombatHealthComponent>()->ApplyHybridHealth(Target,State.Team,State.Health,
            Battle?Battle->GetMatchEpoch():0,Revision,Effects && Life->GetState().Phase!=EGuLiBuildingPhase::ConvertedToUnit);
        if (Effects && Revision>LastBusinessRevision)
        {
            if (bSpawnNotificationPending && State.SpawnEvent.IsValid() && State.LifeState<=1)
                World()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->OnSpawned.Broadcast(*Life);
            bSpawnNotificationPending=false;
            LastBusinessRevision=Revision;
        }
    }


    bool Capture(TArray<uint8>& Bytes) const override
    {
        auto* P=Actor.Get(); if (!P) return false; const auto* Life=P->FindComponentByClass<UGuLiBuildingLifecycleComponent>();
        GuLiHybridBuilding::FPrivate V; V.Key=Record.Key; V.Definition=Life->State.DefinitionId; V.Territory=Life->State.TerritoryIndex;
        V.Origin=Life->State.Origin; V.Builder=Life->State.BuilderGuid;
        FGuLiHybridStateArchive A; GuLiHybridBuilding::Private(A,V); return A.Finish(Bytes);
    }
    bool Validate(TConstArrayView<uint8> Bytes) const override
    {
        GuLiHybridBuilding::FPrivate V; if (!GuLiHybridBuilding::Decode(Bytes,V) || V.Key!=Record.Key
            || V.Definition!=GuLiHybridBuilding::Definition(Record.Definition)) return false;
        const auto* Catalog=UGuLiBuildingCatalog::LoadDefaultCatalog(); const auto* D=Catalog?Catalog->FindById(V.Definition):nullptr;
        const auto* P=Actor.Get();
        return D && GuLiHybridBuilding::Supported(*D) && P;
    }
    bool Restore(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override { return RestoreImpl(Bytes,Stage); }
    bool RestoreQuery(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage) override { return RestoreImpl(Bytes,Stage); }
    void Release() override { if (auto* P=Actor.Get()) P->Destroy(); Actor.Reset(); }
};

static const FGuLiHybridBuildingArchetypeRegistration PlacedArchetype({TEXT("PlacedBuilding"),0,
    [](const FGuLiBuildingDefinition& D) {return D.Category!=EGuLiBuildingCategory::Stronghold;},
    []()->UClass* {return AGuLiPlacedBuilding::StaticClass();}});
static const FGuLiHybridAdapterRegistration GGuLiBuildingAdapterFamily({TEXT("PlacedBuilding"),EGuLiHybridObjectKind::Building,GuLiHybridBuilding::StateVersion,
    [](UGuLiHybridActorWorldSubsystem& Owner,const FGuLiHybridObjectState&)->TSharedPtr<IGuLiHybridSimulationAdapter>
    {return MakeShared<FGuLiHybridBuildingAdapter>(Owner);},nullptr,GuLiHybridTargets::Actor<EGuLiTargetKind::GroundActor,true>,EGuLiHybridOwnershipPolicy::SharedGroup,0,false,TEXT("match/resources")});

static bool AppendBuildingCatalog(UWorld& World,FGuLiHybridManifestData& Data,FString& Error)
{
    auto* Catalog=UGuLiBuildingCatalog::LoadDefaultCatalog(); if (!Catalog || !Catalog->IsUsable()) { Error=TEXT("Missing building catalog"); return false; }
    for (const auto& D : Catalog->Definitions)
    {
        const auto Family=GetGuLiHybridBuildingFamily(D);
        if (Family.IsNone())
        {
            if (D.Category==EGuLiBuildingCategory::Stronghold) continue;
            Error=FString::Printf(TEXT("Building %d has no unambiguous registered gameplay archetype"),D.DefinitionId);return false;
        }
        auto* ActorClass=GetGuLiHybridBuildingClass(D);if (!ActorClass) {Error=TEXT("Building implementation class is unavailable");return false;}
        const int32 ProductionComponents=FGuLiHybridComponentState::CountComponentTemplates(*ActorClass,*UGuLiBuildingProductionComponent::StaticClass());
        if (ProductionComponents<0 || ProductionComponents>1 || (D.HasProductionCapability() && ProductionComponents!=1)
            || FGuLiHybridComponentState::CountComponentTemplates(*ActorClass,*UGuLiBuildingLifecycleComponent::StaticClass())!=1)
        {Error=FString::Printf(TEXT("Building %d has missing or ambiguous native/Blueprint capability components"),D.DefinitionId);return false;}
        const FString Definition=FString::Printf(TEXT("building/%d"),D.DefinitionId);
        if (!FGuLiHybridComponentState::BindDefinitionLayout(Data,Definition,*ActorClass,Error)) return false;
        if (!FGuLiHybridAdapterRegistry::BindDefinition(Data,Definition,Family,Error)
            || !FGuLiHybridAdapterRegistry::BindDefinition(Data,FString::Printf(TEXT("work/place/%d"),D.DefinitionId),TEXT("PlacementWork"),Error)) return false;
        Data.Numbers.Add(FString::Printf(TEXT("building_adapter/%d/health"),D.DefinitionId),D.MaxHealth);
        Data.Strings.Add(FString::Printf(TEXT("building_adapter/%d/class"),D.DefinitionId),
            ActorClass->GetPathName());
        if (D.CompletionUnitTypeId>0)
        {
            if (FGuLiHybridComponentState::CountComponentTemplates(*ActorClass,*UGuLiConstructedUnitComponent::StaticClass())!=1)
            {Error=TEXT("Construction replacement requires exactly one conversion capability");return false;}
            const auto* Table=GetDefault<UGuLiUnitDataSettings>()->SoldierDataTable.LoadSynchronous();
            bool Found=false;FGuLiSoldierDefinition Unit;
            if (Table) for (const auto Name:Table->GetRowNames())
            {bool Valid=false;const auto U=FGuLiCommanderSoldierResolver::Resolve(Table,Name,FGuLiCommanderSoldierResolver::MakeFallbackDefinition(),Valid);
                if (Valid && U.UnitTypeId==D.CompletionUnitTypeId) {Unit=U;Found=true;break;}}
            if (!Found || !Unit.UsesMass() || !Unit.bConstructionOnly || Unit.bSummonOnly)
            {Error=TEXT("Construction replacement has no compatible authored unit");return false;}
            Data.Strings.Add(TEXT("population/definition/")+Definition,TEXT("army"));
            Data.Numbers.Add(TEXT("population/definition/")+Definition+TEXT("/units"),1);
            auto& R=Data.Rules.AddDefaulted_GetRef();R.Id=TEXT("replace/")+Definition;R.SourceDefinition=Definition;
            R.Kind=EGuLiHybridRuleKind::Capability;R.Capability=UTF8_TO_TCHAR(GuLiNet::Replacement::Capability);
            R.CapabilityVersion=GuLiNet::Replacement::Version;R.CapabilityOperation=TEXT("replace");
            R.TargetDefinition=FString::Printf(TEXT("unit/%d"),D.CompletionUnitTypeId);R.SpawnKind=EGuLiHybridObjectKind::Soldier;
            R.SpawnHealth=Unit.MaxHealth;R.SpawnControllerSeat=0;
        }
        const auto Cost=D.Cost;
        for (const auto Kind : {EGuLiHybridRuleKind::Reserve,EGuLiHybridRuleKind::Refund,EGuLiHybridRuleKind::Spawn})
        {
            auto& R=Data.Rules.AddDefaulted_GetRef(); R.Kind=Kind; R.SourceDefinition=FString::Printf(TEXT("work/place/%d"),D.DefinitionId);
            const TCHAR* Operation=Kind==EGuLiHybridRuleKind::Reserve?TEXT("reserve"):Kind==EGuLiHybridRuleKind::Refund?TEXT("refund"):TEXT("place");
            R.Id=FString::Printf(TEXT("%s/building/%d"),Operation,D.DefinitionId); R.Blue=Cost.Blue; R.Red=Cost.Red;
            if (Kind!=EGuLiHybridRuleKind::Spawn) continue;
            R.TargetDefinition=Definition; R.SpawnKind=EGuLiHybridObjectKind::Building;
            R.SpawnHealth=D.MaxHealth; R.SpawnShield=D.MaxShield; R.SpawnShieldRadius=D.ShieldRadius;
            R.SpawnCost=1; R.bSpawnCompleted=false; R.MaximumLiveChildren=GuLiBuildingPlacementPolicy::MaximumBuildingsPerBuilder;
        }
        auto& Admit=Data.Rules.AddDefaulted_GetRef();Admit.Id=FString::Printf(TEXT("admit/work/place/%d"),D.DefinitionId);
        Admit.Kind=EGuLiHybridRuleKind::Spawn;Admit.SourceDefinition=TEXT("player/ground");
        Admit.TargetDefinition=FString::Printf(TEXT("work/place/%d"),D.DefinitionId);Admit.SpawnKind=EGuLiHybridObjectKind::BusinessWork;
        Admit.SpawnCost=.025;Admit.MaximumLiveChildren=64;
        const FString WorkDefinition=Admit.TargetDefinition;
        // Construction-only units are a Commander ability. Do not publish a
        // Ground admission rule which a crafted command could otherwise reach.
        if (D.CompletionUnitTypeId>0) Data.Rules.Pop();
        auto& Retire=Data.Rules.AddDefaulted_GetRef();Retire.Id=FString::Printf(TEXT("retire/work/place/%d"),D.DefinitionId);
        Retire.Kind=EGuLiHybridRuleKind::Destroy;Retire.SourceDefinition=Retire.TargetDefinition=WorkDefinition;
        auto& Complete=Data.Rules.AddDefaulted_GetRef(); Complete.Id=TEXT("complete/")+Definition;
        Complete.Kind=EGuLiHybridRuleKind::Complete; Complete.SourceDefinition=Complete.TargetDefinition=Definition;
        auto& Destroy=Data.Rules.AddDefaulted_GetRef(); Destroy.Id=TEXT("destroy/")+Definition;
        Destroy.Kind=EGuLiHybridRuleKind::Destroy; Destroy.SourceDefinition=Destroy.TargetDefinition=Definition;
        if (D.HasProductionCapability())
        {
            auto* Table=GetDefault<UGuLiUnitDataSettings>()->SoldierDataTable.LoadSynchronous();
            bool Found=false; FGuLiSoldierDefinition Soldier;
            if (Table) for (const auto Name : Table->GetRowNames())
            {
                bool Valid=false;
                const auto Candidate=FGuLiCommanderSoldierResolver::Resolve(Table,Name,FGuLiCommanderSoldierResolver::MakeFallbackDefinition(),Valid);
                if (Valid && Candidate.UnitTypeId==D.ProductionUnitId) { Soldier=Candidate; Found=true; break; }
            }
            if (!Found) { Error=TEXT("Missing production unit definition"); return false; }
            auto& R=Data.Rules.AddDefaulted_GetRef(); R.Id=TEXT("produce/")+Definition; R.SourceDefinition=Definition;
            R.Kind=EGuLiHybridRuleKind::Spawn; R.TargetDefinition=FString::Printf(TEXT("unit/%d"),D.ProductionUnitId);
            // The family may be registered by another Startup provider. Resolve
            // its kind after all definitions, never infer it from the unit id.
            R.SpawnKind=EGuLiHybridObjectKind::None; R.SpawnHealth=Soldier.MaxHealth; R.SpawnControllerSeat=0;
        }
    }
    Data.Strings.Add(TEXT("startup/building_definitions"),TEXT("CatalogCapabilities/v1")); Error.Reset(); return true;
}

static const FGuLiHybridManifestRegistration BuildingsManifestProvider({TEXT("Buildings"),EGuLiHybridManifestPhase::Definitions,{TEXT("Engineer")},AppendBuildingCatalog});

static const FGuLiHybridManifestRegistration ProductionManifestProvider({TEXT("ProductionDefinitions"),EGuLiHybridManifestPhase::Derived,
    {TEXT("Buildings"),TEXT("UnitDefinitions"),TEXT("Resources")},
    [](UWorld&,FGuLiHybridManifestData& Data,FString& Error)
    {
        for (auto& Rule:Data.Rules) if (Rule.Kind==EGuLiHybridRuleKind::Spawn && Rule.Id.StartsWith(TEXT("produce/")))
        {
            FGuLiHybridAdapterFamily Family;
            if (!FGuLiHybridAdapterRegistry::ResolveDefinition(Data,Rule.TargetDefinition,Family,Error)) return false;
            Rule.SpawnKind=Family.Kind;
        }
        return true;
    }});

static const FGuLiHybridManifestRegistration CommanderBuildingsManifestProvider({TEXT("CommanderBuildings"),EGuLiHybridManifestPhase::Derived,{TEXT("Buildings"),TEXT("ArmyState")},
    [](UWorld&,FGuLiHybridManifestData& Data,FString& Error)
    {
        const auto Root=Data.Strings.FindRef(TEXT("army/root_definition"));
        const auto* Catalog=UGuLiBuildingCatalog::LoadDefaultCatalog();
        if (Root.IsEmpty() || !Catalog) {Error=TEXT("Commander placement is missing its registered team authority");return false;}
        for (const auto& D:Catalog->Definitions)
        {
            if (GetGuLiHybridBuildingFamily(D).IsNone()) continue;
            auto& R=Data.Rules.AddDefaulted_GetRef();R.Id=FString::Printf(TEXT("admit/work/place/%d/commander"),D.DefinitionId);
            R.Kind=EGuLiHybridRuleKind::Spawn;R.SourceDefinition=Root;R.TargetDefinition=FString::Printf(TEXT("work/place/%d"),D.DefinitionId);
            R.SpawnKind=EGuLiHybridObjectKind::BusinessWork;R.SpawnCost=.025;R.MaximumLiveChildren=64;R.SpawnControllerSeat=0;
        }
        return true;
    }});
