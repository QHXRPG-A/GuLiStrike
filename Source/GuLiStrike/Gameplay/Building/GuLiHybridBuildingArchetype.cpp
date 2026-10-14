#include "Gameplay/Building/GuLiHybridBuildingAdapter.h"
#include "Gameplay/Building/GuLiBuildingTypes.h"
#include "GameFramework/Actor.h"
namespace
{
auto& Archetypes() {static auto* V=new TArray<const FGuLiHybridBuildingArchetype*>;return *V;}
const FGuLiHybridBuildingArchetype* Resolve(const FGuLiBuildingDefinition& D)
{
    if (!D.IsUsable()) return nullptr;const FGuLiHybridBuildingArchetype* Result=nullptr;bool Ambiguous=false;
    TSet<FName> Names;
    for (const auto* V:Archetypes())
    {
        if (V->Family.IsNone() || !V->Supports || !V->ActorClass || Names.Contains(V->Family)) return nullptr;Names.Add(V->Family);
        if (!V->Supports(D)) continue;
        if (!Result || V->Priority>Result->Priority) {Result=V;Ambiguous=false;}
        else if (V->Priority==Result->Priority) Ambiguous=true;
    }
    return Ambiguous?nullptr:Result;
}
}
FGuLiHybridBuildingArchetypeRegistration::FGuLiHybridBuildingArchetypeRegistration(FGuLiHybridBuildingArchetype V):Value(MoveTemp(V)) {Archetypes().Add(&Value);}
FGuLiHybridBuildingArchetypeRegistration::~FGuLiHybridBuildingArchetypeRegistration() {Archetypes().Remove(&Value);}
FName GetGuLiHybridBuildingFamily(const FGuLiBuildingDefinition& D) {const auto* V=Resolve(D);return V?V->Family:NAME_None;}
UClass* GetGuLiHybridBuildingClass(const FGuLiBuildingDefinition& D)
{
    const auto* V=Resolve(D);auto* Base=V?V->ActorClass():nullptr;
    if (!Base || !Base->IsChildOf(AActor::StaticClass())) return nullptr;
    auto* Class=D.GameplayClass.IsNull()?Base:D.GameplayClass.LoadSynchronous();
    return Class && Class->IsChildOf(Base) && !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)?Class:nullptr;
}
