#pragma once
#include "CoreMinimal.h"
#include "Network/GuLiHybridWorldSubsystem.h"

struct FGuLiHybridManifestData;
/** Composable gameplay capabilities keyed by stable identity, with or without an Actor. The host
 * knows only names, versions and fragments; definitions select the composition. */
class IGuLiHybridEntityCapability
{
public:
    virtual ~IGuLiHybridEntityCapability()=default;
    virtual void Advance(uint64 At,double Delta)=0;
    virtual bool Capture(TArray<uint8>& Out) const=0;
    virtual bool Validate(TConstArrayView<uint8> Bytes) const=0;
    virtual bool Restore(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage)=0;
    virtual void ApplyLedger(const FGuLiHybridObjectState&) {}
    virtual bool AcceptMotion(uint64) const {return true;}
    virtual TOptional<bool> Command(const FGuLiHybridCommand&,const FGuLiHybridCommandMember&) {return {};}
};
struct FGuLiHybridEntityCapabilityContract
{
    FName Name;
    uint32 Version=0;
    TSharedPtr<IGuLiHybridEntityCapability> (*Create)(UWorld&,const FGuLiHybridObjectState&,const FGuLiHybridManifestData&)=nullptr;
};
class FGuLiHybridEntityCapabilityRegistration final
{
public:
    explicit FGuLiHybridEntityCapabilityRegistration(FGuLiHybridEntityCapabilityContract Value);
    ~FGuLiHybridEntityCapabilityRegistration();
    FGuLiHybridEntityCapabilityContract Contract;
};
class FGuLiHybridEntityState
{
    struct FEntry {FName Name;uint32 Version=0;TSharedPtr<IGuLiHybridEntityCapability> Instance;};
    TArray<FEntry> Entries;
public:
    static bool Bind(FGuLiHybridManifestData& M,const FString& Definition,FName Capability,FString& Error);
    static bool ValidateManifest(const FGuLiHybridManifestData& M,FString& Error);
    bool Prepare(UWorld& World,const FGuLiHybridObjectState& Object,const FGuLiHybridManifestData& Manifest);
    void Advance(uint64 At,double Delta);
    void ApplyLedger(const FGuLiHybridObjectState& Object);
    bool AcceptMotion(uint64 At) const;
    TOptional<bool> Command(const FGuLiHybridCommand& C,const FGuLiHybridCommandMember& M);
    bool Capture(TArray<uint8>& Out) const;
    bool Validate(TConstArrayView<uint8> Bytes) const;
    bool Restore(TConstArrayView<uint8> Bytes,EGuLiHybridRestoreStage Stage);
    void Reset() {Entries.Reset();}
};
