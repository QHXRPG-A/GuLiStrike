#pragma once
#include "CoreMinimal.h"
class UWorld;
struct FGuLiHybridObjectState;
struct FGuLiHybridCommand;
struct FGuLiHybridCommandMember;
struct FGuLiHybridManifestData;
struct FGuLiHybridGameplayCommand
{
    FName Operation;uint32 Version=0;
    using FExecute=bool(*)(UWorld&,const FGuLiHybridObjectState&,const FGuLiHybridCommand&,const FGuLiHybridCommandMember&);
    FExecute Execute=nullptr;
};
/** Gameplay-owned command extension; runs only after the shared authority and
 * command/object deduplication checks have accepted delivery. */
class GULISTRIKE_API FGuLiHybridCommandRegistration
{
    FGuLiHybridGameplayCommand Value;
public:
    explicit FGuLiHybridCommandRegistration(FGuLiHybridGameplayCommand);
    ~FGuLiHybridCommandRegistration();
    FGuLiHybridCommandRegistration(const FGuLiHybridCommandRegistration&)=delete;
    static TOptional<bool> Execute(UWorld&,const FGuLiHybridObjectState&,const FGuLiHybridCommand&,const FGuLiHybridCommandMember&);
    static bool AppendVersions(FGuLiHybridManifestData&,FString&);
    static bool Validate(const FGuLiHybridManifestData&,FString&);
};
