#include "Commander/Behavior/GuLiCommanderBehaviorSchema.h"
#include "Gameplay/Data/GuLiUnitDataSubsystem.h"
#include "Gameplay/Resources/GuLiMiningVehiclePawn.h"
#include "Gameplay/Building/GuLiConstructionVehiclePawn.h"
#include "StateTree.h"
#include "Engine/World.h"

UGuLiCommanderActorStateTreeSchema::UGuLiCommanderActorStateTreeSchema()
{
	ScheduledTickPolicy = EStateTreeComponentSchemaScheduledTickPolicy::Denied;
}

const FGuLiCommanderBehaviorPolicy* GuLiCommanderBehavior::GetPolicy(const UStateTree* Tree)
{
	if (!Tree || !Tree->IsReadyToRun()) return nullptr;
	if (const auto* Schema = Cast<UGuLiCommanderActorStateTreeSchema>(Tree->GetSchema())) return &Schema->Policy;
	if (const auto* Schema = Cast<UGuLiCommanderMassStateTreeSchema>(Tree->GetSchema())) return &Schema->Policy;
	return nullptr;
}

const FGuLiCommanderBehaviorPolicy* GuLiCommanderBehavior::FindPolicy(const UWorld& World, uint16 UnitTypeId)
{
	const auto* Units = World.GetSubsystem<UGuLiUnitDataSubsystem>();
	const auto* Unit = Units ? Units->FindDefinition(UnitTypeId) : nullptr;
	return Unit ? GetPolicy(Unit->StateTreeAsset) : nullptr;
}

const FGuLiCommanderBehaviorPolicy* GuLiCommanderBehavior::FindPolicyByWireId(const UWorld& World, int32 WireId)
{
	const auto* Units = World.GetSubsystem<UGuLiUnitDataSubsystem>();
	if (Units && Units->IsCatalogValid())
		for (const auto& Unit : Units->GetDefinitions())
			if (const auto* Policy = GetPolicy(Unit.StateTreeAsset); Policy && Policy->GetWireId() == WireId) return Policy;
	return nullptr;
}

bool GuLiCommanderBehavior::ValidateDefinition(const FGuLiSoldierDefinition& Unit, FString& Error)
{
	const UStateTree* Tree = Unit.StateTreeAsset;
	const auto* Policy = GetPolicy(Tree);
	if (!Policy) { Error = TEXT("StateTreeAsset is missing, uncompiled, or has no Commander schema."); return false; }
	if (Unit.UsesMass() != Tree->GetSchema()->IsA<UGuLiCommanderMassStateTreeSchema>())
	{ Error = TEXT("StateTreeAsset schema does not match ActorClass (Actor/Mass)."); return false; }
	const bool bSupported = Policy->Behavior == EGuLiCommanderBehavior::StrongholdAdvance ? Unit.UsesMass()
		: Policy->Behavior == EGuLiCommanderBehavior::Mining ? Unit.ActorClass && Unit.ActorClass->IsChildOf(AGuLiMiningVehiclePawn::StaticClass())
		: Policy->Behavior == EGuLiCommanderBehavior::Construction && Unit.ActorClass && Unit.ActorClass->IsChildOf(AGuLiConstructionVehiclePawn::StaticClass());
	if (!bSupported || Policy->DisplayName.TrimStartAndEnd().IsEmpty())
	{ Error = TEXT("StateTreeAsset declares an unsupported capability or an empty display name."); return false; }
	return true;
}
