#include "Network/Private/GuLiHybridEntityState.h"
#include "Network/Private/GuLiHybridStateArchive.h"
#include "Network/GuLiHybridManifest.h"
namespace
{
constexpr uint32 Schema=1,Limit=2*1024*1024;
TArray<const FGuLiHybridEntityCapabilityRegistration*>& Registry()
{static auto* R=new TArray<const FGuLiHybridEntityCapabilityRegistration*>;return *R;}
const FGuLiHybridEntityCapabilityContract* Find(FName Name)
{
    const FGuLiHybridEntityCapabilityContract* Out=nullptr;
    for (const auto* R:Registry()) if (R->Contract.Name==Name)
    {if (Out || !R->Contract.Version || !R->Contract.Create) return nullptr;Out=&R->Contract;}
    return Out;
}
struct FPiece {FName Name;uint32 Version=0;TArray<uint8> Bytes;};
bool Archive(FGuLiHybridStateArchive& A,TArray<FPiece>& Parts)
{
    uint32 V=Schema;A.Value(V);if (V!=Schema) A.Reject();
    A.Array(Parts,32,[](auto& R,auto& P){R.Value(P.Name);R.Value(P.Version);R.Array(P.Bytes,1024*1024,[](auto& B,auto& X){B.Value(X);});});
    TSet<FName> Names;
    for (const auto& P:Parts) {if (P.Name.IsNone() || !P.Version || P.Bytes.IsEmpty() || Names.Contains(P.Name)) A.Reject();Names.Add(P.Name);}
    return A.Good();
}
FString Prefix(const FString& D) {return TEXT("entity_capability/")+D+TEXT("/");}
}
FGuLiHybridEntityCapabilityRegistration::FGuLiHybridEntityCapabilityRegistration(FGuLiHybridEntityCapabilityContract V):Contract(MoveTemp(V)) {Registry().Add(this);}
FGuLiHybridEntityCapabilityRegistration::~FGuLiHybridEntityCapabilityRegistration() {Registry().Remove(this);}
bool FGuLiHybridEntityState::Bind(FGuLiHybridManifestData& M,const FString& D,FName Name,FString& Error)
{
    const auto* C=Find(Name);if (!C) {Error=TEXT("Unknown entity capability: ")+Name.ToString();return false;}
    M.Numbers.Add(Prefix(D)+Name.ToString(),C->Version);return true;
}
bool FGuLiHybridEntityState::ValidateManifest(const FGuLiHybridManifestData& M,FString& Error)
{
    for (const auto& P:M.Numbers) if (P.Key.StartsWith(TEXT("entity_capability/")))
    {
        FString D,N;if (!P.Key.RightChop(18).Split(TEXT("/"),&D,&N,ESearchCase::CaseSensitive,ESearchDir::FromEnd)
            || !M.Strings.Contains(TEXT("adapter/definition/")+D)) {Error=TEXT("Entity capability has no definition: ")+P.Key;return false;}
        const auto* C=Find(FName(*N));if (!C || P.Value!=C->Version) {Error=TEXT("Incompatible entity capability: ")+P.Key;return false;}
    }
    return true;
}
bool FGuLiHybridEntityState::Prepare(UWorld& W,const FGuLiHybridObjectState& O,const FGuLiHybridManifestData& M)
{
    Entries.Reset();const auto P=Prefix(O.Definition.ToString());TArray<FName> Names;
    for (const auto& N:M.Numbers) if (N.Key.StartsWith(P)) Names.Add(FName(*N.Key.RightChop(P.Len())));
    Names.Sort([](FName A,FName B){return A.LexicalLess(B);});
    for (FName N:Names)
    {
        const auto* C=Find(N);if (!C || M.Numbers.FindRef(P+N.ToString())!=C->Version) return false;
        auto V=C->Create(W,O,M);if (!V) return false;Entries.Add({N,C->Version,MoveTemp(V)});
    }
    return true;
}
void FGuLiHybridEntityState::Advance(uint64 At,double Delta) {for (auto& E:Entries) E.Instance->Advance(At,Delta);}
bool FGuLiHybridEntityState::HasPeriodicWork() const
{for (const auto& E:Entries) if (E.Instance->HasPeriodicWork()) return true;return false;}
void FGuLiHybridEntityState::ApplyLedger(const FGuLiHybridObjectState& O) {for (auto& E:Entries) E.Instance->ApplyLedger(O);}
bool FGuLiHybridEntityState::AcceptMotion(uint64 At) const {for (const auto& E:Entries) if (!E.Instance->AcceptMotion(At)) return false;return true;}
TOptional<bool> FGuLiHybridEntityState::Command(const FGuLiHybridCommand& C,const FGuLiHybridCommandMember& M)
{for (auto& E:Entries) {auto Result=E.Instance->Command(C,M);if (Result.IsSet()) return Result;}return {};}
bool FGuLiHybridEntityState::Capture(TArray<uint8>& Out) const
{
    TArray<FPiece> Parts;for (const auto& E:Entries) {auto& P=Parts.AddDefaulted_GetRef();P.Name=E.Name;P.Version=E.Version;if (!E.Instance->Capture(P.Bytes)) return false;}
    FGuLiHybridStateArchive A(Limit);return Archive(A,Parts) && A.Finish(Out);
}
bool FGuLiHybridEntityState::Validate(TConstArrayView<uint8> Bytes) const
{
    TArray<FPiece> Parts;FGuLiHybridStateArchive A(Bytes,Limit);if (!Archive(A,Parts) || !A.Done() || Parts.Num()!=Entries.Num()) return false;
    for (int32 I=0;I<Parts.Num();++I) if (Parts[I].Name!=Entries[I].Name || Parts[I].Version!=Entries[I].Version || !Entries[I].Instance->Validate(Parts[I].Bytes)) return false;
    return true;
}
bool FGuLiHybridEntityState::Restore(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage)
{
    if (!Validate(Bytes)) return false;TArray<FPiece> Parts;FGuLiHybridStateArchive A(Bytes,Limit);if (!Archive(A,Parts) || !A.Done()) return false;
    for (int32 I=0;I<Parts.Num();++I) if (!Entries[I].Instance->Restore(Parts[I].Bytes,Stage)) return false;
    return true;
}
