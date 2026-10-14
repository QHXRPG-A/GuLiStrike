#include "Network/GuLiHybridManifestRegistry.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridCommandRegistry.h"
#include "Network/GuLiHybridLedgerBinding.h"

namespace
{
TArray<const FGuLiHybridManifestProvider*>& Providers()
{static auto* Values=new TArray<const FGuLiHybridManifestProvider*>;return *Values;}
}
FGuLiHybridManifestRegistration::FGuLiHybridManifestRegistration(FGuLiHybridManifestProvider Value):Provider(MoveTemp(Value))
{Providers().Add(&Provider);}
FGuLiHybridManifestRegistration::~FGuLiHybridManifestRegistration() {Providers().Remove(&Provider);}
bool FGuLiHybridManifestRegistration::AppendAll(UWorld& World,FGuLiHybridManifestData& Data,FString& Error)
{
    TArray<const FGuLiHybridManifestProvider*> Pending=Providers();TSet<FName> Names,Complete;
    if (Pending.IsEmpty()) {Error=TEXT("No gameplay manifest providers are registered");return false;}
    for (const auto* P:Pending)
    {
        if (P->Name.IsNone() || !P->Append || Names.Contains(P->Name)) {Error=TEXT("Invalid or duplicate gameplay manifest provider");return false;}
        Names.Add(P->Name);
    }
    for (const auto* P:Pending) for (const auto Name:P->Dependencies)
    {
        const auto* Found=Pending.FindByPredicate([&](const auto* D){return D->Name==Name;});
        if (!Found || uint8((*Found)->Phase)>uint8(P->Phase)) {Error=TEXT("Missing or later-phase manifest dependency: ")+Name.ToString();return false;}
    }
    Pending.Sort([](const auto& A,const auto& B) {return A.Phase!=B.Phase?uint8(A.Phase)<uint8(B.Phase):A.Name.LexicalLess(B.Name);});
    while (!Pending.IsEmpty())
    {
        const auto Phase=Pending[0]->Phase;
        const int32 Next=Pending.IndexOfByPredicate([&](const auto* P)
        {return P->Phase==Phase && !P->Dependencies.ContainsByPredicate([&](FName D){return !Complete.Contains(D);});});
        if (Next==INDEX_NONE) {Error=TEXT("Cyclic gameplay manifest dependencies");return false;}
        const auto* P=Pending[Next];if (!P->Append(World,Data,Error)) {Error=P->Name.ToString()+TEXT(": ")+Error;return false;}
        Complete.Add(P->Name);Pending.RemoveAt(Next);
    }
    if (!FGuLiHybridCommandRegistration::AppendVersions(Data,Error)) return false;
    if (!FGuLiHybridLedgerBindingRegistration::AppendVersions(Data,Error)) return false;
    Error.Reset();return true;
}
