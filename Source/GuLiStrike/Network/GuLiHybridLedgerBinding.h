#pragma once
#include "CoreMinimal.h"
class UWorld;
struct FGuLiHybridObjectState;
struct FGuLiHybridManifestData;

/** Gameplay applies a committed capability and retains a bounded application cursor.
 * Cursors are private handoff state: a restore never repeats a displacement or reward. */
struct FGuLiHybridLedgerBinding
{
    FName Capability;
    uint32 Version=0;
    bool (*Apply)(UWorld&,const FGuLiHybridObjectState&,TArray<uint8>& Cursor,bool Effects)=nullptr;
    bool (*ValidateCursor)(TConstArrayView<uint8>)=nullptr;
};
class FGuLiHybridLedgerBindingRegistration final
{
    FGuLiHybridLedgerBinding Binding;
public:
    explicit FGuLiHybridLedgerBindingRegistration(FGuLiHybridLedgerBinding Value);
    ~FGuLiHybridLedgerBindingRegistration();
    FGuLiHybridLedgerBindingRegistration(const FGuLiHybridLedgerBindingRegistration&)=delete;
    FGuLiHybridLedgerBindingRegistration& operator=(const FGuLiHybridLedgerBindingRegistration&)=delete;
    static bool Apply(UWorld&,const FGuLiHybridObjectState&,TMap<FName,TArray<uint8>>& Cursors,bool Effects);
    static bool Capture(const TMap<FName,TArray<uint8>>& Cursors,TArray<uint8>& Bytes);
    static bool Restore(TConstArrayView<uint8> Bytes,TMap<FName,TArray<uint8>>& Cursors);
    /** Require exactly the bindings used by this object, without applying gameplay state. */
    static bool ValidateCursors(const FGuLiHybridObjectState&,const TMap<FName,TArray<uint8>>& Cursors);
    static bool AppendVersions(FGuLiHybridManifestData&,FString& Error);
    static bool Validate(const FGuLiHybridManifestData&,FString& Error);
};
