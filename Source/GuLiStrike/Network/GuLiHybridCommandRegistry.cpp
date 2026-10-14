#include "Network/GuLiHybridCommandRegistry.h"
#include "Network/GuLiHybridManifest.h"
namespace
{
TArray<const FGuLiHybridGameplayCommand*>& Commands() {static auto* V=new TArray<const FGuLiHybridGameplayCommand*>;return *V;}
bool Collect(TMap<FName,const FGuLiHybridGameplayCommand*>& Out,FString& Error)
{
    for (const auto* C:Commands())
    {if (C->Operation.IsNone() || !C->Version || !C->Execute || Out.Contains(C->Operation)) {Error=TEXT("Invalid/duplicate gameplay command registration");return false;}Out.Add(C->Operation,C);}return true;
}
}
FGuLiHybridCommandRegistration::FGuLiHybridCommandRegistration(FGuLiHybridGameplayCommand V):Value(MoveTemp(V)) {Commands().Add(&Value);}
FGuLiHybridCommandRegistration::~FGuLiHybridCommandRegistration() {Commands().Remove(&Value);}
TOptional<bool> FGuLiHybridCommandRegistration::Execute(UWorld& W,const FGuLiHybridObjectState& O,const FGuLiHybridCommand& C,const FGuLiHybridCommandMember& M)
{
    TMap<FName,const FGuLiHybridGameplayCommand*> Values;FString Error;if (!Collect(Values,Error)) return false;
    if (const auto* V=Values.Find(C.Operation)) return (*V)->Execute(W,O,C,M);return {};
}
bool FGuLiHybridCommandRegistration::AppendVersions(FGuLiHybridManifestData& M,FString& Error)
{
    TMap<FName,const FGuLiHybridGameplayCommand*> Values;if (!Collect(Values,Error)) return false;
    for (const auto& [Name,V]:Values) M.Numbers.Add(TEXT("command/")+Name.ToString().ToLower()+TEXT("/version"),V->Version);return true;
}
bool FGuLiHybridCommandRegistration::Validate(const FGuLiHybridManifestData& M,FString& Error)
{
    FGuLiHybridManifestData Required;if (!AppendVersions(Required,Error)) return false;
    for (const auto& [Key,V]:M.Numbers) if (Key.StartsWith(TEXT("command/")) && Key.EndsWith(TEXT("/version")))
        if (!Required.Numbers.Contains(Key) || Required.Numbers.FindChecked(Key)!=V) {Error=TEXT("Missing/incompatible command capability: ")+Key;return false;}
    for (const auto& [Key,V]:Required.Numbers) if (!M.Numbers.Contains(Key) || M.Numbers.FindChecked(Key)!=V)
    {Error=TEXT("Manifest omits required command capability: ")+Key;return false;}return true;
}
