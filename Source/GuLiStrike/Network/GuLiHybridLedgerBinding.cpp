#include "Network/GuLiHybridLedgerBinding.h"
#include "Network/GuLiHybridManifest.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "Network/Private/GuLiHybridStateArchive.h"
namespace
{
auto& Bindings() {static auto* V=new TArray<const FGuLiHybridLedgerBinding*>;return *V;}
uint64& BindingRevision() {static uint64 V=1;return V;}
struct FBindingContracts
{
    TMap<FName,FGuLiHybridLedgerBinding> Values;
    TArray<FName> Names;
};
TSharedPtr<const FBindingContracts> Collect()
{
    static uint64 Revision=0;
    static TSharedPtr<const FBindingContracts> Cached;
    if (Revision==BindingRevision()) return Cached;
    auto Next=MakeShared<FBindingContracts>();
    for (const auto* V:Bindings())
    {
        if (V->Capability.IsNone() || !V->Version || !V->Apply || !V->ValidateCursor || Next->Values.Contains(V->Capability)) return nullptr;
        Next->Values.Add(V->Capability,*V);Next->Names.Add(V->Capability);
    }
    Next->Names.Sort(FNameLexicalLess());
    Revision=BindingRevision();Cached=Next;return Cached;
}
}
FGuLiHybridLedgerBindingRegistration::FGuLiHybridLedgerBindingRegistration(FGuLiHybridLedgerBinding V):Binding(MoveTemp(V)) {Bindings().Add(&Binding);++BindingRevision();}
FGuLiHybridLedgerBindingRegistration::~FGuLiHybridLedgerBindingRegistration() {Bindings().Remove(&Binding);++BindingRevision();}
bool FGuLiHybridLedgerBindingRegistration::Apply(UWorld& W,const FGuLiHybridObjectState& O,TMap<FName,TArray<uint8>>& Cursors,bool Effects)
{
    const auto Contracts=Collect();if (!Contracts) return false;const auto& Values=Contracts->Values;
    for (const auto& [Name,_]:Cursors) if (!Values.Contains(Name) || !O.Capabilities.Contains(Name)) return false;
    for (auto Name:Contracts->Names) if (O.Capabilities.Contains(Name))
    {auto& Cursor=Cursors.FindOrAdd(Name);if (!Values[Name].Apply(W,O,Cursor,Effects) || !Values[Name].ValidateCursor(Cursor)) return false;}
    return ValidateCursors(O,Cursors);
}
bool FGuLiHybridLedgerBindingRegistration::ValidateCursors(const FGuLiHybridObjectState& O,const TMap<FName,TArray<uint8>>& Cursors)
{
    const auto Contracts=Collect();if (!Contracts || Cursors.Num()>32) return false;const auto& Values=Contracts->Values;
    int32 Required=0;
    for (const auto& [Name,Binding]:Values) if (O.Capabilities.Contains(Name))
    {
        ++Required;const auto* Cursor=Cursors.Find(Name);
        if (!Cursor || Cursor->Num()>4096 || !Binding.ValidateCursor(*Cursor)) return false;
    }
    return Cursors.Num()==Required;
}
bool FGuLiHybridLedgerBindingRegistration::Capture(const TMap<FName,TArray<uint8>>& Cursors,TArray<uint8>& Bytes)
{
    const auto Contracts=Collect();if (!Contracts || Cursors.Num()>32) return false;const auto& Values=Contracts->Values;
    FGuLiHybridStateArchive A(64*1024);TArray<FName> Names;Cursors.GenerateKeyArray(Names);Names.Sort(FNameLexicalLess());
    A.Array(Names,32,[&](auto& B,FName& Name)
    {
        B.Value(Name);const auto* V=Values.Find(Name);if (!V) {B.Reject();return;}uint32 Version=V->Version;B.Value(Version);
        auto Data=Cursors.FindChecked(Name);if (!V->ValidateCursor(Data)) {B.Reject();return;}B.Bytes(Data,4096);
    });return A.Finish(Bytes);
}
bool FGuLiHybridLedgerBindingRegistration::Restore(TConstArrayView<uint8> Bytes,TMap<FName,TArray<uint8>>& Cursors)
{
    const auto Contracts=Collect();if (!Contracts) return false;const auto& Values=Contracts->Values;
    TMap<FName,TArray<uint8>> Result;TArray<FName> Names;FGuLiHybridStateArchive A(Bytes,64*1024);
    A.Array(Names,32,[&](auto& B,FName& Name)
    {
        B.Value(Name);uint32 Version=0;B.Value(Version);TArray<uint8> Data;B.Bytes(Data,4096);const auto* V=Values.Find(Name);
        if (!V || V->Version!=Version || Result.Contains(Name) || !V->ValidateCursor(Data)) B.Reject();else Result.Add(Name,MoveTemp(Data));
    });if (!A.Done()) return false;Cursors=MoveTemp(Result);return true;
}
bool FGuLiHybridLedgerBindingRegistration::AppendVersions(FGuLiHybridManifestData& M,FString& Error)
{
    const auto Contracts=Collect();if (!Contracts) {Error=TEXT("Invalid/duplicate ledger binding");return false;}
    for (const auto& [Name,V]:Contracts->Values) M.Numbers.Add(TEXT("ledger_binding/")+Name.ToString().ToLower()+TEXT("/version"),V.Version);return true;
}
bool FGuLiHybridLedgerBindingRegistration::Validate(const FGuLiHybridManifestData& M,FString& Error)
{
    FGuLiHybridManifestData Required;if (!AppendVersions(Required,Error)) return false;
    for (const auto& [Name,Version]:M.Numbers) if (Name.StartsWith(TEXT("ledger_binding/")) && Name.EndsWith(TEXT("/version")))
        if (!Required.Numbers.Contains(Name) || Required.Numbers[Name]!=Version) {Error=TEXT("Missing ledger binding: ")+Name;return false;}
    for (const auto& [Name,Version]:Required.Numbers) if (!M.Numbers.Contains(Name) || M.Numbers[Name]!=Version)
    {Error=TEXT("Manifest ledger binding version differs: ")+Name;return false;}return true;
}
