#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Network/GuLiHybridManifest.h"
#include "GuLiHybridActorWorldSubsystem.generated.h"

/** Owns native actor factories for the current World; never owns a connection. */
UCLASS()
class GULISTRIKE_API UGuLiHybridActorWorldSubsystem final : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    const FGuLiHybridManifestData& GetManifest() const { return Manifest; }
    const TArray<FName>* FindEntityCapabilityNames(FName Definition) const;
    bool IsEntityLayoutCacheEnabled() const;
    int32 GetEntityCapabilityDefinitionCount() const {return EntityCapabilityNames.Num();}
    double GetEntityCapabilityIndexBuildMilliseconds() const {return EntityCapabilityIndexBuildMilliseconds;}
private:
    FGuLiHybridManifestData Manifest;
    TMap<FName,TArray<FName>> EntityCapabilityNames;
    double EntityCapabilityIndexBuildMilliseconds=0;
};
