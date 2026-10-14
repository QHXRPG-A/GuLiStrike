#pragma once
#include "CoreMinimal.h"

class UWorld;
struct FGuLiHybridManifestData;
enum class EGuLiHybridManifestPhase:uint8 {Definitions,Startup,Derived};
struct FGuLiHybridManifestProvider
{
    FName Name;
    EGuLiHybridManifestPhase Phase=EGuLiHybridManifestPhase::Definitions;
    TArray<FName> Dependencies;
    bool (*Append)(UWorld&,FGuLiHybridManifestData&,FString&)=nullptr;
};
/** Gameplay modules register their own data exporter. The editor calls only
 * AppendAll; adding a capability does not add a branch to the export pipeline. */
class GULISTRIKE_API FGuLiHybridManifestRegistration final
{
public:
    explicit FGuLiHybridManifestRegistration(FGuLiHybridManifestProvider Value);
    ~FGuLiHybridManifestRegistration();
    FGuLiHybridManifestRegistration(const FGuLiHybridManifestRegistration&)=delete;
    FGuLiHybridManifestRegistration& operator=(const FGuLiHybridManifestRegistration&)=delete;
    static bool AppendAll(UWorld& World,FGuLiHybridManifestData& Data,FString& Error);
private:
    FGuLiHybridManifestProvider Provider;
};
