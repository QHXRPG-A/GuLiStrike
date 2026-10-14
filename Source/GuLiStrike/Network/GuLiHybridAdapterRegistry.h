#pragma once
#include "CoreMinimal.h"

class UGuLiHybridActorWorldSubsystem;
class IGuLiHybridSimulationAdapter;
struct FGuLiHybridObjectState;
struct FGuLiHybridManifestData;
struct FGuLiTargetHandle;
enum class EGuLiHybridObjectKind : uint8;
enum class EGuLiHybridOwnershipPolicy : uint8 { Fixed, CommanderActivity, ControllerPinned, RetainedSource, SharedGroup, ParentWork };

/** A gameplay capability family, shared by any number of authored definitions.
 * Registration belongs beside the gameplay implementation, never in the world router. */
struct GULISTRIKE_API FGuLiHybridAdapterFamily
{
    FName Name;
    EGuLiHybridObjectKind Kind={};
    uint32 StateVersion=0;
    using FCreate = TSharedPtr<IGuLiHybridSimulationAdapter> (*)(UGuLiHybridActorWorldSubsystem&,const FGuLiHybridObjectState&);
    using FInstall = bool (*)(UGuLiHybridActorWorldSubsystem&);
    using FTarget = bool (*)(const FGuLiHybridManifestData&,const FGuLiHybridObjectState&,uint32 MatchEpoch,FGuLiTargetHandle&);
    FCreate Create=nullptr;
    FInstall Install=nullptr;
    FTarget ResolveTarget=nullptr;
    EGuLiHybridOwnershipPolicy OwnershipPolicy=EGuLiHybridOwnershipPolicy::Fixed;
    uint8 ControllerSeat=0;
    bool bRetireOnVacancy=false;
    FName OwnershipGroup;
};

/** Module-lifetime native registration. No connection, actor or world is held here. */
class GULISTRIKE_API FGuLiHybridAdapterRegistration final
{
public:
    explicit FGuLiHybridAdapterRegistration(FGuLiHybridAdapterFamily Family);
    ~FGuLiHybridAdapterRegistration();
    FGuLiHybridAdapterRegistration(const FGuLiHybridAdapterRegistration&)=delete;
    FGuLiHybridAdapterRegistration& operator=(const FGuLiHybridAdapterRegistration&)=delete;
    const FGuLiHybridAdapterFamily& GetFamily() const {return Family;}
private:
    FGuLiHybridAdapterFamily Family;
};

/** The hashed manifest selects families; the registry only verifies and dispatches them. */
class GULISTRIKE_API FGuLiHybridAdapterRegistry final
{
public:
    static bool GetFamilies(TArray<FGuLiHybridAdapterFamily>& Out,FString& Error);
    static bool BindDefinition(FGuLiHybridManifestData& Manifest,const FString& Definition,FName Family,FString& Error);
    static bool Validate(const FGuLiHybridManifestData& Manifest,FString& Error);
    static bool ResolveDefinition(const FGuLiHybridManifestData& Manifest,const FString& Definition,
        FGuLiHybridAdapterFamily& Out,FString& Error);
    static bool Resolve(const FGuLiHybridManifestData& Manifest,const FGuLiHybridObjectState& State,
        FGuLiHybridAdapterFamily& Out,FString& Error);
};
