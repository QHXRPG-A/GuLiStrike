#include "Gameplay/Building/GuLiConstructionPresentationComponent.h"
#include "Gameplay/Building/GuLiBuildingLifecycleComponent.h"
#include "Gameplay/Building/GuLiBuildingConstructionVisualComponent.h"
#include "Gameplay/Units/GuLiEngineeringTravelComponent.h"
#include "Gameplay/Units/GuLiExternalUnitControlComponent.h"
#include "Gameplay/Presentation/GuLiUnitRenderPolicy.h"
#include "Gameplay/Vfx/GuLiVfxRegistrySubsystem.h"
#include "Battle/Combat/GuLiCombatDamageLedger.h"
#include "Components/SceneComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/StructOnScope.h"

UGuLiConstructionPresentationComponent::UGuLiConstructionPresentationComponent()
{
	SetIsReplicatedByDefault(true);
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UGuLiConstructionPresentationComponent::BeginPlay()
{
	Super::BeginPlay();
	OnRep_State();
}

void UGuLiConstructionPresentationComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	HideBeams();
	Super::EndPlay(Reason);
}

void UGuLiConstructionPresentationComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGuLiConstructionPresentationComponent, State);
}

void UGuLiConstructionPresentationComponent::SetConstructionAuthority(UGuLiBuildingLifecycleComponent* Building)
{
	if (!GetOwner()->HasAuthority()) return;
	const bool bActive = Building && Building->GetState().Phase == EGuLiBuildingPhase::UnderConstruction;
	AActor* Target = bActive ? Building->GetOwner() : nullptr;
	const uint32 Instance = bActive ? Building->GetState().InstanceId : 0;
	if (State.bActive == bActive && State.Building == Target && State.BuildingInstance == Instance) return;
	State.Building = Target; State.BuildingInstance = Instance; State.bActive = bActive;
	const auto* GameState = GetWorld()->GetGameState();
	State.ScanStartedAt = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	OnRep_State();
	GetOwner()->FlushNetDormancy(); GetOwner()->ForceNetUpdate();
}

void UGuLiConstructionPresentationComponent::OnRep_State()
{
	SelectedSpan = {};
	HideBeams();
	SetComponentTickEnabled(State.bActive && GetOwner()->GetNetMode() != NM_DedicatedServer);
}

void UGuLiConstructionPresentationComponent::InitializePresentation(AActor* Actor)
{
	HideBeams();
	const bool bSameActor = Presentation.Get() == Actor;
	Presentation = Actor;
	if (!Actor || GetOwner()->GetNetMode() == NM_DedicatedServer) return;
	// This also clears MiningActive, so inherited Blueprint logic cannot reactivate green beams.
	if (auto* Stop = Actor->FindFunction(TEXT("StopMining")))
	{
		FStructOnScope Args(Stop);
		Actor->ProcessEvent(Stop, Args.GetStructMemory());
	}
	Actor->SetActorTickEnabled(false);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		Pivots[Side].Reset(); Muzzles[Side].Reset(); Beams[Side].Reset();
	}
	TInlineComponentArray<USceneComponent*> Components(Actor);
	for (auto* Component : Components)
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FString Suffix = Side == 0 ? TEXT("L") : TEXT("R");
			if (Component->GetFName() == FName(TEXT("CollectorPivot_") + Suffix)) Pivots[Side] = Component;
			if (Component->GetFName() == FName(TEXT("LaserMuzzle_") + Suffix)) Muzzles[Side] = Component;
			if (Component->GetFName() == FName(TEXT("MiningLaser_") + Suffix)) Beams[Side] = Cast<UNiagaraComponent>(Component);
		}
	const int32 VfxId = GetDefault<UGuLiBuildingConstructionSettings>()->LaserVfxId;
	auto* System = GuLiVfx::Load<UNiagaraSystem>(this, VfxId);
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (!bSameActor && Pivots[Side].IsValid()) TravelRotations[Side] = Pivots[Side]->GetRelativeRotation();
		if (auto* Beam = Beams[Side].Get())
		{
			Beam->bAutoActivate = false;
			Beam->DeactivateImmediate(); Beam->SetVisibility(false);
			Beam->SetAsset(System);
			// Beam width is in world centimeters, independent of the vehicle presentation scale.
			Beam->SetAbsolute(false, false, true);
			Beam->SetRelativeScale3D(GuLiVfx::Scale(this, VfxId));
			Beam->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Beam->SetCastShadow(false);
			GuLiUnitRenderPolicy::ApplyReflectionExclusions(*Beam);
		}
	}
	SelectedSpan = {};
	SetComponentTickEnabled(State.bActive);
}

void UGuLiConstructionPresentationComponent::HideBeams()
{
	for (int32 Side = 0; Side < 2; ++Side)
	{
		if (auto* Beam = Beams[Side].Get())
		{
			Beam->DeactivateImmediate(); Beam->SetVisibility(false);
		}
		if (bShowing && Pivots[Side].IsValid()) Pivots[Side]->SetRelativeRotation(TravelRotations[Side]);
	}
	bShowing = false;
}

void UGuLiConstructionPresentationComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* Function)
{
	Super::TickComponent(Dt, TickType, Function);
	const auto* Health = GetOwner()->FindComponentByClass<UGuLiCombatHealthComponent>();
	const auto* Travel = GetOwner()->FindComponentByClass<UGuLiEngineeringTravelComponent>();
	const auto* Building = IsValid(State.Building) ? State.Building->FindComponentByClass<UGuLiBuildingLifecycleComponent>() : nullptr;
	auto* Visual = IsValid(State.Building) ? State.Building->FindComponentByClass<UGuLiBuildingConstructionVisualComponent>() : nullptr;
	if (!State.bActive || !Health || !Health->IsAlive() || !Travel || Travel->IsInTransit() || Travel->IsRouting()
		|| UGuLiExternalUnitControlComponent::AreActorActionsLocked(GetOwner())
		|| !Presentation.IsValid() || Presentation->IsHidden() || !Building || !Visual
		|| Building->GetState().InstanceId != State.BuildingInstance || Building->GetState().Phase != EGuLiBuildingPhase::UnderConstruction
		|| !Visual->IsConstructionVisible())
	{ if (bShowing) HideBeams(); return; }
	for (int32 Side = 0; Side < 2; ++Side)
		if (!Pivots[Side].IsValid() || !Muzzles[Side].IsValid() || !Beams[Side].IsValid() || !Beams[Side]->GetAsset())
		{ if (bShowing) HideBeams(); return; }
	if (SelectedSpan.Contour == INDEX_NONE || SelectedSpan.Revision != Visual->GetShapeRevision())
		if (!Visual->FindNearestConstructionSpan(GetOwner()->GetActorLocation(), SelectedSpan)) { HideBeams(); return; }
	const auto* GameState = GetWorld()->GetGameState();
	const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const auto* Vehicle = Cast<IGuLiEngineeringVehicle>(GetOwner());
	const double Phase = Vehicle ? FMath::Frac(double(Vehicle->GetStableActorId().Value) * 0.38196601125) : 0.0;
	// Cosine makes a smooth two-second out-and-back motion, with no reversal jerk.
	const float Center = 0.5f + 0.19f * FMath::Sin(UE_PI * (Now - State.ScanStartedAt + Phase * 2.0));
	FVector EdgeStart, EdgeEnd;
	if (!Visual->SampleConstructionContour(SelectedSpan.Contour, SelectedSpan.Start, EdgeStart) || !Visual->SampleConstructionContour(SelectedSpan.Contour, SelectedSpan.Start + SelectedSpan.Length, EdgeEnd))
	{ HideBeams(); return; }
	const float SideOrder = FVector::DotProduct(EdgeEnd - EdgeStart, GetOwner()->GetActorRightVector()) >= 0 ? 1.0f : -1.0f;
	for (int32 Side = 0; Side < 2; ++Side)
	{
		FVector Contact;
		if (!Visual->SampleConstructionContour(SelectedSpan.Contour, SelectedSpan.Start + SelectedSpan.Length * (Center + SideOrder * (Side == 0 ? -0.075f : 0.075f)), Contact))
		{ HideBeams(); return; }
		Pivots[Side]->SetWorldRotation((Contact - Pivots[Side]->GetComponentLocation()).Rotation());
		// Niagara remains attached to the real muzzle; only the endpoint is updated.
		Beams[Side]->SetVariablePosition(TEXT("User.Beam End"), Contact);
		Beams[Side]->SetVisibility(true);
		if (!bShowing) Beams[Side]->Activate(true);
	}
	bShowing = true;
}
