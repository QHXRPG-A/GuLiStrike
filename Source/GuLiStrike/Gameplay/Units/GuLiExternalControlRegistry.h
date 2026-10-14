#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Network/GuLiHybridWorldSubsystem.h"
#include "GuLiExternalControlRegistry.generated.h"

struct FGuLiExternalBody
{
    FTransform Transform;
    float Radius=20,HalfHeight=20,Altitude=0;
    bool bGroundPivot=true,bAirborne=false,bFollowParent=false,bVehicle=false,bLocked=false;
    FGuLiHybridObjectKey Parent;
    TWeakObjectPtr<AActor> Actor;
};
struct FGuLiExternalControl
{
    FGuid Token;uint32 Step=0;uint64 LogicalTick=0;
    bool bPhased=false,bLocked=false,bDisplace=false;
    FTransform Transform;
};
/** Gameplay endpoints, independent of transport, model and selection routing. */
UCLASS()
class GULISTRIKE_API UGuLiExternalControlRegistry : public UWorldSubsystem
{
    GENERATED_BODY()
    struct FEndpoint
    {
        TWeakObjectPtr<UObject> Owner;
        TFunction<bool(FGuLiExternalBody&)> Query;
        TFunction<bool(const FGuLiExternalControl&)> Apply;
        TFunction<bool(const FTransform&,TConstArrayView<FGuLiHybridObjectKey>,FTransform&)> PlanAttached;
    };
    TMap<FGuLiHybridObjectKey,FEndpoint> Entries;
public:
    bool Bind(FGuLiHybridObjectKey Key,UObject& Owner,TFunction<bool(FGuLiExternalBody&)> Query,TFunction<bool(const FGuLiExternalControl&)> Apply,
        TFunction<bool(const FTransform&,TConstArrayView<FGuLiHybridObjectKey>,FTransform&)> PlanAttached={});
    void Unbind(FGuLiHybridObjectKey Key,const UObject& Owner);
    bool Query(FGuLiHybridObjectKey Key,FGuLiExternalBody& Out) const;
    bool Apply(FGuLiHybridObjectKey Key,const FGuLiExternalControl& State) const;
    TOptional<bool> PlanAttached(FGuLiHybridObjectKey Key,const FTransform& Parent,TConstArrayView<FGuLiHybridObjectKey> Siblings,FTransform& Out) const;
};
