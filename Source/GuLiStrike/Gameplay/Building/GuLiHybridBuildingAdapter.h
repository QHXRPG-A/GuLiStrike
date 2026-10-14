#pragma once
#include "CoreMinimal.h"
struct FGuLiBuildingDefinition;
/** Gameplay category/capabilities choose a reusable family; definition IDs do not. */
FName GetGuLiHybridBuildingFamily(const FGuLiBuildingDefinition& Definition);
UClass* GetGuLiHybridBuildingClass(const FGuLiBuildingDefinition& Definition);
/** A new gameplay mechanism registers its reusable building implementation here. */
struct FGuLiHybridBuildingArchetype
{
    FName Family;
    int32 Priority=0;
    bool (*Supports)(const FGuLiBuildingDefinition&)=nullptr;
    UClass* (*ActorClass)()=nullptr;
};
class FGuLiHybridBuildingArchetypeRegistration final
{
    FGuLiHybridBuildingArchetype Value;
public:
    explicit FGuLiHybridBuildingArchetypeRegistration(FGuLiHybridBuildingArchetype V);
    ~FGuLiHybridBuildingArchetypeRegistration();
    FGuLiHybridBuildingArchetypeRegistration(const FGuLiHybridBuildingArchetypeRegistration&)=delete;
    FGuLiHybridBuildingArchetypeRegistration& operator=(const FGuLiHybridBuildingArchetypeRegistration&)=delete;
};
