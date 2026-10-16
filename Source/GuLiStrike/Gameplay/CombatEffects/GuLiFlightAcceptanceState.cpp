#include "Gameplay/CombatEffects/GuLiFlightAcceptanceSubsystem.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Network/GuLiNetworkSettings.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Subsystems/SubsystemCollection.h"

namespace
{
const FName AcceptanceFlightSystem(TEXT("AcceptanceFlights"));
constexpr uint32 AcceptanceFlightVersion=2;
struct FAcceptanceFlightEntry
{
    int32 Index=0;
    FGuLiHybridObjectKey Source;
    FGuid Current,Prepared;
    uint64 NextGeneration=0;
};
struct FAcceptanceFlightPiece
{
    bool Enabled=false;
    int32 Count=0,Seed=0;
    TArray<FAcceptanceFlightEntry> Entries;
};
void EncodeAcceptance(FGuLiHybridStateArchive& A,FAcceptanceFlightPiece& V)
{
    uint32 Version=AcceptanceFlightVersion; A.Value(Version);
    A.Value(V.Enabled); A.Value(V.Count); A.Value(V.Seed);
    if (Version!=AcceptanceFlightVersion || (V.Count!=0 && V.Count!=500) || (V.Enabled && V.Count!=500)) A.Reject();
    TSet<int32> Indices;
    A.Array(V.Entries,500,[&](auto& Archive,auto& E)
    {
        Archive.Value(E.Index); Archive.Value(E.Source); Archive.Value(E.Current); Archive.Value(E.Prepared); Archive.Value(E.NextGeneration);
        if (E.Index<0 || E.Index>=V.Count || Indices.Contains(E.Index) || !E.Source.IsValid()
            || E.Source.Kind!=EGuLiHybridObjectKind::Soldier || E.NextGeneration==MAX_uint64
            || (E.Current.IsValid() && E.Current==E.Prepared) || (E.Prepared.IsValid() && !E.Current.IsValid())) Archive.Reject();
        Indices.Add(E.Index);
    });
}
bool DecodeAcceptance(TConstArrayView<uint8> Bytes,FAcceptanceFlightPiece& Out)
{ FGuLiHybridStateArchive A(Bytes); EncodeAcceptance(A,Out); return A.Done(); }
}

/** The load generator owns no battle authority. Handoff preserves its recipe
 *  identities, including admitted effects retained by the original simulator. */
class FGuLiAutomaticFlightState final:public IGuLiHybridSharedStateAdapter
{
    TWeakObjectPtr<UGuLiFlightAcceptanceSubsystem> Owner;
    void ClearBindings(UGuLiFlightAcceptanceSubsystem& S,const TArray<FGuLiHybridObjectKey>& Objects) const
    {
        for (auto I=S.AutomaticSources.CreateIterator();I;++I) if (Objects.Contains(I.Value()))
        {
            S.AutomaticFlights.Remove(I.Key());S.AutomaticPreparedFlights.Remove(I.Key());
            S.AutomaticGenerations.Remove(I.Key());I.RemoveCurrent();
        }
    }
    bool Merge(const TArray<FGuLiHybridSharedStatePiece>& Pieces,FAcceptanceFlightPiece& Out) const
    {
        const auto* S=Owner.Get(); if (!S || Pieces.IsEmpty()) return false;
        auto* H=S->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>(); if (!H) return false;
        TSet<int32> Indices; bool First=true;
        for (const auto& Piece:Pieces)
        {
            FAcceptanceFlightPiece Data;
            if (Piece.Name!=AcceptanceFlightSystem || Piece.Version!=Version() || !Piece.Player.IsValid()
                || !DecodeAcceptance(Piece.Bytes,Data) || Data.Count!=S->AutomaticCount || Data.Seed!=S->AutomaticSeed)
            {
                UE_LOG(LogTemp,Error,TEXT("AcceptanceFlights import rejected: shape/config player=%s count=%d/%d seed=%d/%d"),
                    *Piece.Player.ToString(),Data.Count,S->AutomaticCount,Data.Seed,S->AutomaticSeed);
                return false;
            }
            if (First) { Out.Enabled=Data.Enabled; Out.Count=Data.Count; Out.Seed=Data.Seed; First=false; }
            else if (Data.Enabled!=Out.Enabled) return false;
            for (const auto& Entry:Data.Entries)
            {
                FGuLiHybridObjectState Source;
                if (Indices.Contains(Entry.Index) || !Piece.Objects.Contains(Entry.Source) || !H->GetObject(Entry.Source,Source))
                {
                    UE_LOG(LogTemp,Error,TEXT("AcceptanceFlights import rejected: source/scope index=%d source=%s/%u"),
                        Entry.Index,*Entry.Source.StableId.ToString(),Entry.Source.LifeGeneration);
                    return false;
                }
                for (const auto& Id:{Entry.Current,Entry.Prepared}) if (Id.IsValid())
                {
                    FGuLiHybridObjectKey Key; Key.Kind=EGuLiHybridObjectKind::Flight; Key.StableId=Id; Key.LifeGeneration=1;
                    FGuLiHybridObjectState Flight;
                    if (H->GetObject(Key,Flight) && Flight.Parent!=Entry.Source)
                    {
                        UE_LOG(LogTemp,Error,TEXT("AcceptanceFlights import rejected: parent index=%d flight=%s source=%s/%u parent=%s/%u"),
                            Entry.Index,*Id.ToString(),*Entry.Source.StableId.ToString(),Entry.Source.LifeGeneration,
                            *Flight.Parent.StableId.ToString(),Flight.Parent.LifeGeneration);
                        return false;
                    }
                }
                Out.Entries.Add(Entry); Indices.Add(Entry.Index);
            }
        }
        return true;
    }
public:
    explicit FGuLiAutomaticFlightState(UGuLiFlightAcceptanceSubsystem& S):Owner(&S) {}
    UObject* LifetimeOwner() const override { return Owner.Get(); }
    uint32 Version() const override { return AcceptanceFlightVersion; }
    bool UsesObject(const FGuLiHybridObjectKey& Key) const override
    {
        const auto* S=Owner.Get();
        // Empty producer domains are authoritative too. A dead producer whose
        // slots moved elsewhere must clear a recipient's old startup bindings.
        return S && S->AutomaticCount>0 && Key.Kind==EGuLiHybridObjectKind::Soldier;
    }
    bool Capture(const TArray<FGuLiHybridObjectKey>& Objects,TArray<uint8>& Bytes) const override
    {
        const auto* S=Owner.Get(); if (!S) return false;
        auto* H=S->GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>(); if (!H) return false;
        FAcceptanceFlightPiece Data; Data.Enabled=S->bAutomaticLoad; Data.Count=S->AutomaticCount; Data.Seed=S->AutomaticSeed;
        for (const auto& Pair:S->AutomaticSources) if (Objects.Contains(Pair.Value))
            Data.Entries.Add({Pair.Key,Pair.Value,S->AutomaticFlights.FindRef(Pair.Key),S->AutomaticPreparedFlights.FindRef(Pair.Key),S->AutomaticGenerations.FindRef(Pair.Key)});
        Data.Entries.Sort([](const auto& A,const auto& B){return A.Index<B.Index;});
        FGuLiHybridStateArchive A; EncodeAcceptance(A,Data); return A.Finish(Bytes);
    }
    bool Validate(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>&) const override
    { FAcceptanceFlightPiece Data; return Merge(Pieces,Data); }
    bool Restore(const TArray<FGuLiHybridSharedStatePiece>& Pieces,const TArray<FGuLiHybridObjectKey>& Objects,EGuLiHybridRestoreStage Stage) override
    {
        auto* S=Owner.Get(); if (!S) return false;
        if (Stage==EGuLiHybridRestoreStage::Detach)
        {
            ClearBindings(*S,Objects);
            return true;
        }
        if (Stage==EGuLiHybridRestoreStage::Create)
        {
            FAcceptanceFlightPiece Data; if (!Merge(Pieces,Data)) return false;
            // Replace only this incoming producer scope. Retained flight
            // entities keep their own original simulator and retirement path.
            ClearBindings(*S,Objects);
            S->bAutomaticLoad=Data.Enabled;
            for (const auto& Entry:Data.Entries)
            {
                S->AutomaticSources.Add(Entry.Index,Entry.Source); S->AutomaticGenerations.Add(Entry.Index,Entry.NextGeneration);
                if (Entry.Current.IsValid()) S->AutomaticFlights.Add(Entry.Index,Entry.Current);
                if (Entry.Prepared.IsValid()) S->AutomaticPreparedFlights.Add(Entry.Index,Entry.Prepared);
            }
        }
        if (Stage==EGuLiHybridRestoreStage::Activate)
        {
            auto* Ledger=S->GetWorld()->GetSubsystem<UGuLiDamageLedgerSubsystem>(); if (!Ledger) return false;
            S->AutomaticEpoch=Ledger->GetMatchEpoch(); S->AutomaticSampleId={}; S->AutomaticMinimum=MAX_int32;
            S->AutomaticLive=S->AutomaticLocalGoal=S->Peak=0;
        }
        return true;
    }
};

void UGuLiFlightAcceptanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    if (!UGuLiNetworkSettings::IsHybrid(GetWorld())) return;
    FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceExtraFlights="),AutomaticCount);
    FParse::Value(FCommandLine::Get(),TEXT("GuLiAcceptanceSeed="),AutomaticSeed);
    Collection.InitializeDependency<UGuLiHybridWorldSubsystem>();
    auto* H=GetWorld()->GetSubsystem<UGuLiHybridWorldSubsystem>();
    if (!H || !H->RegisterSharedStateSystem(AcceptanceFlightSystem,MakeShared<FGuLiAutomaticFlightState>(*this)))
    { if (H) H->Suspend(TEXT("Acceptance flight recipe handoff registration failed")); }
}
