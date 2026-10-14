#include "Gameplay/Resources/GuLiResourceInventorySubsystem.h"
#include "Gameplay/Resources/Private/GuLiHybridResourceInventoryState.h"
#include "Network/Private/GuLiHybridEntityState.h"
#include "Network/GuLiHybridManifestRegistry.h"
#include "Network/GuLiHybridManifest.h"
#include "UObject/StrongObjectPtr.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UGuLiResourceInventoryComponent* UGuLiResourceInventorySubsystem::FindInventory(FGuLiHybridObjectKey K) const
{
    if (const auto* V=Entities.Find(K)) return V->Get();
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();auto* A=H?H->FindBoundActor(K):nullptr;
    return A?A->FindComponentByClass<UGuLiResourceInventoryComponent>():nullptr;
}
FGuid UGuLiResourceInventorySubsystem::RequestExtraction(FGuLiHybridObjectKey K,int32 Node,FGuid Token)
{auto* V=FindInventory(K);return V?V->RequestExtraction(Node,Token):FGuid();}
FGuid UGuLiResourceInventorySubsystem::RequestDelivery(FGuLiHybridObjectKey K,FGuLiHybridObjectKey T)
{auto* V=FindInventory(K);return V?V->RequestDeliveryKey(T):FGuid();}
bool UGuLiResourceInventorySubsystem::Bind(FGuLiHybridObjectKey K,UGuLiResourceInventoryComponent& V)
{if (FindInventory(K)) return false;Entities.Add(K,&V);return true;}
void UGuLiResourceInventorySubsystem::Unbind(FGuLiHybridObjectKey K,const UGuLiResourceInventoryComponent& V)
{if (const auto* P=Entities.Find(K);P && P->Get()==&V) Entities.Remove(K);}
namespace
{
class FInventory final : public IGuLiHybridEntityCapability
{
    TStrongObjectPtr<UGuLiResourceInventoryComponent> Value;
    TWeakObjectPtr<UGuLiResourceInventorySubsystem> Host;
    FGuLiHybridObjectKey Key;
    bool Decode(TConstArrayView<uint8> Bytes,FGuLiHybridResourceInventoryState& S) const
    {FGuLiHybridStateArchive A(Bytes,1024*1024);S.Serialize(A);return A.Done() && Value->ValidateState(S);}
public:
    // UActorComponent's final GetWorld supports a direct UWorld outer. The
    // capability owns this transient component without allocating an Actor.
    FInventory(UGuLiResourceInventorySubsystem& H,const FGuLiHybridObjectState& O):Value(NewObject<UGuLiResourceInventoryComponent>(H.GetWorld(),NAME_None,RF_Transient)),Host(&H),Key(O.Key) {}
    bool Initialize(const FGuLiHybridObjectState& O)
    {return Value->BindEntity(*Host->GetWorld(),O) && Host->Bind(Key,*Value.Get());}
    ~FInventory() override {if (Host.IsValid()) Host->Unbind(Key,*Value.Get());Value->UnbindEntity();}
    void Advance(uint64,double Delta) override {Value->AdvanceLogical(float(Delta));}
    bool Capture(TArray<uint8>& Out) const override
    {FGuLiHybridResourceInventoryState S;if (!Value->CaptureState(S)) return false;FGuLiHybridStateArchive A(1024*1024);S.Serialize(A);return A.Finish(Out);}
    bool Validate(TConstArrayView<uint8> B) const override {FGuLiHybridResourceInventoryState S;return Decode(B,S);}
    bool Restore(TConstArrayView<uint8> B,EGuLiHybridRestoreStage Stage) override
    {FGuLiHybridResourceInventoryState S;return Decode(B,S) && (Stage!=EGuLiHybridRestoreStage::Link || Value->RestoreState(S));}
};
const FGuLiHybridEntityCapabilityRegistration Inventory({TEXT("ResourceInventory"),FGuLiHybridResourceInventoryState::Version,
    [](UWorld& W,const FGuLiHybridObjectState& O,const FGuLiHybridManifestData&)->TSharedPtr<IGuLiHybridEntityCapability>
    {
        auto* H=W.GetSubsystem<UGuLiResourceInventorySubsystem>();if (!H) return {};
        auto V=MakeShared<FInventory>(*H,O);if (!V->Initialize(O)) return {};return V;
    }});
const FGuLiHybridManifestRegistration Profiles({TEXT("EntityInventories"),EGuLiHybridManifestPhase::Derived,{TEXT("UnitDefinitions"),TEXT("Resources")},
    [](UWorld&,FGuLiHybridManifestData& M,FString& Error)
    {
        TSet<FName> Seen;
        for (const auto& P:GetDefault<UGuLiResourceInventorySettings>()->Entities)
        {
            const auto D=P.Definition.ToString();
            if (P.Definition.IsNone() || Seen.Contains(P.Definition) || (!P.bCargo && !P.bProcessing)
                || !M.Strings.Contains(TEXT("adapter/definition/")+D))
            {Error=TEXT("Duplicate or unknown inventory definition: ")+D;return false;}
            Seen.Add(P.Definition);
            // Actor components consume the same per-definition profile in
            // their exporter; do not attach a second entity-hosted inventory.
            if (M.Strings.Contains(TEXT("resource/component/")+D)) continue;
            // Reuse the component exporter so both hosts freeze identical rates and capacity rules.
            auto* Template=NewObject<UGuLiResourceInventoryComponent>();Template->bCargo=P.bCargo;Template->bProcessing=P.bProcessing;
            Template->bUseEconomyDefaults=false;Template->CargoCapacity=P.CargoCapacity;Template->ProcessingCapacity=P.ProcessingCapacity;
            Template->ExtractionRate=P.ExtractionRate;Template->ProcessingRate=P.ProcessingRate;
            if (!Template->ExportDefinition(M,D,Error) || !FGuLiHybridEntityState::Bind(M,D,TEXT("ResourceInventory"),Error)) return false;
            M.Strings.Add(TEXT("resource/component/")+D,TEXT("entity/ResourceInventory"));
        }
        return true;
    }});
}
