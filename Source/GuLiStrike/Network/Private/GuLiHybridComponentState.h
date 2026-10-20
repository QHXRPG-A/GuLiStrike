#pragma once
#include "CoreMinimal.h"
#include "Network/GuLiHybridWorldSubsystem.h"

class AActor;
class UActorComponent;
class UClass;
struct FGuLiHybridManifestData;

/** One reusable gameplay state capability. Its registration owns no world or UObject. */
struct FGuLiHybridComponentCapability
{
    FName Name;
    uint32 Version=0;
    const UClass* (*ComponentClass)()=nullptr;
    bool (*Capture)(const UActorComponent&,TArray<uint8>&)=nullptr;
    bool (*Validate)(const UActorComponent&,TConstArrayView<uint8>)=nullptr;
    bool (*Restore)(UActorComponent&,TConstArrayView<uint8>,EGuLiHybridRestoreStage)=nullptr;
    TArray<FGuLiHybridObjectKey> (*Dependencies)(TConstArrayView<uint8>)=nullptr;
    void (*AdvanceLogical)(UActorComponent&,uint64,double)=nullptr;
    bool (*ApplyLedger)(UActorComponent&,const FGuLiHybridObjectState&,uint64,bool)=nullptr;
    bool (*PrepareRestore)(UActorComponent&,TConstArrayView<uint8>)=nullptr;
    // Compact query/presentation state; never private jobs, RNG or callbacks.
    // These three hooks are either all supplied or all absent.
    bool (*CapturePublic)(const UActorComponent&,TArray<uint8>&)=nullptr;
    bool (*ValidatePublic)(const UActorComponent&,TConstArrayView<uint8>)=nullptr;
    bool (*ApplyPublic)(UActorComponent&,TConstArrayView<uint8>)=nullptr;
    // Export authored component capabilities without adding a class/model switch
    // to the manifest pipeline. Called for native and inherited SCS templates.
    bool (*ExportDefinition)(const UActorComponent&,FGuLiHybridManifestData&,const FString&,FString&)=nullptr;
    // Optional live eligibility, checked immediately before each logical callback.
    // Omission keeps the original cadence; no eligibility result is retained.
    bool (*NeedsLogicalAdvance)(const UActorComponent&,uint64)=nullptr;
    void (*ResetResourceRequests)(UActorComponent&,uint64)=nullptr;
};

class FGuLiHybridComponentCapabilityRegistration final
{
public:
    explicit FGuLiHybridComponentCapabilityRegistration(FGuLiHybridComponentCapability Capability);
    ~FGuLiHybridComponentCapabilityRegistration();
    FGuLiHybridComponentCapabilityRegistration(const FGuLiHybridComponentCapabilityRegistration&)=delete;
    FGuLiHybridComponentCapabilityRegistration& operator=(const FGuLiHybridComponentCapabilityRegistration&)=delete;
    const FGuLiHybridComponentCapability& Get() const {return Capability;}
private:
    FGuLiHybridComponentCapability Capability;
};

/** Private to one object adapter. Weak identities survive no world or restore boundary. */
struct FGuLiHybridComponentBindingContext final
{
    struct FData;
    TUniquePtr<FData> Data;
    FGuLiHybridComponentBindingContext();
    ~FGuLiHybridComponentBindingContext();
    void Reset();
};
enum class EGuLiHybridPublicCapture : uint8 { Absent, Captured, Invalid };

/** Actor adapters aggregate registered component capabilities without knowing unit definitions. */
class FGuLiHybridComponentState final
{
public:
    static constexpr uint32 ContractVersion=4;
    static constexpr uint32 PublicMotionHz=10;
    static bool BindContracts(FGuLiHybridManifestData& Manifest,FString& Error);
    static bool ValidateContracts(const FGuLiHybridManifestData& Manifest,FString& Error);
    static bool Capture(const AActor& Actor,TArray<uint8>& Out,FGuLiHybridComponentBindingContext* Context=nullptr);
    static bool Validate(const AActor& Actor,TConstArrayView<uint8> Bytes,FGuLiHybridComponentBindingContext* Context=nullptr);
    static bool Restore(AActor& Actor,TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage,FGuLiHybridComponentBindingContext* Context=nullptr);
    static void ResetResourceRequests(AActor& Actor,uint64 Epoch,FGuLiHybridComponentBindingContext* Context=nullptr);
    static bool Advance(AActor& Actor,uint64 Tick,double Delta,FGuLiHybridComponentBindingContext* Context=nullptr);
    static bool ApplyLedger(AActor& Actor,const FGuLiHybridObjectState& State,uint64 Revision,bool Effects,FGuLiHybridComponentBindingContext* Context=nullptr);
    static bool PrepareRestore(AActor& Actor,TConstArrayView<uint8> Bytes,FGuLiHybridComponentBindingContext* Context=nullptr);
    static bool HasPublicState(const AActor& Actor,FGuLiHybridComponentBindingContext* Context=nullptr);
    static bool CapturePublic(const AActor& Actor,TArray<uint8>& Out,FGuLiHybridComponentBindingContext* Context=nullptr);
    static bool ValidatePublic(const AActor& Actor,TConstArrayView<uint8> Bytes,FGuLiHybridComponentBindingContext* Context=nullptr);
    static bool ApplyPublic(AActor& Actor,TConstArrayView<uint8> Bytes,FGuLiHybridComponentBindingContext* Context=nullptr);
    static EGuLiHybridPublicCapture TryCapturePublic(const AActor& Actor,TArray<uint8>& Out,FGuLiHybridComponentBindingContext* Context=nullptr);
    /** Counts native and inherited Blueprint component templates without spawning an actor. */
    static int32 CountComponentTemplates(const UClass& ActorClass,const UClass& ComponentClass);
    static bool BindDefinitionLayout(FGuLiHybridManifestData& Manifest,const FString& Definition,const UClass& ActorClass,FString& Error);
    static bool ValidateDefinitionLayout(const FGuLiHybridManifestData& Manifest,const FString& Definition,const AActor& Actor,FGuLiHybridComponentBindingContext* Context=nullptr);
    static TArray<FGuLiHybridObjectKey> RestoreDependencies(TConstArrayView<uint8> Bytes);
};
