#include "Battle/Combat/GuLiHybridTargetBinding.h"
#include "Gameplay/Resources/GuLiResourceFactoryActor.h"
#include "Gameplay/Resources/GuLiResourceWorldSubsystem.h"
#include "Gameplay/Resources/GuLiResourceMapDefinition.h"
#include "Gameplay/Building/GuLiBuildingCatalog.h"
#include "Gameplay/Building/GuLiHybridBuildingAdapter.h"
#include "Network/GuLiHybridAdapterRegistry.h"
#include "Gameplay/Building/GuLiBuildingRegistrySubsystem.h"
#include "Network/GuLiHybridActorWorldSubsystem.h"
#include "Network/GuLiHybridActorComponent.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Battle/Framework/GuLiBattleGameState.h"
#include "NavigationSystem.h"
#include "Gameplay/Navigation/Grid/GuLiGridQueries.h"
#include "Engine/World.h"

namespace GuLiHybridFactory
{
constexpr uint32 Version=6;
struct FPrivate
{
    FGuLiHybridObjectKey Key;
    int32 Territory=INDEX_NONE;
    FGuid Builder;
    EGuLiBuildingOrigin Origin=EGuLiBuildingOrigin::Map;
    FVector Dock=FVector::ZeroVector;
    FGuLiFactoryDoorState Door;
};
void Door(FGuLiHybridStateArchive& A,FGuLiFactoryDoorState& V)
{
    A.Value(V.bOpen);A.Value(V.StartAlpha);A.Value(V.StartServerTime);A.Value(V.Duration);
    if (V.StartAlpha<0 || V.StartAlpha>1 || V.StartServerTime<0 || V.Duration<=0) A.Reject();
}
void Private(FGuLiHybridStateArchive& A,FPrivate& V)
{
    uint32 Schema=Version;A.Value(Schema);if (Schema!=Version) A.Reject();
    A.Value(V.Key);A.Value(V.Territory);A.Value(V.Builder);A.Enum(V.Origin,uint8(EGuLiBuildingOrigin::Gift));
    A.Value(V.Dock);Door(A,V.Door);
    if (!V.Key.IsValid() || V.Key.Kind!=EGuLiHybridObjectKind::Building
        || (V.Origin==EGuLiBuildingOrigin::Manual && !V.Builder.IsValid())) A.Reject();
}
bool Decode(TConstArrayView<uint8> Bytes,FPrivate& Out) {FGuLiHybridStateArchive A(Bytes,1024*1024);Private(A,Out);return A.Done();}
}
class FGuLiHybridResourceFactoryAdapter final : public IGuLiHybridSimulationAdapter
{
    TWeakObjectPtr<UGuLiHybridActorWorldSubsystem> Owner;
    TWeakObjectPtr<AGuLiResourceFactoryActor> Actor;
    FGuLiHybridObjectState Record;
    bool bLocal=false;
    bool bSpawnNotificationPending=false;
    uint64 LastApplied=0,LastPublished=0,LastBusinessRevision=0;
    UWorld* World() const {return Owner.IsValid()?Owner->GetWorld():nullptr;}
    bool Create(const FGuid& RestoredBuilder=FGuid())
    {
        if (Actor.IsValid()) return true;
        auto* W=World();auto* Resources=W?W->GetSubsystem<UGuLiResourceWorldSubsystem>():nullptr;
        const auto* Config=Resources?Resources->GetEconomyConfig():nullptr;const auto* Catalog=UGuLiBuildingCatalog::LoadDefaultCatalog();
        const FString Definition=Record.Definition.ToString(),Suffix=Definition.RightChop(9);
        const auto* D=Catalog && Definition.StartsWith(TEXT("building/")) && Suffix.IsNumeric()?Catalog->FindById(FCString::Atoi(*Suffix)):nullptr;
        if (!Config || !D || GetGuLiHybridBuildingFamily(*D)!=TEXT("ResourceFactory") || Record.Health.MaxHealth!=D->MaxHealth
            || !Record.Ordinal) return false;
        const bool Manual=Record.Parent.Kind==EGuLiHybridObjectKind::BusinessWork;
        FGuid Builder=RestoredBuilder.IsValid()?RestoredBuilder:Record.Controller;
        if (Manual && !RestoredBuilder.IsValid())
        {
            FGuLiHybridObjectState Work;
            // A retired placement work is not required to materialize its
            // already admitted building on a receiving client.
            if (W->GetSubsystem<UGuLiHybridWorldSubsystem>()->GetObject(Record.Parent,Work))
            {
                if (Work.Team!=Record.Team || !Work.Controller.IsValid()) return false;
                Builder=Work.Controller;
            }
            if (!Builder.IsValid()) return false;
        }
        FNavLocation Ground;
        const FVector Direction=FVector(-Record.Location.X,-Record.Location.Y,0).GetSafeNormal();
        FVector Dock=Record.Location+Direction*Config->FactoryDockOffsetCentimeters;
        if (GuLiGrid::ProjectBody(*W,Dock,1,FVector(300,300,100000),Ground)) Dock=Ground.Location;
        const FTransform Transform(FRotator(0,Record.FacingYawDegrees,0),Record.Location);
        auto* Class=GetGuLiHybridBuildingClass(*D);
        if (!Class || !Class->IsChildOf(AGuLiResourceFactoryActor::StaticClass())
            || Owner->GetManifest().Strings.FindRef(FString::Printf(TEXT("building_adapter/%d/class"),D->DefinitionId))!=Class->GetPathName()) return false;
        auto* P=W->SpawnActorDeferred<AGuLiResourceFactoryActor>(Class,Transform,nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
        if (!P) return false;Actor=P;
        auto* Binding=NewObject<UGuLiHybridActorComponent>(P);P->AddInstanceComponent(Binding);Binding->Install(Record,bLocal);Binding->RegisterComponent();
        P->InitializeFactory(Record.Team,*Config,Dock,FGuLiControllableActorId(Record.Ordinal),Resources->FindTerritoryIndex(Record.Location),
            Manual?EGuLiBuildingOrigin::Manual:Record.Parent.Kind==EGuLiHybridObjectKind::Territory?EGuLiBuildingOrigin::Gift:EGuLiBuildingOrigin::Map,Record.bCompleted,Builder,D->DefinitionId);
        P->FinishSpawning(Transform);P->SetReplicates(false);P->SetReplicateMovement(false);P->SetActorTickEnabled(false);
        P->Lifecycle->SetComponentTickEnabled(false);ApplyLedger(Record,0,false);return true;
    }
    bool RestoreImpl(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage)
    {
        GuLiHybridFactory::FPrivate V;if (!GuLiHybridFactory::Decode(Bytes,V) || V.Key!=Record.Key) return false;
        if (Stage==EGuLiHybridRestoreStage::Detach) {Release();return true;}
        if (Stage==EGuLiHybridRestoreStage::Create) return Create(V.Builder);
        auto* P=Actor.Get();if (!P) return false;
        if (Stage==EGuLiHybridRestoreStage::Link)
        {
            P->Lifecycle->State.TerritoryIndex=V.Territory;P->Lifecycle->State.BuilderGuid=V.Builder;P->Lifecycle->State.Origin=V.Origin;
            P->DockPoint=V.Dock;P->OnRep_DockPoint();P->DoorState=V.Door;P->OnRep_DoorState();
            LastApplied=LastPublished=0;
        }
        if (Stage==EGuLiHybridRestoreStage::Activate)
        {
            P->FindComponentByClass<UGuLiHybridActorComponent>()->Install(Record,bLocal);
            if (bLocal && P->Lifecycle->bSlotsPending && P->Lifecycle->ConstructionPrototype.IsValid())
                P->Lifecycle->PrepareConstructionSlots(*P->Lifecycle->ConstructionPrototype.Get());
        }
        return true;
    }
public:
    explicit FGuLiHybridResourceFactoryAdapter(UGuLiHybridActorWorldSubsystem& V):Owner(&V) {}
    ~FGuLiHybridResourceFactoryAdapter() override {Release();}
    UObject* LifetimeOwner() const override {return Owner.Get();}
    uint32 Version() const override {return GuLiHybridFactory::Version;}
    bool Prepare(const FGuLiHybridObjectState& V) override
    {Record=V;bSpawnNotificationPending=V.LifeState==0 && V.SpawnEvent.IsValid();return Create();}
    bool PrepareRestore(const FGuLiHybridObjectState& State,TConstArrayView<uint8> Bytes) override
    {
        Record=State;GuLiHybridFactory::FPrivate V;
        if (!GuLiHybridFactory::Decode(Bytes,V) || V.Key!=State.Key) return false;
        bSpawnNotificationPending=State.LifeState==0 && State.SpawnEvent.IsValid();
        return Create(V.Builder);
    }
    void SetAuthority(const FGuLiHybridObjectState& V,bool Local) override
    {Record=V;bLocal=Local;LastApplied=LastPublished=0;if (auto* P=Actor.Get()) P->FindComponentByClass<UGuLiHybridActorComponent>()->Install(V,Local);}
    void Advance(uint64 At,double Delta) override
    {
        auto* P=Actor.Get();if (!P || !bLocal || Record.LifeState!=1 || !UGuLiHybridActorComponent::HasSimulation(P)) return;
        if (At<LastPublished+6) return;
        FGuLiHybridStateArchive A(4096);uint32 Schema=Version();auto Door=P->DoorState;A.Value(Schema);GuLiHybridFactory::Door(A,Door);
        TArray<uint8> Bytes;if (A.Finish(Bytes) && World()->GetSubsystem<UGuLiHybridWorldSubsystem>()->PublishMotion(Record.Key,Bytes)) LastPublished=At;
    }
    bool Execute(const FGuLiHybridCommand&,const FGuLiHybridCommandMember&) override {return false;}
    bool ApplyMotion(TConstArrayView<uint8> Bytes,uint64 At) override
    {
        auto* P=Actor.Get();if (!P || bLocal) return false;if (At<=LastApplied) return true;
        FGuLiHybridStateArchive A(Bytes,4096);uint32 Schema=0;FGuLiFactoryDoorState Door;A.Value(Schema);GuLiHybridFactory::Door(A,Door);
        if (Schema!=Version() || !A.Done()) return false;
        P->DoorState=Door;P->OnRep_DoorState();LastApplied=At;return true;
    }
    void ApplyLedger(const FGuLiHybridObjectState& V,uint64 Revision,bool Effects) override
    {
        Record=V;if (auto* P=Actor.Get())
        {
            P->FindComponentByClass<UGuLiHybridActorComponent>()->Install(V,bLocal);P->Team=V.Team;
            P->Lifecycle->ApplyHybridState(V,Effects);
            FGuLiTargetHandle Target;Target.Kind=EGuLiTargetKind::GroundActor;Target.AuthorityId=V.Key.StableId;Target.Generation=V.Key.LifeGeneration;Target.LocalId=V.Ordinal;
            P->FindComponentByClass<UGuLiCombatHealthComponent>()->ApplyHybridHealth(Target,V.Team,V.Health,World()->GetGameState<AGuLiBattleGameState>()->GetMatchEpoch(),Revision,Effects);
            if (Effects && Revision>LastBusinessRevision)
            {
                if (bSpawnNotificationPending && V.SpawnEvent.IsValid() && V.LifeState<=1)
                    World()->GetSubsystem<UGuLiBuildingRegistrySubsystem>()->OnSpawned.Broadcast(*P->Lifecycle);
                bSpawnNotificationPending=false; LastBusinessRevision=Revision;
            }
        }
    }


    bool Capture(TArray<uint8>& Bytes) const override
    {
        const auto* P=Actor.Get();if (!P) return false;GuLiHybridFactory::FPrivate V;V.Key=Record.Key;V.Territory=P->Lifecycle->State.TerritoryIndex;
        V.Builder=P->Lifecycle->State.BuilderGuid;V.Origin=P->Lifecycle->State.Origin;
        V.Dock=P->DockPoint;V.Door=P->DoorState;FGuLiHybridStateArchive A(1024*1024);GuLiHybridFactory::Private(A,V);return A.Finish(Bytes);
    }
    bool Validate(TConstArrayView<uint8> Bytes) const override
    {
        GuLiHybridFactory::FPrivate V;if (!GuLiHybridFactory::Decode(Bytes,V) || V.Key!=Record.Key) return false;
        return true;
    }


    bool Restore(TConstArrayView<uint8> B,EGuLiHybridRestoreStage S) override {return RestoreImpl(B,S);}
    bool RestoreQuery(TConstArrayView<uint8> B,EGuLiHybridRestoreStage S) override {return RestoreImpl(B,S);}
    void Release() override {if (auto* P=Actor.Get()) P->Destroy();Actor.Reset();LastApplied=LastPublished=0;}
};
static const FGuLiHybridAdapterRegistration GGuLiFactoryAdapterFamily({TEXT("ResourceFactory"),EGuLiHybridObjectKind::Building,GuLiHybridFactory::Version,
    [](UGuLiHybridActorWorldSubsystem& Owner,const FGuLiHybridObjectState&)->TSharedPtr<IGuLiHybridSimulationAdapter>
    {return MakeShared<FGuLiHybridResourceFactoryAdapter>(Owner);},nullptr,GuLiHybridTargets::Actor<EGuLiTargetKind::GroundActor,true>,EGuLiHybridOwnershipPolicy::SharedGroup,0,false,TEXT("match/resources")});
static const FGuLiHybridBuildingArchetypeRegistration FactoryArchetype({TEXT("ResourceFactory"),100,
    [](const FGuLiBuildingDefinition& D) {return D.Category==EGuLiBuildingCategory::Factory && D.CompletionUnitTypeId==0;},
    []()->UClass* {return AGuLiResourceFactoryActor::StaticClass();}});
