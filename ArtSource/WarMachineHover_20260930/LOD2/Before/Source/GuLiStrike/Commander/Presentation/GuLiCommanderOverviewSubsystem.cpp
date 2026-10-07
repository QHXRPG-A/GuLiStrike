#include "Commander/Presentation/GuLiCommanderOverviewSubsystem.h"
#include "Commander/Presentation/GuLiCommanderCameraPawn.h"
#include "Commander/Presentation/GuLiCommanderPresentationActor.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiBuildingPlacementPreview.h"
#include "Gameplay/Resources/GuLiResourceActors.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Gameplay/CombatEffects/GuLiUnitWreck.h"
#include "Gameplay/Wingman/Presentation/GuLiWingmanPresentationActor.h"
#include "Commander/UI/GuLiCommanderHealthBarRenderer.h"
#include "Commander/Presentation/GuLiCommanderRouteLineComponent.h"
#include "Components/WidgetComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"

bool UGuLiCommanderOverviewSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const auto* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld() && World->GetNetMode() != NM_DedicatedServer;
}
void UGuLiCommanderOverviewSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	for (TActorIterator<AActor> It(GetWorld()); It; ++It) ActorSpawned(*It);
	SpawnHandle = GetWorld()->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &ThisClass::ActorSpawned));
}
void UGuLiCommanderOverviewSubsystem::Deinitialize()
{
	GetWorld()->RemoveOnActorSpawnedHandler(SpawnHandle);
	Actors.Reset(); Visuals.Reset();
	Super::Deinitialize();
}
void UGuLiCommanderOverviewSubsystem::ActorSpawned(AActor* Actor) { if (IsValid(Actor)) Actors.Add(Actor); }
void UGuLiCommanderOverviewSubsystem::RegisterVisual(UPrimitiveComponent* Component)
{
	if (IsValid(Component)) Visuals.AddUnique(Component);
}
void UGuLiCommanderOverviewSubsystem::ForgetVisual(UPrimitiveComponent* Component)
{
	if (IsValid(Component) && Component->GetWorld())
		if (auto* Registry = Component->GetWorld()->GetSubsystem<UGuLiCommanderOverviewSubsystem>()) Registry->Visuals.Remove(Component);
}
bool UGuLiCommanderOverviewSubsystem::IsUnitOrBuilding(const AActor* Actor)
{
	return IsValid(Actor) && !Actor->IsA<AGuLiCommanderCameraPawn>() &&
		(Actor->IsA<APawn>() || Actor->IsA<AGuLiTerritoryOutpostActor>() ||
		 Actor->FindComponentByClass<UGuLiBuildingLifecycleComponent>() || Actor->FindComponentByClass<UGuLiCombatHealthComponent>());
}
void UGuLiCommanderOverviewSubsystem::GatherHiddenComponents(TSet<FPrimitiveComponentId>& Out)
{
	Actors.RemoveAllSwap([](const auto& Weak) { return !Weak.IsValid(); });
	Visuals.RemoveAllSwap([](const auto& Weak) { return !Weak.IsValid(); });
	for (const auto& Weak : Actors)
	{
		AActor* Actor = Weak.Get();
		bool bHide = Actor->IsA<AGuLiCommanderPresentationActor>() || Actor->IsA<AGuLiWingmanPresentationActor>()
			|| Actor->IsA<AGuLiCommanderHealthBarRenderer>() || Actor->IsA<AGuLiBuildingPlacementPreview>()
			|| Actor->IsA<AGuLiUnitWreck>() || IsUnitOrBuilding(Actor);
		for (const AActor* Parent = Actor->GetAttachParentActor(); !bHide && Parent; Parent = Parent->GetAttachParentActor())
			bHide = IsUnitOrBuilding(Parent) || Parent->IsA<AGuLiCommanderPresentationActor>();
		for (const AActor* Owner = Actor->GetOwner(); !bHide && Owner; Owner = Owner->GetOwner())
			bHide = IsUnitOrBuilding(Owner);
		if (!bHide) continue;
		TInlineComponentArray<UPrimitiveComponent*> Components(Actor);
		for (UPrimitiveComponent* Component : Components)
			if (Component->IsRegistered() && !Component->IsA<UWidgetComponent>() && !Component->IsA<UGuLiCommanderRouteLineComponent>())
				Out.Add(Component->GetPrimitiveSceneId());
	}
	for (const auto& Weak : Visuals)
		if (const auto* Component = Weak.Get(); Component && Component->IsRegistered()) Out.Add(Component->GetPrimitiveSceneId());
}
