#pragma once
#include "CoreMinimal.h"
class UWorld;
struct FGuLiHybridObjectState;
struct FGuLiHybridCommand;
struct FGuLiHybridCommandMember;
struct FGuLiHybridManifestData;
enum class EGuLiHybridCommandPreparation : uint8 {Ready,Pending,Rejected};
struct FGuLiHybridGameplayCommand
{
    FName Operation;uint32 Version=0;
    using FExecute=bool(*)(UWorld&,const FGuLiHybridObjectState&,const FGuLiHybridCommand&,const FGuLiHybridCommandMember&);
    FExecute Execute=nullptr;
    using FPrepare=EGuLiHybridCommandPreparation(*)(UWorld&,const FGuLiHybridObjectState&,const FGuLiHybridCommand&,const FGuLiHybridCommandMember&);
    using FCancel=void(*)(UWorld&,const FGuLiHybridObjectState&,const FGuLiHybridCommand&,const FGuLiHybridCommandMember&);
    FPrepare Prepare=nullptr;
    FCancel Cancel=nullptr;
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
    static EGuLiHybridCommandPreparation Prepare(UWorld&,const FGuLiHybridObjectState&,const FGuLiHybridCommand&,const FGuLiHybridCommandMember&);
    static bool HasPreparation(FName Operation);
    static void Cancel(UWorld&,const FGuLiHybridObjectState&,const FGuLiHybridCommand&,const FGuLiHybridCommandMember&);
    static bool AppendVersions(FGuLiHybridManifestData&,FString&);
    static bool Validate(const FGuLiHybridManifestData&,FString&);
};
